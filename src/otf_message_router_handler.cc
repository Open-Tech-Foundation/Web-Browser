#include "otf_message_router_handler.h"

#include "include/cef_command_line.h"
#include "otf_handler.h"
#include "otf_native_rpc.h"
#include "otf_rpc_dispatcher.h"
#include "otf_utils.h"

namespace otf {

bool OtfMessageRouterHandler::OnQuery(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    int64_t query_id,
    const CefString& request,
    bool persistent,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback) {
  // The cefQuery bridge exposes privileged operations. Only internal UI
  // surfaces are trusted to call it; web content is denied outright.
  const std::string frame_url = frame ? frame->GetURL().ToString() : "";
  bool trusted_frame = IsInternalBrowserUiUrl(frame_url);
  if (!trusted_frame) {
    CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
    if (cmd && cmd->HasSwitch("dev-ui-url")) {
      const std::string dev_ui_url =
          cmd->GetSwitchValue("dev-ui-url").ToString();
      if (!dev_ui_url.empty() &&
          (frame_url == dev_ui_url || frame_url == dev_ui_url + "/" ||
           frame_url.rfind(dev_ui_url + "/", 0) == 0)) {
        trusted_frame = true;
      }
    }
  }
  if (!trusted_frame) {
    callback->Failure(1, "denied");
    return true;
  }

  constexpr size_t kMaxRequestBytes = 64 * 1024;
  if (request.size() > kMaxRequestBytes) {
    callback->Failure(1, "request too large");
    return true;
  }

  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !handler->tab_manager_) return false;
  const std::string msg = request.ToString();
  const size_t first_non_space = msg.find_first_not_of(" \t\r\n");
  if (first_non_space == std::string::npos || msg[first_non_space] != '{') {
    return false;
  }

  NativeRpcRequest rpc_request;
  std::string parse_error;
  if (!ParseNativeRpcRequest(msg, &rpc_request, &parse_error)) {
    callback->Failure(1, parse_error);
    return true;
  }
  if (DispatchNativeRpc(handler, browser, callback, rpc_request)) {
    return true;
  }
  NativeRpcFailure(callback, rpc_request, "unknown_method",
                   "Unknown native RPC method");
  return true;
}

}  // namespace otf
