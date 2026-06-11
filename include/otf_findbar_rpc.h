#ifndef OTF_FINDBAR_RPC_H_
#define OTF_FINDBAR_RPC_H_

#include "include/cef_browser.h"
#include "include/wrapper/cef_message_router.h"
#include "otf_native_rpc.h"

namespace otf {

class OtfHandler;

bool HandleFindbarRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request);

}  // namespace otf

#endif  // OTF_FINDBAR_RPC_H_
