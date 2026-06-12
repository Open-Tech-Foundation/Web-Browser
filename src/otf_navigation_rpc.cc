#include "otf_navigation_rpc.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <set>
#include <string>

#include "include/views/cef_browser_view.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {
namespace {

using Callback = CefMessageRouterBrowserSide::Handler::Callback;

bool HasOnlyParamKeys(CefRefPtr<CefDictionaryValue> params,
                      const std::set<std::string>& allowed,
                      std::string* error) {
  CefDictionaryValue::KeyList keys;
  params->GetKeys(keys);
  for (const auto& key : keys) {
    const std::string k = key.ToString();
    if (!allowed.count(k)) {
      if (error) *error = "unexpected param: " + k;
      return false;
    }
  }
  return true;
}

bool IsDangerousSchemeUrl(const std::string& url) {
  static const char* kDangerousSchemes[] = {
      "javascript:", "data:", "file:", "vbscript:", "blob:"};
  for (const char* scheme : kDangerousSchemes) {
    if (url.rfind(scheme, 0) == 0) return true;
  }
  return false;
}

std::string TrimWhitespaceCopy(const std::string& value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

bool HasWhitespace(const std::string& value) {
  return std::any_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isspace(c);
  });
}

bool HasStrongUrlIntent(const std::string& value) {
  return value.find_first_of("/:?#") != std::string::npos;
}

std::string ExtractHostCandidate(const std::string& value) {
  const size_t end = value.find_first_of("/:?#");
  return value.substr(0, end == std::string::npos ? value.size() : end);
}

std::optional<int> ExtractExplicitPort(const std::string& value) {
  const size_t scheme_pos = value.find("://");
  const size_t host_start = scheme_pos == std::string::npos ? 0 : scheme_pos + 3;
  const size_t host_end = value.find_first_of("/?#", host_start);
  const std::string authority =
      value.substr(host_start, host_end == std::string::npos
                                 ? std::string::npos
                                 : host_end - host_start);
  const size_t colon = authority.rfind(':');
  if (colon == std::string::npos || colon + 1 >= authority.size()) {
    return std::nullopt;
  }

  const std::string port_text = authority.substr(colon + 1);
  if (!std::all_of(port_text.begin(), port_text.end(), [](unsigned char c) {
        return std::isdigit(c);
      })) {
    return std::nullopt;
  }

  int port = 0;
  for (char c : port_text) {
    port = (port * 10) + (c - '0');
    if (port > 65535) return port;
  }
  return port;
}

bool HasValidExplicitPort(const std::string& value) {
  const std::optional<int> port = ExtractExplicitPort(value);
  return !port.has_value() || (*port >= 0 && *port <= 65535);
}

bool LooksLikeHostSyntax(const std::string& value) {
  static const std::regex localhost_pattern(
      R"(^localhost(?::\d{1,5})?(?:[/?#]|$))",
      std::regex_constants::icase);
  static const std::regex ipv4_pattern(
      R"(^(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?:[/?#]|$))");
  static const std::regex domain_pattern(
      R"(^(?=.{1,253}$)(?!-)(?:[a-zA-Z\d-]{1,63}\.)+[a-zA-Z]{2,63}(?::\d{1,5})?(?:[/?#].*)?$)");
  return std::regex_search(value, localhost_pattern) ||
         std::regex_search(value, ipv4_pattern) ||
         std::regex_search(value, domain_pattern);
}

bool HasExplicitScheme(const std::string& value) {
  static const std::regex explicit_scheme_pattern(
      R"(^[a-zA-Z][a-zA-Z\d+.-]*://)");
  return std::regex_search(value, explicit_scheme_pattern);
}

bool IsLocalhostOrIpv4(const std::string& value) {
  static const std::regex localhost_pattern(
      R"(^localhost(?::\d{1,5})?(?:[/?#]|$))",
      std::regex_constants::icase);
  static const std::regex ipv4_pattern(
      R"(^(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?:[/?#]|$))");
  return std::regex_search(value, localhost_pattern) ||
         std::regex_search(value, ipv4_pattern);
}

bool ResolveUserInputUrlWithoutDns(const std::string& input,
                                   const std::string& search_engine_id,
                                   std::string* resolved_url,
                                   std::string* dns_host) {
  const std::string trimmed = TrimWhitespaceCopy(input);
  if (trimmed.rfind("browser://", 0) == 0 ||
      trimmed.rfind("//", 0) == 0 ||
      HasExplicitScheme(trimmed)) {
    if (IsDangerousSchemeUrl(trimmed)) {
      *resolved_url = otf::BuildSearchUrl(search_engine_id, trimmed);
      return true;
    }
    *resolved_url = trimmed;
    return true;
  }

  if (HasWhitespace(trimmed) || !LooksLikeHostSyntax(trimmed)) {
    *resolved_url = otf::BuildSearchUrl(search_engine_id, trimmed);
    return true;
  }

  if (!HasValidExplicitPort(trimmed)) {
    *resolved_url = otf::BuildSearchUrl(search_engine_id, trimmed);
    return true;
  }

  if (IsLocalhostOrIpv4(trimmed)) {
    *resolved_url = "http://" + trimmed;
    return true;
  }

  if (HasStrongUrlIntent(trimmed)) {
    *resolved_url = "https://" + trimmed;
    return true;
  }

  *dns_host = ExtractHostCandidate(trimmed);
  return false;
}

bool ReadRequiredInput(CefRefPtr<CefDictionaryValue> params,
                       std::string* input,
                       std::string* error) {
  if (!params || !params->HasKey("input") ||
      params->GetType("input") != VTYPE_STRING) {
    if (error) *error = "input must be a string";
    return false;
  }
  std::string parsed = params->GetString("input").ToString();
  if (parsed.empty()) {
    if (error) *error = "input must not be empty";
    return false;
  }
  if (input) *input = std::move(parsed);
  return true;
}

class RpcUserInputResolveCallback : public CefResolveCallback {
 public:
  RpcUserInputResolveCallback(
      CefRefPtr<Callback> callback,
      NativeRpcRequest request,
      std::string input,
      std::string search_engine_id)
      : callback_(callback),
        request_(std::move(request)),
        input_(std::move(input)),
        search_engine_id_(std::move(search_engine_id)) {}

  void OnResolveCompleted(cef_errorcode_t result,
                          const std::vector<CefString>& resolved_ips) override {
    const std::string trimmed = TrimWhitespaceCopy(input_);
    const bool resolved = result == ERR_NONE && !resolved_ips.empty();
    NativeRpcSuccessString(
        callback_, request_,
        resolved ? "https://" + trimmed
                 : otf::BuildSearchUrl(search_engine_id_, trimmed));
  }

 private:
  CefRefPtr<Callback> callback_;
  NativeRpcRequest request_;
  std::string input_;
  std::string search_engine_id_;

  IMPLEMENT_REFCOUNTING(RpcUserInputResolveCallback);
};

bool ReadRequiredUrl(CefRefPtr<CefDictionaryValue> params,
                     std::string* url,
                     std::string* error) {
  if (!params || !params->HasKey("url") ||
      params->GetType("url") != VTYPE_STRING) {
    if (error) *error = "url must be a string";
    return false;
  }
  std::string parsed = params->GetString("url").ToString();
  if (parsed.empty()) {
    if (error) *error = "url must not be empty";
    return false;
  }
  if (IsDangerousSchemeUrl(parsed)) {
    if (error) *error = "dangerous scheme";
    return false;
  }
  if (url) *url = std::move(parsed);
  return true;
}

bool ReadOptionalUrl(CefRefPtr<CefDictionaryValue> params,
                     std::string* url,
                     std::string* error) {
  if (!params || !params->HasKey("url")) {
    if (url) *url = "browser://newtab";
    return true;
  }
  return ReadRequiredUrl(params, url, error);
}

bool ReadTabId(CefRefPtr<CefDictionaryValue> params,
               int* tab_id,
               std::string* error) {
  if (!params || !params->HasKey("tabId") ||
      params->GetType("tabId") != VTYPE_INT) {
    if (error) *error = "tabId must be an integer";
    return false;
  }
  const int parsed = params->GetInt("tabId");
  if (parsed < 0) {
    if (error) *error = "tabId must be non-negative";
    return false;
  }
  if (tab_id) *tab_id = parsed;
  return true;
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

void SetSchemeUrlIfNeeded(OtfHandler* handler,
                          int tab_id,
                          const std::string& url) {
  if (handler && handler->tab_manager_ && url.rfind("browser://", 0) == 0) {
    handler->tab_manager_->SetSchemeUrl(tab_id, url);
  }
}

}  // namespace

bool HandleNavigationRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  if (!handler ||
      (request.method != "navigation.current" &&
       request.method != "navigation.tab" &&
       request.method != "navigation.newTab" &&
       request.method != "navigation.newPrivateTab" &&
       request.method != "navigation.resolveInput")) {
    return false;
  }

  std::string error;
  if (request.method == "navigation.resolveInput") {
    if (!HasOnlyParamKeys(request.params, {"input"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    std::string input;
    if (!ReadRequiredInput(request.params, &input, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    const std::optional<std::string> search_engine_id =
        otf::GetCurrentSearchEngineId();
    const std::string engine_id = search_engine_id.value_or("");
    std::string resolved_url;
    std::string dns_host;
    const bool resolved_without_dns =
        ResolveUserInputUrlWithoutDns(input, engine_id, &resolved_url, &dns_host);
    const bool looks_like_url =
        resolved_without_dns &&
        (resolved_url.rfind("http://", 0) == 0 ||
         resolved_url.rfind("https://", 0) == 0 ||
         resolved_url.rfind("browser://", 0) == 0 ||
         resolved_url.rfind("//", 0) == 0 ||
         HasExplicitScheme(resolved_url));
    if (looks_like_url) {
      NativeRpcSuccessString(callback, request, resolved_url);
      return true;
    }
    if (!search_engine_id.has_value()) {
      NativeRpcSuccessString(callback, request, "browser://settings");
      return true;
    }
    if (resolved_without_dns) {
      NativeRpcSuccessString(callback, request, resolved_url);
      return true;
    }
    CefRefPtr<CefRequestContext> request_context =
        browser ? browser->GetHost()->GetRequestContext()
                : CefRequestContext::GetGlobalContext();
    if (!request_context) {
      NativeRpcSuccessString(
          callback, request,
          otf::BuildSearchUrl(*search_engine_id, TrimWhitespaceCopy(input)));
      return true;
    }
    request_context->ResolveHost(
        "https://" + dns_host,
        new RpcUserInputResolveCallback(callback, request, input,
                                        *search_engine_id));
    return true;
  }

  if (request.method == "navigation.current") {
    if (!HasOnlyParamKeys(request.params, {"url"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    std::string url;
    if (!ReadRequiredUrl(request.params, &url, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view && browser && handler->tab_manager_) {
      const int tab_id = view->GetID();
      SetSchemeUrlIfNeeded(handler, tab_id, url);
      browser->GetMainFrame()->LoadURL(url);
    }
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  if (request.method == "navigation.tab") {
    if (!HasOnlyParamKeys(request.params, {"tabId", "url"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    int tab_id = -1;
    std::string url;
    if (!ReadTabId(request.params, &tab_id, &error) ||
        !ReadRequiredUrl(request.params, &url, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    CefRefPtr<CefBrowser> target =
        handler->tab_manager_ ? handler->tab_manager_->GetBrowser(tab_id)
                              : nullptr;
    if (target) {
      SetSchemeUrlIfNeeded(handler, tab_id, url);
      target->GetMainFrame()->LoadURL(url);
      if (OtfApp* app = OtfApp::GetInstance()) {
        app->FocusCurrentTabContent();
      }
    }
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  if (request.method == "navigation.newPrivateTab") {
    if (!HasOnlyParamKeys(request.params, {"url"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    std::string url;
    if (!ReadOptionalUrl(request.params, &url, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    OtfApp* app = OtfApp::GetInstance();
    if (!app) {
      Failure(callback, request, "not_ready", "App not ready");
      return true;
    }
    const int id = app->CreateTab(url, -1, /*is_private=*/true);
    handler->NotifyNewTab(id, -1);
    app->SwitchTab(id);
    NativeRpcSuccessRaw(callback, request, std::to_string(id));
    return true;
  }

  if (!HasOnlyParamKeys(request.params, {"url"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  std::string url;
  if (!ReadOptionalUrl(request.params, &url, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  OtfApp* app = OtfApp::GetInstance();
  if (!app) {
    Failure(callback, request, "not_ready", "App not ready");
    return true;
  }
  const int id = app->CreateTab(url);
  handler->NotifyNewTab(id, -1);
  app->SwitchTab(id);
  handler->PersistWorkspaceForTab(id);
  NativeRpcSuccessRaw(callback, request, std::to_string(id));
  return true;
}

}  // namespace otf
