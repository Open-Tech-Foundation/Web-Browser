#ifndef OTF_BROWSER_APP_H_
#define OTF_BROWSER_APP_H_

#include "include/cef_app.h"
#include "include/wrapper/cef_message_router.h"
#include "include/views/cef_window.h"
#include "otf_browser_shell.h"

namespace otf {

class OtfApp : public CefApp,
               public CefBrowserProcessHandler,
               public CefRenderProcessHandler {
 public:
  OtfApp();

  // CefApp methods:
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
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

  int CreateTab(const std::string& url);
  void SwitchTab(int tab_id);
  int CloseTab(int tab_id);
  int GetCurrentTabId() const { return current_tab_id_; }
  void CreateFindBarOverlay();
  void CreateZoomBarOverlay();
  void FocusCurrentTabContent();
  void RestoreFindSessionForTab(int tab_id, bool focus_findbar);
  void PositionFindBarOverlay();
  void PositionZoomBarOverlay();
  void ShowZoomBarOverlay();
  void HideZoomBarOverlay();

  static OtfApp* GetInstance();
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<CefOverlayController> findbar_overlay_;
  CefRefPtr<CefOverlayController> zoombar_overlay_;

 private:
  TabManager tab_manager_;
  int current_tab_id_ = -1;
  CefRefPtr<CefMessageRouterRendererSide> renderer_side_router_;

  IMPLEMENT_REFCOUNTING(OtfApp);
};

} // namespace otf

#endif // OTF_BROWSER_APP_H_
