#ifndef OTF_COOKIE_TRACKING_RPC_H_
#define OTF_COOKIE_TRACKING_RPC_H_

#include "include/wrapper/cef_message_router.h"
#include "otf_native_rpc.h"

namespace otf {

class OtfHandler;

bool HandleCookieTrackingRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request);

}  // namespace otf

#endif  // OTF_COOKIE_TRACKING_RPC_H_
