#include "otf_site_data_rpc.h"

#include <set>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/internal/cef_time.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
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

bool RequiredStringParam(CefRefPtr<CefDictionaryValue> params,
                         const std::string& key,
                         std::string* out,
                         std::string* error) {
  if (!params->HasKey(key) || params->GetType(key) != VTYPE_STRING) {
    if (error) *error = key + " must be a string";
    return false;
  }
  *out = params->GetString(key).ToString();
  if (out->empty()) {
    if (error) *error = key + " must not be empty";
    return false;
  }
  return true;
}

std::string NormalizeSiteDataOrigin(const std::string& raw) {
  const std::string extracted = otf::ExtractOrigin(raw);
  if (extracted.empty()) return {};
  if (extracted.rfind("http://", 0) != 0 &&
      extracted.rfind("https://", 0) != 0) {
    return {};
  }
  std::string normalized_raw = raw;
  if (normalized_raw.size() > 1 && normalized_raw.back() == '/') {
    normalized_raw.pop_back();
  }
  if (normalized_raw != extracted) return {};
  return extracted;
}

bool RequiredOriginParam(const NativeRpcRequest& request,
                         std::string* origin,
                         std::string* error) {
  std::string raw;
  if (!HasOnlyParamKeys(request.params, {"origin"}, error) ||
      !RequiredStringParam(request.params, "origin", &raw, error)) {
    return false;
  }
  *origin = NormalizeSiteDataOrigin(raw);
  if (origin->empty()) {
    if (error) *error = "origin must be a canonical http(s) origin";
    return false;
  }
  return true;
}

bool IsValidPermissionSetting(const std::string& permission,
                              const std::string& setting) {
  if (permission == "popup" || permission == "downloads") {
    return setting == "ask" || setting == "allow" || setting == "block";
  }
  if (permission == "autoPictureInPicture") {
    return setting == "allow" || setting == "block";
  }
  if (permission == "images" || permission == "javascript") {
    return setting == "allow" || setting == "block";
  }
  return false;
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

void Fail(CefRefPtr<Callback> callback,
          const NativeRpcRequest& request,
          const std::string& code,
          const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

void SucceedOk(CefRefPtr<Callback> callback, const NativeRpcRequest& request) {
  NativeRpcSuccessString(callback, request, "ok");
}

bool HandleShowClearPopup(OtfHandler* handler,
                          CefRefPtr<Callback> callback,
                          const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  handler->pending_cleardata_origin_ = origin;
  OtfApp* app = OtfApp::GetInstance();
  otf::PopupOverlay* popup = app ? app->GetPopup("cleardata") : nullptr;
  if (popup) {
    std::string* pending = &handler->pending_cleardata_origin_;
    popup->SetRestoreProducer([pending]() {
      return JsonObjectBuilder().AddString("origin", *pending).Build();
    });
    popup->Show();
  }
  SucceedOk(callback, request);
  return true;
}

bool HandleOpenPage(OtfHandler* handler,
                    CefRefPtr<CefBrowser> browser,
                    CefRefPtr<Callback> callback,
                    const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  OtfApp* app = OtfApp::GetInstance();
  if (app) {
    if (otf::PopupOverlay* popup = app->GetPopup("cleardata")) popup->Hide();
    std::string encoded;
    for (char c : origin) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
          c == '.' || c == '_' || c == '~') {
        encoded += c;
      } else {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%%%02X",
                      static_cast<unsigned char>(c));
        encoded += buf;
      }
    }
    const int active_id = app->GetCurrentTabId();
    const bool from_private =
        active_id >= 0 && handler->tab_manager_ &&
        handler->tab_manager_->IsPrivate(active_id);
    int id = app->CreateTab("browser://sitedata?origin=" + encoded, -1,
                            from_private);
    handler->NotifyNewTab(id, handler->tab_manager_->GetId(browser));
    app->SwitchTab(id);
  }
  SucceedOk(callback, request);
  return true;
}

bool HandleGetStorage(OtfHandler* handler,
                      CefRefPtr<CefBrowser> browser,
                      CefRefPtr<Callback> callback,
                      const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  if (!handler->devtools_bridge_) {
    Fail(callback, request, "devtools_unavailable", "devtools bridge not attached");
    return true;
  }
  handler->devtools_bridge_->Attach(handler->ResolveSiteDataBrowser(browser));
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("origin", origin);
  const int message_id = handler->devtools_bridge_->Execute(
      "Storage.getUsageAndQuota", params,
      [callback, request](bool ok, const std::string& result_json) {
        if (ok) NativeRpcSuccessRaw(callback, request, result_json);
        else Fail(callback, request, "devtools_failed", result_json);
      });
  if (message_id == 0) {
    Fail(callback, request, "devtools_submit_failed", "Storage usage request failed");
  }
  return true;
}

class CookieListVisitor : public CefCookieVisitor {
 public:
  CookieListVisitor(CefRefPtr<Callback> callback, NativeRpcRequest request)
      : callback_(callback), request_(std::move(request)) {}
  bool Visit(const CefCookie& cookie, int count, int total,
             bool& delete_cookie) override {
    if (resolved_) return false;
    delete_cookie = false;
    if (!rows_.empty()) rows_ += ",";
    rows_ += JsonObjectBuilder()
                 .AddString("name", CefString(&cookie.name).ToString())
                 .AddString("value", CefString(&cookie.value).ToString())
                 .AddString("domain", CefString(&cookie.domain).ToString())
                 .AddString("path", CefString(&cookie.path).ToString())
                 .AddBool("secure", cookie.secure != 0)
                 .AddBool("httpOnly", cookie.httponly != 0)
                 .AddRaw("createdAt",
                         std::to_string(CefBaseTimeToUnixSeconds(cookie.creation)))
                 .AddRaw("lastAccessAt",
                         std::to_string(CefBaseTimeToUnixSeconds(cookie.last_access)))
                 .AddRaw("expiresAt",
                         std::to_string(cookie.has_expires
                                            ? CefBaseTimeToUnixSeconds(cookie.expires)
                                            : 0))
                 .Build();
    if (count + 1 >= total) Resolve();
    return true;
  }
  void Resolve() {
    if (resolved_) return;
    resolved_ = true;
    NativeRpcSuccessRaw(callback_, request_, "[" + rows_ + "]");
  }
  void ResolveWatchdog() {
    if (resolved_) return;
    resolved_ = true;
    if (rows_.empty()) {
      NativeRpcSuccessRaw(callback_, request_, "[]");
    } else {
      Fail(callback_, request_, "cookie_timeout", "Cookie listing timed out");
    }
  }
 private:
  CefRefPtr<Callback> callback_;
  NativeRpcRequest request_;
  bool resolved_ = false;
  std::string rows_;
  IMPLEMENT_REFCOUNTING(CookieListVisitor);
};

class CookieListFallbackTask : public CefTask {
 public:
  explicit CookieListFallbackTask(CefRefPtr<CookieListVisitor> visitor)
      : visitor_(visitor) {}
  void Execute() override { visitor_->ResolveWatchdog(); }
 private:
  CefRefPtr<CookieListVisitor> visitor_;
  IMPLEMENT_REFCOUNTING(CookieListFallbackTask);
};

bool HandleGetCookies(OtfHandler* handler,
                      CefRefPtr<CefBrowser> browser,
                      CefRefPtr<Callback> callback,
                      const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  CefRefPtr<CookieListVisitor> visitor =
      new CookieListVisitor(callback, request);
  CefRefPtr<CefBrowser> ctx_browser = handler->ResolveSiteDataBrowser(browser);
  CefRefPtr<CefCookieManager> mgr =
      ctx_browser ? ctx_browser->GetHost()->GetRequestContext()->GetCookieManager(nullptr)
                  : CefCookieManager::GetGlobalManager(nullptr);
  if (mgr) {
    mgr->VisitUrlCookies(origin, true, visitor);
  } else {
    visitor->Resolve();
  }
  CefPostDelayedTask(TID_UI, new CookieListFallbackTask(visitor), 2000);
  return true;
}

class CookiePurgeVisitor : public CefCookieVisitor {
 public:
  CookiePurgeVisitor(CefRefPtr<Callback> callback, NativeRpcRequest request)
      : callback_(callback), request_(std::move(request)) {}
  bool Visit(const CefCookie& cookie, int count, int total,
             bool& delete_cookie) override {
    delete_cookie = true;
    ++deleted_;
    if (count + 1 >= total) Resolve();
    return true;
  }
  void Resolve() {
    if (resolved_) return;
    resolved_ = true;
    NativeRpcSuccessRaw(callback_, request_, std::to_string(deleted_));
  }
  void ResolveWatchdog() {
    if (resolved_) return;
    resolved_ = true;
    if (deleted_ == 0) {
      NativeRpcSuccessRaw(callback_, request_, "0");
    } else {
      Fail(callback_, request_, "cookie_timeout", "Cookie clear timed out");
    }
  }
 private:
  CefRefPtr<Callback> callback_;
  NativeRpcRequest request_;
  int deleted_ = 0;
  bool resolved_ = false;
  IMPLEMENT_REFCOUNTING(CookiePurgeVisitor);
};

class CookiePurgeFallbackTask : public CefTask {
 public:
  explicit CookiePurgeFallbackTask(CefRefPtr<CookiePurgeVisitor> visitor)
      : visitor_(visitor) {}
  void Execute() override { visitor_->ResolveWatchdog(); }
 private:
  CefRefPtr<CookiePurgeVisitor> visitor_;
  IMPLEMENT_REFCOUNTING(CookiePurgeFallbackTask);
};

bool HandleClearCookies(OtfHandler* handler,
                        CefRefPtr<CefBrowser> browser,
                        CefRefPtr<Callback> callback,
                        const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  CefRefPtr<CookiePurgeVisitor> visitor =
      new CookiePurgeVisitor(callback, request);
  CefRefPtr<CefBrowser> ctx_browser = handler->ResolveSiteDataBrowser(browser);
  CefRefPtr<CefCookieManager> mgr =
      ctx_browser ? ctx_browser->GetHost()->GetRequestContext()->GetCookieManager(nullptr)
                  : CefCookieManager::GetGlobalManager(nullptr);
  if (mgr) {
    mgr->VisitUrlCookies(origin, true, visitor);
  } else {
    visitor->Resolve();
  }
  if (handler->store_) {
    handler->store_->ClearCookiePolicyRecords(origin);
  }
  CefPostDelayedTask(TID_UI, new CookiePurgeFallbackTask(visitor), 2000);
  return true;
}

bool HandleClearStorage(OtfHandler* handler,
                        CefRefPtr<CefBrowser> browser,
                        CefRefPtr<Callback> callback,
                        const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  CefRefPtr<CefBrowser> ctx_browser = handler->ResolveSiteDataBrowser(browser);
  if (!ctx_browser || !handler->devtools_bridge_) {
    Fail(callback, request, "devtools_unavailable", "devtools bridge not attached");
    return true;
  }
  handler->devtools_bridge_->Attach(ctx_browser);
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("origin", origin);
  params->SetString(
      "storageTypes",
      "appcache,file_systems,indexeddb,local_storage,"
      "shader_cache,websql,service_workers,cache_storage");
  const int message_id = handler->devtools_bridge_->Execute(
      "Storage.clearDataForOrigin", params,
      [callback, request](bool ok, const std::string& result_json) {
        if (ok) SucceedOk(callback, request);
        else Fail(callback, request, "devtools_failed", result_json);
      });
  if (message_id == 0) {
    Fail(callback, request, "devtools_submit_failed", "Storage clear request failed");
  }
  return true;
}

bool HandleClearPermissions(OtfHandler* handler,
                            CefRefPtr<CefBrowser> browser,
                            CefRefPtr<Callback> callback,
                            const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  if (handler->store_ && !handler->store_->ClearSitePermissions(origin)) {
    Fail(callback, request, "store_failed", "Failed to clear stored site permissions");
    return true;
  }
  CefRefPtr<CefBrowser> ctx_browser = handler->ResolveSiteDataBrowser(browser);
  if (!ctx_browser || !handler->devtools_bridge_) {
    SucceedOk(callback, request);
    return true;
  }
  handler->devtools_bridge_->Attach(ctx_browser);
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("origin", origin);
  const int message_id = handler->devtools_bridge_->Execute(
      "Browser.resetPermissions", params,
      [callback, request](bool ok, const std::string& result_json) {
        if (ok) SucceedOk(callback, request);
        else Fail(callback, request, "devtools_failed", result_json);
      });
  if (message_id == 0) {
    Fail(callback, request, "devtools_submit_failed", "Permission reset request failed");
  }
  return true;
}

bool HandleGetCrossOrigins(OtfHandler* handler,
                           CefRefPtr<Callback> callback,
                           const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  std::lock_guard<std::mutex> lock(handler->cross_origin_mutex_);
  auto it = handler->cross_origin_resources_.find(origin);
  if (it == handler->cross_origin_resources_.end()) {
    NativeRpcSuccessRaw(callback, request, "[]");
    return true;
  }
  std::string json = "[";
  bool first = true;
  for (const auto& res : it->second) {
    if (!first) json += ",";
    first = false;
    json += JsonString(res);
  }
  json += "]";
  NativeRpcSuccessRaw(callback, request, json);
  return true;
}

bool HandleGetPermissions(OtfHandler* handler,
                          CefRefPtr<Callback> callback,
                          const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  if (handler->store_ && !handler->IsGuestSessionActive()) {
    NativeRpcSuccessRaw(callback, request,
                        handler->store_->GetSitePermissionsJson(origin));
  } else {
    NativeRpcSuccessRaw(callback, request, "{}");
  }
  return true;
}

bool HandleGetCookiePolicy(OtfHandler* handler,
                           CefRefPtr<Callback> callback,
                           const NativeRpcRequest& request) {
  std::string origin;
  std::string error;
  if (!RequiredOriginParam(request, &origin, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  if (!handler->store_) {
    NativeRpcSuccessRaw(callback, request, "[]");
    return true;
  }
  NativeRpcSuccessRaw(callback, request,
                      handler->store_->GetCookiePolicyJson(origin));
  return true;
}

bool HandleSetPermission(OtfHandler* handler,
                         CefRefPtr<Callback> callback,
                         const NativeRpcRequest& request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"origin", "permission", "setting"}, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  std::string raw_origin;
  std::string permission;
  std::string setting;
  if (!RequiredStringParam(request.params, "origin", &raw_origin, &error) ||
      !RequiredStringParam(request.params, "permission", &permission, &error) ||
      !RequiredStringParam(request.params, "setting", &setting, &error)) {
    Fail(callback, request, "invalid_params", error);
    return true;
  }
  const std::string origin = NormalizeSiteDataOrigin(raw_origin);
  if (origin.empty() || !IsValidPermissionSetting(permission, setting)) {
    Fail(callback, request, "invalid_params", "invalid permission setting");
    return true;
  }
  if (handler->IsGuestSessionActive()) {
    Fail(callback, request, "guest_session", "Site permissions are disabled in guest sessions");
    return true;
  }
  if (handler->store_ &&
      !handler->store_->SetSitePermission(origin, permission, setting)) {
    Fail(callback, request, "store_failed", "Failed to save site permission");
    return true;
  }
  SucceedOk(callback, request);
  return true;
}

}  // namespace

bool HandleSiteDataRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  if (!handler) return false;
  if (request.method == "siteData.showClearPopup") {
    return HandleShowClearPopup(handler, callback, request);
  }
  if (request.method == "siteData.openPage") {
    return HandleOpenPage(handler, browser, callback, request);
  }
  if (request.method == "siteData.getStorage") {
    return HandleGetStorage(handler, browser, callback, request);
  }
  if (request.method == "siteData.getCookies") {
    return HandleGetCookies(handler, browser, callback, request);
  }
  if (request.method == "siteData.clearCookies") {
    return HandleClearCookies(handler, browser, callback, request);
  }
  if (request.method == "siteData.clearStorage") {
    return HandleClearStorage(handler, browser, callback, request);
  }
  if (request.method == "siteData.clearPermissions") {
    return HandleClearPermissions(handler, browser, callback, request);
  }
  if (request.method == "siteData.getCrossOriginResources") {
    return HandleGetCrossOrigins(handler, callback, request);
  }
  if (request.method == "siteData.getPermissions") {
    return HandleGetPermissions(handler, callback, request);
  }
  if (request.method == "siteData.getCookiePolicy") {
    return HandleGetCookiePolicy(handler, callback, request);
  }
  if (request.method == "siteData.setPermission") {
    return HandleSetPermission(handler, callback, request);
  }
  return false;
}

}  // namespace otf
