#ifndef OTF_BROWSER_APP_H_
#define OTF_BROWSER_APP_H_

#include "include/cef_app.h"
#include "include/wrapper/cef_message_router.h"
#include "otf_browser_shell.h"

namespace otf {

class OtfApp : public CefApp,
               public CefBrowserProcessHandler,
               public CefRenderProcessHandler {
 public:
  OtfApp();

  // CefApp methods:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  // CefBrowserProcessHandler methods:
  void OnContextInitialized() override;
  CefRefPtr<CefClient> GetDefaultClient() override;

  // CefRenderProcessHandler methods:
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  TabManager tab_manager_;
  CefRefPtr<CefMessageRouterRendererSide> renderer_side_router_;

  IMPLEMENT_REFCOUNTING(OtfApp);
};

} // namespace otf

#endif // OTF_BROWSER_APP_H_
