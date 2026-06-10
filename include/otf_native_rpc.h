#ifndef OTF_NATIVE_RPC_H_
#define OTF_NATIVE_RPC_H_

#include <string>

#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace otf {

struct NativeRpcRequest {
  std::string id;
  std::string method;
  CefRefPtr<CefDictionaryValue> params;
};

bool ParseNativeRpcRequest(const std::string& wire,
                           NativeRpcRequest* out,
                           std::string* error);

void NativeRpcSuccessRaw(
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request,
    const std::string& result_json);

void NativeRpcSuccessString(
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request,
    const std::string& result);

void NativeRpcFailure(
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request,
    const std::string& code,
    const std::string& message);

}  // namespace otf

#endif  // OTF_NATIVE_RPC_H_
