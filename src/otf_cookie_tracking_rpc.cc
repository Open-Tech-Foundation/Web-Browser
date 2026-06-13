#include "otf_cookie_tracking_rpc.h"

#include <set>
#include <string>

#include "include/cef_cookie.h"
#include "include/internal/cef_time.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {
namespace {

using Callback = CefMessageRouterBrowserSide::Handler::Callback;

bool HasOnlyParamKeys(CefRefPtr<CefDictionaryValue> params,
                      const std::set<std::string>& allowed,
                      std::string* error) {
  if (!params) {
    if (error) *error = "params must be an object";
    return false;
  }
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

int64_t CefBaseTimeToUnixSeconds(cef_basetime_t base_time) {
  cef_time_t cef_time{};
  time_t out = 0;
  if (cef_time_from_basetime(base_time, &cef_time) &&
      cef_time_to_timet(&cef_time, &out)) {
    return static_cast<int64_t>(out);
  }
  return 0;
}

std::string CookiePathForTracking(const CefCookie& cookie) {
  std::string path = CefString(&cookie.path).ToString();
  return path.empty() ? "/" : path;
}

class CookieInspectVisitor : public CefCookieVisitor {
 public:
  CookieInspectVisitor(CefRefPtr<Callback> callback, NativeRpcRequest request)
      : callback_(callback), request_(std::move(request)) {}

  ~CookieInspectVisitor() override {
    NativeRpcSuccessRaw(callback_, request_, "[" + json_ + "]");
  }

  bool Visit(const CefCookie& cookie,
             int count,
             int total,
             bool& delete_cookie) override {
    delete_cookie = false;
    if (!json_.empty()) json_ += ",";
    json_ += JsonObjectBuilder()
                 .AddString("name", CefString(&cookie.name).ToString())
                 .AddString("domain", CefString(&cookie.domain).ToString())
                 .AddString("path", CookiePathForTracking(cookie))
                 .AddBool("secure", cookie.secure != 0)
                 .AddBool("httpOnly", cookie.httponly != 0)
                 .AddRaw("createdAt",
                         std::to_string(
                             CefBaseTimeToUnixSeconds(cookie.creation)))
                 .AddRaw("lastAccessAt",
                         std::to_string(
                             CefBaseTimeToUnixSeconds(cookie.last_access)))
                 .Build();
    return true;
  }

 private:
  CefRefPtr<Callback> callback_;
  NativeRpcRequest request_;
  std::string json_;

  IMPLEMENT_REFCOUNTING(CookieInspectVisitor);
};

bool HandleList(OtfHandler* handler,
                CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
                const NativeRpcRequest& request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {}, &error)) {
    NativeRpcFailure(callback, request, "invalid_params", error);
    return true;
  }

  CefRefPtr<CefBrowser> context_browser;
  if (OtfApp* app = OtfApp::GetInstance()) {
    const int current_id = app->GetCurrentTabId();
    if (handler->tab_manager_ && current_id >= 0) {
      context_browser = handler->tab_manager_->GetBrowser(current_id);
    }
  }
  if (!context_browser) {
    NativeRpcFailure(callback, request, "no_active_tab", "no active tab");
    return true;
  }

  CefRefPtr<CefCookieManager> manager =
      context_browser->GetHost()->GetRequestContext()->GetCookieManager(nullptr);
  if (!manager) {
    NativeRpcSuccessRaw(callback, request, "[]");
    return true;
  }

  manager->VisitAllCookies(new CookieInspectVisitor(callback, request));
  return true;
}

}  // namespace

bool HandleCookieTrackingRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  if (!handler || !callback || request.method != "cookieTracking.list") {
    return false;
  }
  return HandleList(handler, callback, request);
}

}  // namespace otf
