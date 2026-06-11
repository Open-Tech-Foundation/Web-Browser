#include "otf_downloads_rpc.h"

#include <set>
#include <string>

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

bool RequireNoParams(const NativeRpcRequest& request, std::string* error) {
  return request.params && HasOnlyParamKeys(request.params, {}, error);
}

bool ReadDownloadId(CefRefPtr<CefDictionaryValue> params,
                    uint32_t* download_id,
                    std::string* error) {
  if (!params || !params->HasKey("id") || params->GetType("id") != VTYPE_INT) {
    if (error) *error = "id must be an integer";
    return false;
  }
  const int parsed = params->GetInt("id");
  if (parsed <= 0) {
    if (error) *error = "id must be positive";
    return false;
  }
  if (download_id) *download_id = static_cast<uint32_t>(parsed);
  return true;
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

bool HandleControl(OtfHandler* handler,
                   CefRefPtr<Callback> callback,
                   const NativeRpcRequest& request,
                   void (CefDownloadItemCallback::*operation)()) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"id"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  uint32_t download_id = 0;
  if (!ReadDownloadId(request.params, &download_id, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  auto it = handler->download_callbacks_.find(download_id);
  if (it != handler->download_callbacks_.end() && it->second) {
    (it->second.get()->*operation)();
  }
  NativeRpcSuccessString(callback, request, "ok");
  return true;
}

}  // namespace

bool HandleDownloadsRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler ||
      (request.method != "downloads.list" &&
       request.method != "downloads.subscribe" &&
       request.method != "downloads.cancel" &&
       request.method != "downloads.pause" &&
       request.method != "downloads.resume" &&
       request.method != "downloads.clearFinished")) {
    return false;
  }

  std::string error;
  if (request.method == "downloads.list") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    NativeRpcSuccessRaw(callback, request,
                        handler->guest_session_active_
                            ? "[]"
                            : handler->GetDownloadsJson());
    return true;
  }

  if (request.method == "downloads.subscribe") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!handler->guest_session_active_) {
      handler->downloads_subscription_ = callback;
    }
    NativeRpcSuccessRaw(callback, request,
                        JsonObjectBuilder()
                            .AddString("key", "downloads-update")
                            .AddRaw("downloads",
                                    handler->guest_session_active_
                                        ? "[]"
                                        : handler->GetDownloadsJson())
                            .Build());
    return true;
  }

  if (request.method == "downloads.cancel") {
    return HandleControl(handler, callback, request, &CefDownloadItemCallback::Cancel);
  }
  if (request.method == "downloads.pause") {
    return HandleControl(handler, callback, request, &CefDownloadItemCallback::Pause);
  }
  if (request.method == "downloads.resume") {
    return HandleControl(handler, callback, request, &CefDownloadItemCallback::Resume);
  }

  if (!RequireNoParams(request, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  if (!handler->guest_session_active_) {
    for (auto it = handler->downloads_.begin(); it != handler->downloads_.end();) {
      if (it->second.is_complete || it->second.is_canceled ||
          it->second.is_interrupted) {
        handler->download_callbacks_.erase(it->first);
        it = handler->downloads_.erase(it);
      } else {
        ++it;
      }
    }
    if (handler->store_) {
      handler->store_->DeleteFinishedDownloads();
    }
    handler->NotifyDownloadsChanged();
    handler->NotifyDownloadBadge();
  }
  NativeRpcSuccessString(callback, request, "ok");
  return true;
}

}  // namespace otf
