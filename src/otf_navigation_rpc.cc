#include "otf_navigation_rpc.h"

#include <set>
#include <string>

#include "include/views/cef_browser_view.h"
#include "otf_app.h"
#include "otf_handler.h"

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
       request.method != "navigation.newTab")) {
    return false;
  }

  std::string error;
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
