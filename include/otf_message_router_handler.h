#ifndef OTF_MESSAGE_ROUTER_HANDLER_H_
#define OTF_MESSAGE_ROUTER_HANDLER_H_

#include "include/wrapper/cef_message_router.h"

namespace otf {

class OtfMessageRouterHandler
    : public CefMessageRouterBrowserSide::Handler {
 public:
  OtfMessageRouterHandler() = default;

  bool OnQuery(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      int64_t query_id,
      const CefString& request,
      bool persistent,
      CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback)
      override;
};

}  // namespace otf

#endif  // OTF_MESSAGE_ROUTER_HANDLER_H_
