#include "otf_permissions_rpc.h"

#include <set>
#include <string>

#include "otf_app.h"
#include "otf_handler.h"
#include "otf_popup_overlay.h"
#include "otf_store.h"
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

bool ReadIntParam(CefRefPtr<CefDictionaryValue> params,
                  const std::string& key,
                  int* value,
                  std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_INT) {
    if (error) *error = key + " must be an integer";
    return false;
  }
  if (value) *value = params->GetInt(key);
  return true;
}

bool ReadOriginParam(CefRefPtr<CefDictionaryValue> params,
                     std::string* origin,
                     std::string* error) {
  if (!params || !params->HasKey("origin") ||
      params->GetType("origin") != VTYPE_STRING) {
    if (error) *error = "origin must be a string";
    return false;
  }
  const std::string normalized = NormalizeOrigin(
      params->GetString("origin").ToString());
  if (normalized.empty()) {
    if (error) *error = "origin must not be empty";
    return false;
  }
  if (origin) *origin = normalized;
  return true;
}

void HidePopup(const std::string& name) {
  if (OtfApp* app = OtfApp::GetInstance()) {
    if (auto* popup = app->GetPopup(name)) {
      popup->Hide();
    }
  }
}

void StartPendingDownload(OtfHandler* handler) {
  if (!handler || handler->download_ask_pending_url_.empty() ||
      !handler->download_ask_pending_browser_) {
    return;
  }
  handler->download_ask_pending_browser_->GetHost()->StartDownload(
      handler->download_ask_pending_url_);
  handler->download_ask_pending_url_.clear();
  handler->download_ask_pending_browser_ = nullptr;
}

void AllowPendingPopup(OtfHandler* handler, int popup_id) {
  if (!handler) return;
  auto it = handler->pending_popups_.find(popup_id);
  if (it == handler->pending_popups_.end()) {
    return;
  }
  handler->OpenAcceptedPopup(it->second);
  HidePopup("blockedpopup");
  handler->pending_popups_.erase(it);
}

void Success(CefRefPtr<Callback> callback, const NativeRpcRequest& request) {
  NativeRpcSuccessRaw(callback, request, "null");
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

}  // namespace

bool HandlePermissionsRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler ||
      (request.method != "permissions.popup.allow" &&
       request.method != "permissions.popup.alwaysAllow" &&
       request.method != "permissions.download.allow" &&
       request.method != "permissions.download.alwaysAllow")) {
    return false;
  }

  std::string error;
  if (request.method == "permissions.popup.allow") {
    if (!request.params || !HasOnlyParamKeys(request.params, {"id"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    int popup_id = -1;
    if (!ReadIntParam(request.params, "id", &popup_id, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    AllowPendingPopup(handler, popup_id);
    Success(callback, request);
    return true;
  }

  if (request.method == "permissions.popup.alwaysAllow") {
    if (!request.params ||
        !HasOnlyParamKeys(request.params, {"id", "origin"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    int popup_id = -1;
    std::string origin;
    if (!ReadIntParam(request.params, "id", &popup_id, &error) ||
        !ReadOriginParam(request.params, &origin, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!handler->IsGuestSessionActive() && handler->GetStore()) {
      handler->GetStore()->SetSitePermission(origin, "popup", "allow");
    }
    AllowPendingPopup(handler, popup_id);
    Success(callback, request);
    return true;
  }

  if (!request.params ||
      !HasOnlyParamKeys(request.params, {"origin"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  std::string origin;
  if (!ReadOriginParam(request.params, &origin, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  if (request.method == "permissions.download.allow") {
    handler->allow_once_downloads_.insert(origin);
    HidePopup("downloadrequest");
    StartPendingDownload(handler);
    Success(callback, request);
    return true;
  }

  if (!handler->IsGuestSessionActive() && handler->GetStore()) {
    handler->GetStore()->SetSitePermission(origin, "downloads", "allow");
  }
  HidePopup("downloadrequest");
  StartPendingDownload(handler);
  Success(callback, request);
  return true;
}

}  // namespace otf
