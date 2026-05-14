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
  void CreateDownloadsOverlay();
  void CreateCertificateOverlay();
  void CreateAppMenuOverlay();
  void FocusCurrentTabContent();
  void OpenPendingStartupTabs();
  void RestoreFindSessionForTab(int tab_id, bool focus_findbar);
  void PositionFindBarOverlay();
  void PositionZoomBarOverlay();
  void PositionDownloadsOverlay();
  void PositionCertificateOverlay();
  void RefreshCertificateOverlay();
  void DestroyCertificateOverlay();
  void PositionAppMenuOverlay();
  void ShowZoomBarOverlay();
  void HideZoomBarOverlay();
  void ShowDownloadsOverlay();
  void HideDownloadsOverlay();
  void ShowCertificateOverlay();
  void HideCertificateOverlay();
  void ShowAppMenuOverlay();
  void HideAppMenuOverlay();

  static OtfApp* GetInstance();
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<CefOverlayController> findbar_overlay_;
  CefRefPtr<CefOverlayController> zoombar_overlay_;
  CefRefPtr<CefOverlayController> downloads_overlay_;
  CefRefPtr<CefOverlayController> certificate_overlay_;
  CefRefPtr<CefOverlayController> appmenu_overlay_;

 private:
  TabManager tab_manager_;
  int current_tab_id_ = -1;
  std::string startup_behavior_ = "newtab";
  std::vector<std::string> startup_urls_;
  bool startup_tabs_opened_ = false;
  CefRefPtr<CefMessageRouterRendererSide> renderer_side_router_;

  IMPLEMENT_REFCOUNTING(OtfApp);
};

} // namespace otf

#endif // OTF_BROWSER_APP_H_
