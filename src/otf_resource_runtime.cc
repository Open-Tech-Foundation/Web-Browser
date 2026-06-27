#include "otf_resource_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_callback.h"
#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_task.h"
#include "include/internal/cef_time.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_store.h"
#include "otf_utils.h"

namespace otf {
namespace {

constexpr int64_t kFirstPartyCookieMaxAgeSeconds = 7 * 24 * 60 * 60;

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string HostFromOrigin(const std::string& origin) {
  CefURLParts parts;
  if (!CefParseURL(origin, parts)) return {};
  return ToLowerAscii(CefString(&parts.host).ToString());
}

bool IsSameSiteHost(const std::string& first_host,
                    const std::string& second_host) {
  if (first_host.empty() || second_host.empty()) return false;
  if (first_host == second_host) return true;
  const bool first_under_second =
      first_host.size() > second_host.size() &&
      first_host.compare(first_host.size() - second_host.size(),
                         second_host.size(), second_host) == 0 &&
      first_host[first_host.size() - second_host.size() - 1] == '.';
  const bool second_under_first =
      second_host.size() > first_host.size() &&
      second_host.compare(second_host.size() - first_host.size(),
                          first_host.size(), first_host) == 0 &&
      second_host[second_host.size() - first_host.size() - 1] == '.';
  return first_under_second || second_under_first;
}

int64_t CefBaseTimeToUnixSeconds(cef_basetime_t base_time) {
  cef_time_t cef_time{};
  time_t out = 0;
  if (cef_time_from_basetime(base_time, &cef_time) &&
      cef_time_to_timet(&cef_time, &out)) {
    return static_cast<int64_t>(out);
  }
  return 0;
}

cef_basetime_t UnixSecondsToCefBaseTime(int64_t unix_seconds) {
  cef_time_t cef_time{};
  cef_basetime_t out{};
  cef_time_from_timet(static_cast<time_t>(unix_seconds), &cef_time);
  cef_time_to_basetime(&cef_time, &out);
  return out;
}

std::string CookiePath(const CefCookie& cookie) {
  std::string path = CefString(&cookie.path).ToString();
  return path.empty() ? "/" : path;
}

std::string CookieUrl(const std::string& request_url, const CefCookie& cookie) {
  const std::string domain = CefString(&cookie.domain).ToString();
  if (domain.empty()) return request_url;
  const std::string scheme =
      request_url.rfind("https://", 0) == 0 || cookie.secure ? "https://" : "http://";
  std::string host = domain;
  while (!host.empty() && host.front() == '.') host.erase(host.begin());
  return scheme + host + CookiePath(cookie);
}

void RecordCookiePolicy(OtfStore* store,
                        const std::string& top_origin,
                        const std::string& cookie_origin,
                        const CefCookie& cookie,
                        const std::string& action,
                        const std::string& reason,
                        int64_t original_expires_at,
                        int64_t imposed_expires_at) {
  if (!store || top_origin.empty()) return;
  CookiePolicyRecord record;
  record.top_origin = top_origin;
  record.cookie_origin = cookie_origin;
  record.name = CefString(&cookie.name).ToString();
  record.domain = CefString(&cookie.domain).ToString();
  record.path = CookiePath(cookie);
  record.action = action;
  record.reason = reason;
  record.original_expires_at = original_expires_at;
  record.imposed_expires_at = imposed_expires_at;
  store->RecordCookiePolicyEvent(record);
}

void RecordNamedCookiePolicy(OtfStore* store,
                             const std::string& top_origin,
                             const std::string& cookie_origin,
                             const std::string& name,
                             const std::string& domain,
                             const std::string& path,
                             const std::string& action,
                             const std::string& reason) {
  if (!store || top_origin.empty() || name.empty()) return;
  CookiePolicyRecord record;
  record.top_origin = top_origin;
  record.cookie_origin = cookie_origin;
  record.name = name;
  record.domain = domain;
  record.path = path.empty() ? "/" : path;
  record.action = action;
  record.reason = reason;
  store->RecordCookiePolicyEvent(record);
}

void RecordThirdPartyIsolation(OtfStore* store,
                               const std::string& top_origin,
                               const std::string& cookie_origin) {
  if (!store || top_origin.empty() || cookie_origin.empty()) return;
  CookiePolicyRecord record;
  record.top_origin = top_origin;
  record.cookie_origin = cookie_origin;
  record.name = "(third-party resource)";
  record.path = "/";
  record.action = "third_party_isolated";
  record.reason = "third_party_cookie_access_blocked";
  store->RecordCookiePolicyEvent(record);
}

class FlushCookieStoreCallback : public CefSetCookieCallback {
 public:
  explicit FlushCookieStoreCallback(CefRefPtr<CefCookieManager> manager)
      : manager_(manager) {}

  void OnComplete(bool success) override {
    if (success && manager_) {
      manager_->FlushStore(nullptr);
    }
  }

 private:
  CefRefPtr<CefCookieManager> manager_;
  IMPLEMENT_REFCOUNTING(FlushCookieStoreCallback);
};

std::vector<std::string> CookieNamesFromHeader(const std::string& header) {
  std::vector<std::string> names;
  std::stringstream stream(header);
  std::string part;
  while (std::getline(stream, part, ';')) {
    const size_t eq = part.find('=');
    if (eq == std::string::npos) continue;
    std::string name = part.substr(0, eq);
    name.erase(name.begin(), std::find_if(name.begin(), name.end(), [](unsigned char c) {
      return !std::isspace(c);
    }));
    name.erase(std::find_if(name.rbegin(), name.rend(), [](unsigned char c) {
      return !std::isspace(c);
    }).base(), name.end());
    if (!name.empty()) names.push_back(name);
  }
  return names;
}

std::string TrimAscii(std::string value) {
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
    return !std::isspace(c);
  }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
    return !std::isspace(c);
  }).base(), value.end());
  return value;
}

std::string CookieNameFromSetCookie(const std::string& header) {
  const size_t eq = header.find('=');
  if (eq == std::string::npos) return {};
  return TrimAscii(header.substr(0, eq));
}

bool ParseSetCookieForRewrite(const std::string& header,
                              const std::string& request_url,
                              CefCookie* cookie,
                              int64_t* original_expires_at) {
  if (!cookie || !original_expires_at) return false;
  std::stringstream stream(header);
  std::string part;
  if (!std::getline(stream, part, ';')) return false;
  const size_t eq = part.find('=');
  if (eq == std::string::npos) return false;
  const std::string name = TrimAscii(part.substr(0, eq));
  const std::string value = TrimAscii(part.substr(eq + 1));
  if (name.empty()) return false;

  CefURLParts parts;
  if (!CefParseURL(request_url, parts)) return false;
  std::string domain = CefString(&parts.host).ToString();
  std::string path = "/";
  bool secure = false;
  bool http_only = false;
  cef_cookie_same_site_t same_site = CEF_COOKIE_SAME_SITE_UNSPECIFIED;
  *original_expires_at = 0;

  while (std::getline(stream, part, ';')) {
    part = TrimAscii(part);
    const size_t attr_eq = part.find('=');
    const std::string raw_key =
        attr_eq == std::string::npos ? part : part.substr(0, attr_eq);
    const std::string key = ToLowerAscii(TrimAscii(raw_key));
    const std::string attr_value =
        attr_eq == std::string::npos ? "" : TrimAscii(part.substr(attr_eq + 1));
    if (key == "domain" && !attr_value.empty()) {
      domain = attr_value;
    } else if (key == "path" && !attr_value.empty()) {
      path = attr_value;
    } else if (key == "max-age" && !attr_value.empty()) {
      char* end = nullptr;
      const long long max_age = std::strtoll(attr_value.c_str(), &end, 10);
      if (end && *end == '\0' && max_age > 0) {
        *original_expires_at = std::time(nullptr) + max_age;
      }
    } else if (key == "secure") {
      secure = true;
    } else if (key == "httponly") {
      http_only = true;
    } else if (key == "samesite") {
      const std::string same_site_value = ToLowerAscii(attr_value);
      if (same_site_value == "none") {
        same_site = CEF_COOKIE_SAME_SITE_NO_RESTRICTION;
      } else if (same_site_value == "lax") {
        same_site = CEF_COOKIE_SAME_SITE_LAX_MODE;
      } else if (same_site_value == "strict") {
        same_site = CEF_COOKIE_SAME_SITE_STRICT_MODE;
      }
    }
  }

  CefString(&cookie->name).FromString(name);
  CefString(&cookie->value).FromString(value);
  CefString(&cookie->domain).FromString(domain);
  CefString(&cookie->path).FromString(path.empty() ? "/" : path);
  cookie->secure = secure;
  cookie->httponly = http_only;
  cookie->same_site = same_site;
  return true;
}

void RecordSetCookiePolicy(OtfStore* store,
                           const std::string& top_origin,
                           const std::string& cookie_origin,
                           const std::string& request_url,
                           const std::string& header,
                           const std::string& action,
                           const std::string& reason) {
  CefCookie cookie;
  int64_t original_expires_at = 0;
  if (ParseSetCookieForRewrite(header, request_url, &cookie,
                               &original_expires_at)) {
    RecordCookiePolicy(store, top_origin, cookie_origin, cookie, action, reason,
                       original_expires_at, 0);
    return;
  }
  RecordNamedCookiePolicy(store, top_origin, cookie_origin,
                          CookieNameFromSetCookie(header), "", "/", action,
                          reason);
}

class CookieRewriteTask : public CefTask {
 public:
  CookieRewriteTask(CefRefPtr<CefRequestContext> context,
                    std::string request_url,
                    CefCookie cookie,
                    int64_t capped_expires_at,
                    OtfStore* store = nullptr,
                    std::string top_origin = "",
                    std::string cookie_origin = "",
                    std::string reason = "",
                    int64_t original_expires_at = 0)
      : context_(context),
        request_url_(std::move(request_url)),
        cookie_(cookie),
        capped_expires_at_(capped_expires_at),
        store_(store),
        top_origin_(std::move(top_origin)),
        cookie_origin_(std::move(cookie_origin)),
        reason_(std::move(reason)),
        original_expires_at_(original_expires_at) {}

  void Execute() override {
    if (!context_) return;
    CefRefPtr<CefCookieManager> manager = context_->GetCookieManager(nullptr);
    if (!manager) return;
    cookie_.has_expires = true;
    cookie_.expires = UnixSecondsToCefBaseTime(capped_expires_at_);
    manager->SetCookie(CookieUrl(request_url_, cookie_), cookie_,
                       new FlushCookieStoreCallback(manager));
    if (store_ && !top_origin_.empty()) {
      RecordCookiePolicy(store_, top_origin_, cookie_origin_, cookie_,
                         "capped_expiry", reason_,
                         original_expires_at_, capped_expires_at_);
    }
  }

 private:
  CefRefPtr<CefRequestContext> context_;
  std::string request_url_;
  CefCookie cookie_;
  int64_t capped_expires_at_ = 0;
  OtfStore* store_ = nullptr;
  std::string top_origin_;
  std::string cookie_origin_;
  std::string reason_;
  int64_t original_expires_at_ = 0;
  IMPLEMENT_REFCOUNTING(CookieRewriteTask);
};

class DeleteCookieTask : public CefTask {
 public:
  DeleteCookieTask(CefRefPtr<CefRequestContext> context,
                   std::string url,
                   std::string name)
      : context_(context), url_(std::move(url)), name_(std::move(name)) {}

  void Execute() override {
    if (!context_ || name_.empty()) return;
    CefRefPtr<CefCookieManager> manager = context_->GetCookieManager(nullptr);
    if (manager) manager->DeleteCookies(url_, name_, nullptr);
  }

 private:
  CefRefPtr<CefRequestContext> context_;
  std::string url_;
  std::string name_;
  IMPLEMENT_REFCOUNTING(DeleteCookieTask);
};

class CookieCapSweepVisitor : public CefCookieVisitor {
 public:
  CookieCapSweepVisitor(CefRefPtr<CefCookieManager> manager,
                        OtfStore* store,
                        std::string request_url,
                        std::string top_origin,
                        std::string cookie_origin)
      : manager_(manager),
        store_(store),
        request_url_(std::move(request_url)),
        top_origin_(std::move(top_origin)),
        cookie_origin_(std::move(cookie_origin)) {}

  bool Visit(const CefCookie& cookie,
             int count,
             int total,
             bool& delete_cookie) override {
    (void)count;
    (void)total;
    delete_cookie = false;
    if (!manager_) return true;

    const int64_t now = std::time(nullptr);
    const int64_t capped_expires_at = now + kFirstPartyCookieMaxAgeSeconds;
    const int64_t original_expires_at =
        cookie.has_expires ? CefBaseTimeToUnixSeconds(cookie.expires) : 0;
    if (original_expires_at > 0 && original_expires_at <= capped_expires_at) {
      return true;
    }

    CefCookie capped = cookie;
    capped.has_expires = true;
    capped.expires = UnixSecondsToCefBaseTime(capped_expires_at);
    manager_->SetCookie(CookieUrl(request_url_, capped), capped,
                        new FlushCookieStoreCallback(manager_));
    RecordCookiePolicy(
        store_, top_origin_, cookie_origin_, capped, "capped_expiry",
        original_expires_at > 0 ? "first_party_expiry_over_7_days"
                                : "first_party_session_cookie_7_day_cap",
        original_expires_at, capped_expires_at);
    return true;
  }

 private:
  CefRefPtr<CefCookieManager> manager_;
  OtfStore* store_ = nullptr;
  std::string request_url_;
  std::string top_origin_;
  std::string cookie_origin_;
  IMPLEMENT_REFCOUNTING(CookieCapSweepVisitor);
};

class CookieCapSweepTask : public CefTask {
 public:
  CookieCapSweepTask(CefRefPtr<CefRequestContext> context,
                     OtfStore* store,
                     std::string request_url,
                     std::string top_origin,
                     std::string cookie_origin)
      : context_(context),
        store_(store),
        request_url_(std::move(request_url)),
        top_origin_(std::move(top_origin)),
        cookie_origin_(std::move(cookie_origin)) {}

  void Execute() override {
    if (!context_ || request_url_.empty()) return;
    CefRefPtr<CefCookieManager> manager = context_->GetCookieManager(nullptr);
    if (!manager) return;
    manager->VisitUrlCookies(
        request_url_, true,
        new CookieCapSweepVisitor(manager, store_, request_url_, top_origin_,
                                  cookie_origin_));
  }

 private:
  CefRefPtr<CefRequestContext> context_;
  OtfStore* store_ = nullptr;
  std::string request_url_;
  std::string top_origin_;
  std::string cookie_origin_;
  IMPLEMENT_REFCOUNTING(CookieCapSweepTask);
};

bool IsNonTabBrowserViewId(int view_id) {
  return view_id == kUiBrowserViewId ||
         view_id == kFindBarBrowserViewId ||
         view_id == kZoomBarBrowserViewId ||
         view_id == kDownloadsBrowserViewId ||
         view_id == kAppMenuBrowserViewId ||
         view_id == kCertificateBrowserViewId ||
         view_id == kBookmarkBrowserViewId ||
         view_id == kImagePreviewBrowserViewId ||
         view_id == kDocPreviewBrowserViewId ||
         view_id == kClearSiteDataBrowserViewId ||
         view_id == kWorkspaceBrowserViewId ||
         view_id == kQrBrowserViewId ||
         view_id == kBlockedPopupBrowserViewId ||
         view_id == kDownloadRequestBrowserViewId ||
         view_id == kLinkPreviewBrowserViewId ||
         view_id == kToastNotificationBrowserViewId ||
         view_id == kConsoleBrowserViewId ||
         view_id == kSnipPreviewBrowserViewId ||
         view_id == kSplitMenuBrowserViewId;
}

class ImageBlockHandler : public CefResourceRequestHandler {
 public:
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override {
    return RV_CANCEL;
  }

 private:
  IMPLEMENT_REFCOUNTING(ImageBlockHandler);
};

class StrictCookieAccessFilter : public CefCookieAccessFilter {
 public:
  StrictCookieAccessFilter(OtfStore* store,
                           std::string page_origin,
                           std::string resource_origin,
                           bool third_party,
                           bool third_party_cookies_allowed)
      : store_(store),
        page_origin_(std::move(page_origin)),
        resource_origin_(std::move(resource_origin)),
        third_party_(third_party),
        third_party_cookies_allowed_(third_party_cookies_allowed) {}

  bool CanSendCookie(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     CefRefPtr<CefRequest> request,
                     const CefCookie& cookie) override {
    (void)browser;
    (void)frame;
    (void)request;
    if (!third_party_ || third_party_cookies_allowed_) return true;
    RecordCookiePolicy(store_, page_origin_, resource_origin_, cookie,
                       "blocked_send", "third_party_cookie", 0, 0);
    return false;
  }

  bool CanSaveCookie(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     CefRefPtr<CefRequest> request,
                     CefRefPtr<CefResponse> response,
                     const CefCookie& cookie) override {
    (void)frame;
    (void)response;
    if (third_party_) {
      if (third_party_cookies_allowed_) return true;
      RecordCookiePolicy(store_, page_origin_, resource_origin_, cookie,
                         "blocked_save", "third_party_cookie", 0, 0);
      return false;
    }

    const int64_t now = std::time(nullptr);
    const int64_t capped_expires_at = now + kFirstPartyCookieMaxAgeSeconds;
    const int64_t original_expires_at =
        cookie.has_expires ? CefBaseTimeToUnixSeconds(cookie.expires) : 0;
    if (original_expires_at > 0 && original_expires_at <= capped_expires_at) {
      return true;
    }

    RecordCookiePolicy(
        store_, page_origin_, resource_origin_, cookie, "capped_expiry",
        cookie.has_expires ? "first_party_expiry_over_7_days"
                           : "first_party_session_cookie_7_day_cap",
        original_expires_at, capped_expires_at);

    CefRefPtr<CefRequestContext> context =
        browser ? browser->GetHost()->GetRequestContext() : nullptr;
    if (context && request) {
    CefPostTask(TID_UI,
                  new CookieRewriteTask(context, request->GetURL().ToString(),
                                        cookie, capped_expires_at, store_,
                                        page_origin_, resource_origin_,
                                        cookie.has_expires
                                            ? "first_party_expiry_over_7_days"
                                            : "first_party_session_cookie_7_day_cap",
                                        original_expires_at));
    }
    return true;
  }

 private:
  OtfStore* store_;
  std::string page_origin_;
  std::string resource_origin_;
  bool third_party_ = false;
  bool third_party_cookies_allowed_ = false;
  IMPLEMENT_REFCOUNTING(StrictCookieAccessFilter);
};

class PrivacyResourceHandler : public CefResourceRequestHandler {
 public:
  PrivacyResourceHandler(CefRefPtr<CefResourceRequestHandler> inner,
                         OtfStore* store,
                         std::string page_origin,
                         std::string resource_origin,
                         bool third_party,
                         bool third_party_cookies_allowed)
      : inner_(inner),
        store_(store),
        page_origin_(std::move(page_origin)),
        resource_origin_(std::move(resource_origin)),
        third_party_(third_party),
        third_party_cookies_allowed_(third_party_cookies_allowed) {}

  CefRefPtr<CefCookieAccessFilter> GetCookieAccessFilter(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request) override {
    return new StrictCookieAccessFilter(store_, page_origin_, resource_origin_,
                                        third_party_,
                                        third_party_cookies_allowed_);
  }

  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override {
    if (third_party_ && !third_party_cookies_allowed_ && request) {
      CefRequest::HeaderMap headers;
      request->GetHeaderMap(headers);
      CefRequest::HeaderMap filtered_headers;
      for (const auto& [key, value] : headers) {
        if (ToLowerAscii(key.ToString()) == "cookie") {
          for (const auto& name : CookieNamesFromHeader(value.ToString())) {
            RecordNamedCookiePolicy(store_, page_origin_, resource_origin_, name,
                                    "", "/", "blocked_send",
                                    "third_party_cookie");
          }
        } else {
          filtered_headers.insert(std::make_pair(key, value));
        }
      }
      const std::string cookie_header = request->GetHeaderByName("Cookie").ToString();
      if (!cookie_header.empty()) {
        for (const auto& name : CookieNamesFromHeader(cookie_header)) {
          RecordNamedCookiePolicy(store_, page_origin_, resource_origin_, name,
                                  "", "/", "blocked_send",
                                  "third_party_cookie");
        }
      }
      request->SetHeaderMap(filtered_headers);
      request->SetHeaderByName("Cookie", "", true);
    }
    if (inner_) {
      return inner_->OnBeforeResourceLoad(browser, frame, request, callback);
    }
    return RV_CONTINUE;
  }

  bool OnResourceResponse(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefRequest> request,
                          CefRefPtr<CefResponse> response) override {
    (void)frame;
    if (third_party_ && !third_party_cookies_allowed_ && browser && request &&
        response) {
      CefResponse::HeaderMap headers;
      response->GetHeaderMap(headers);
      CefRefPtr<CefRequestContext> context =
          browser->GetHost()->GetRequestContext();
      for (const auto& [key, value] : headers) {
        if (ToLowerAscii(key.ToString()) != "set-cookie") continue;
        const std::string header = value.ToString();
        const std::string name = CookieNameFromSetCookie(header);
        if (name.empty()) continue;
        RecordSetCookiePolicy(store_, page_origin_, resource_origin_,
                              request->GetURL().ToString(), header,
                              "blocked_save", "third_party_cookie");
        CefPostDelayedTask(
            TID_UI,
            new DeleteCookieTask(context, request->GetURL().ToString(), name),
            500);
      }
    } else if (!third_party_ && browser && request && response) {
      CefResponse::HeaderMap headers;
      response->GetHeaderMap(headers);
      CefRefPtr<CefRequestContext> context =
          browser->GetHost()->GetRequestContext();
      CefPostDelayedTask(
          TID_UI,
          new CookieCapSweepTask(context, store_, request->GetURL().ToString(),
                                 page_origin_, resource_origin_),
          1000);
      const int64_t now = std::time(nullptr);
      const int64_t capped_expires_at = now + kFirstPartyCookieMaxAgeSeconds;
      for (const auto& [key, value] : headers) {
        if (ToLowerAscii(key.ToString()) != "set-cookie") continue;
        CefCookie cookie;
        int64_t original_expires_at = 0;
        if (!ParseSetCookieForRewrite(value.ToString(), request->GetURL().ToString(),
                                      &cookie, &original_expires_at)) {
          continue;
        }
        if (original_expires_at > 0 && original_expires_at <= capped_expires_at) {
          continue;
        }
        const std::string reason =
            original_expires_at > 0 ? "first_party_expiry_over_7_days"
                                    : "first_party_session_cookie_7_day_cap";
        CefPostDelayedTask(
            TID_UI,
            new CookieRewriteTask(context, request->GetURL().ToString(), cookie,
                                  capped_expires_at, store_, page_origin_,
                                  resource_origin_, reason, original_expires_at),
            500);
      }
    }
    return false;
  }

 private:
  CefRefPtr<CefResourceRequestHandler> inner_;
  OtfStore* store_;
  std::string page_origin_;
  std::string resource_origin_;
  bool third_party_ = false;
  bool third_party_cookies_allowed_ = false;
  IMPLEMENT_REFCOUNTING(PrivacyResourceHandler);
};

}  // namespace

CefRefPtr<CefBrowser> OtfHandler::ResolveSiteDataBrowser(
    CefRefPtr<CefBrowser> requester) {
  CEF_REQUIRE_UI_THREAD();
  int id = tab_manager_ ? tab_manager_->GetId(requester) : -1;
  if (id < 0) {
    OtfApp* app = OtfApp::GetInstance();
    id = app ? app->GetCurrentTabId() : -1;
  }
  CefRefPtr<CefBrowser> resolved =
      (id >= 0 && tab_manager_) ? tab_manager_->GetBrowser(id) : nullptr;
  return resolved ? resolved : requester;
}

CefRefPtr<CefResourceRequestHandler>
OtfHandler::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  (void)frame;
  (void)is_navigation;
  (void)is_download;
  (void)disable_default_handling;

  const CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  const int tab_id =
      (view && !IsNonTabBrowserViewId(view->GetID())) ? view->GetID() : -1;
  CefRefPtr<CefResourceRequestHandler> inner;
  std::string page_origin;
  std::string resource_origin;
  bool third_party = false;
  bool third_party_cookies_allowed = false;

  if (store_) {
    const std::string resource_url = request->GetURL().ToString();
    resource_origin = ExtractOrigin(resource_url);
    page_origin = ExtractOrigin(request->GetFirstPartyForCookies().ToString());
    if (!page_origin.empty() && !resource_origin.empty() &&
        !IsSameSiteHost(HostFromOrigin(page_origin),
                        HostFromOrigin(resource_origin))) {
      third_party = true;
      RecordThirdPartyIsolation(store_.get(), page_origin, resource_origin);
      std::lock_guard<std::mutex> lock(cross_origin_mutex_);
      cross_origin_resources_[page_origin].insert(resource_origin);
    }
  }

  if (store_ && page_origin.empty() && !request_initiator.empty()) {
    const std::string raw = request_initiator.ToString();
    if (raw.rfind("browser://", 0) != 0 &&
        raw.rfind("file://", 0) != 0) {
      page_origin = ExtractOrigin(raw);
      if (!page_origin.empty()) {
        if (request->GetResourceType() != RT_MAIN_FRAME &&
            page_origin == resource_origin && browser) {
          CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
          const std::string main_origin =
              main_frame ? ExtractOrigin(main_frame->GetURL().ToString()) : "";
          if (!main_origin.empty()) page_origin = main_origin;
        }
        if (!resource_origin.empty() &&
            !IsSameSiteHost(HostFromOrigin(page_origin),
                            HostFromOrigin(resource_origin))) {
          third_party = true;
          RecordThirdPartyIsolation(store_.get(), page_origin, resource_origin);
          std::lock_guard<std::mutex> lock(cross_origin_mutex_);
          cross_origin_resources_[page_origin].insert(resource_origin);
        }

        if (!IsGuestTab(tab_id) && request->GetResourceType() == RT_IMAGE &&
            store_->GetSitePermission(page_origin, "images") == "block") {
          inner = new ImageBlockHandler;
        }
      }
    }
  } else if (store_ && page_origin.empty() &&
             request->GetResourceType() != RT_MAIN_FRAME && browser) {
    CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
    if (main_frame) {
      page_origin = ExtractOrigin(main_frame->GetURL().ToString());
    }
    if (!page_origin.empty() && !resource_origin.empty() &&
        !IsSameSiteHost(HostFromOrigin(page_origin),
                        HostFromOrigin(resource_origin))) {
      third_party = true;
      RecordThirdPartyIsolation(store_.get(), page_origin, resource_origin);
      std::lock_guard<std::mutex> lock(cross_origin_mutex_);
      cross_origin_resources_[page_origin].insert(resource_origin);
    }
  } else if (store_ && page_origin.empty() && frame && !frame->IsMain()) {
    CefRefPtr<CefFrame> parent = frame->GetParent();
    if (parent) {
      page_origin = ExtractOrigin(parent->GetURL().ToString());
    }
    if (!page_origin.empty() && !resource_origin.empty() &&
        !IsSameSiteHost(HostFromOrigin(page_origin),
                        HostFromOrigin(resource_origin))) {
      third_party = true;
      RecordThirdPartyIsolation(store_.get(), page_origin, resource_origin);
      std::lock_guard<std::mutex> lock(cross_origin_mutex_);
      cross_origin_resources_[page_origin].insert(resource_origin);
    }
  } else if (store_ && page_origin.empty() && is_navigation &&
             !resource_origin.empty()) {
    page_origin = resource_origin;
  }

  if (store_ && !page_origin.empty() && !resource_origin.empty()) {
    if (third_party && !IsGuestTab(tab_id)) {
      third_party_cookies_allowed =
          store_->GetSitePermission(resource_origin, "thirdPartyCookies") ==
              "allow" ||
          store_->GetSitePermission(page_origin, "thirdPartyCookies") ==
              "allow";
    }
    return new PrivacyResourceHandler(inner, store_.get(), page_origin,
                                      resource_origin, third_party,
                                      third_party_cookies_allowed);
  }

  return inner;
}

}  // namespace otf
