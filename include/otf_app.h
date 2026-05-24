#ifndef OTF_BROWSER_APP_H_
#define OTF_BROWSER_APP_H_

#include <map>
#include <memory>
#include <string>

#include "include/cef_app.h"
#include "include/wrapper/cef_message_router.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "otf_browser_shell.h"
#include "otf_popup_overlay.h"
#include "otf_store.h"

namespace otf {

class OtfApp : public CefApp,
               public CefBrowserProcessHandler,
               public CefRenderProcessHandler {
 public:
  OtfApp();

  // CefApp methods:
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;
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
  // OnBrowserCreated receives the extra_info dictionary the main process
  // attached when calling CefBrowserView::CreateBrowserView — we use it to
  // ship the resolved screen profile across the process boundary so the
  // sandboxed renderer doesn't have to touch the filesystem.
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDictionaryValue> extra_info) override;
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

  // Register the browser:// scheme handler factory on a specific request
  // context. Must be called for every per-workspace context created after
  // startup because CefRegisterSchemeHandlerFactory only covers the global
  // context; new contexts don't inherit it.
  void RegisterBrowserSchemeForContext(CefRefPtr<CefRequestContext> ctx);

  // Build the extra_info dictionary to attach to every BrowserView we
  // create from the main process. Currently carries the resolved screen
  // profile JSON; can grow over time. Main-process-only.
  CefRefPtr<CefDictionaryValue> MakeBrowserExtraInfo() const;

  // Build a BrowserView configured for use inside a PopupOverlay (transparent
  // background, ALLOY runtime, the standard OtfViewDelegate with the given
  // height hint). Factored out so PopupOverlay doesn't need access to the
  // OtfViewDelegate type which is file-local to otf_app.cc.
  CefRefPtr<CefBrowserView> BuildOverlayBrowserView(const std::string& url,
                                                   int view_id,
                                                   int height_hint);

  // Generic popup registry. New popups should be authored against this API
  // instead of bespoke Create/Show/Hide methods.
  PopupOverlay* GetPopup(const std::string& name);
  // Iterate registered popups — used on tab switch to hide any visible
  // popups so they don't leak into the new tab's context.
  void HideAllPopups();
  // Build the CefBrowserView + overlay controller for each registered popup
  // and attach it to the window. Called once from OnWindowCreated.
  void CreateAllPopups(CefRefPtr<CefWindow> window);
  // Re-anchor every registered popup. Called from window OnLayoutChanged.
  void RepositionAllPopups();
  // Route an OnAfterCreated browser event to whichever popup owns the
  // matching view id. Returns true if handled.
  bool DispatchPopupBrowserCreated(int view_id, CefRefPtr<CefBrowser> browser);

  int CreateTab(const std::string& url, int parent_id = -1);
  int CreateRestoredTab(const WorkspaceTab& tab, int parent_id = -1);
  void SwitchTab(int tab_id);
  int CloseTab(int tab_id);
  int GetCurrentTabId() const { return current_tab_id_; }
  void CreateFindBarOverlay();
  void CreateZoomBarOverlay();
  void CreateDownloadsOverlay();
  void CreateCertificateOverlay();
  void CreateAppMenuOverlay();
  void CreateBookmarkOverlay();
  void CreateImagePreviewOverlay();
  void CreateLinkPreviewOverlay();
  void CreateConsoleOverlay();
  void PositionLinkPreviewOverlay();
  void SetLinkPreviewVisible(bool visible);
  void PositionConsoleOverlay();
  void ShowConsoleOverlay();
  void HideConsoleOverlay();
  void ToggleConsoleOverlay();
  void SetConsoleWidth(int w);
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
  void PositionBookmarkOverlay();
  void PositionImagePreviewOverlay();
  void ShowZoomBarOverlay();
  void HideZoomBarOverlay();
  void ShowDownloadsOverlay();
  void HideDownloadsOverlay();
  void ShowCertificateOverlay();
  void HideCertificateOverlay();
  void ShowAppMenuOverlay();
  void HideAppMenuOverlay();
  void ShowBookmarkOverlay();
  void HideBookmarkOverlay();
  void ShowImagePreviewOverlay();
  void HideImagePreviewOverlay();
  void RestoreImagePreviewStateForTab(int tab_id, const WorkspaceTab& tab);

  static OtfApp* GetInstance();
  void ToggleFullscreen();
  void SetContentFullscreen(bool fullscreen);

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<CefBrowserView> ui_view_;
  CefRefPtr<CefOverlayController> findbar_overlay_;
  CefRefPtr<CefOverlayController> zoombar_overlay_;
  CefRefPtr<CefOverlayController> downloads_overlay_;
  CefRefPtr<CefOverlayController> certificate_overlay_;
  CefRefPtr<CefOverlayController> appmenu_overlay_;
  CefRefPtr<CefOverlayController> bookmark_overlay_;
  CefRefPtr<CefOverlayController> image_preview_overlay_;
  CefRefPtr<CefOverlayController> link_preview_overlay_;

  // Console side panel — a proper CefBrowserView child of content_area_panel_,
  // not an overlay, so the tab content resizes when it is shown/hidden.
  CefRefPtr<CefPanel> content_area_panel_;
  CefRefPtr<CefBoxLayout> content_area_layout_;
  CefRefPtr<CefBrowserView> console_view_;

  static constexpr int kConsoleWidth = 420;

 private:
  void ApplyFullscreenState();
  bool fullscreen_ = false;

  TabManager tab_manager_;
  int current_tab_id_ = -1;
  std::string startup_behavior_ = "newtab";
  std::vector<std::string> startup_urls_;
  bool startup_tabs_opened_ = false;
  // Persisted tabs queued for restore once the window is up. Populated in
  // OnContextInitialized after the first tab has consumed its entry, and
  // drained by OpenPendingStartupTabs.
  std::vector<WorkspaceTab> pending_workspace_restore_;
  WorkspaceTab pending_workspace_restore_first_;
  bool pending_workspace_restore_first_is_preview_ = false;
  CefRefPtr<CefMessageRouterRendererSide> renderer_side_router_;
  std::map<std::string, std::unique_ptr<PopupOverlay>> popups_;
  int console_width_ = kConsoleWidth;
  void* console_delegate_ = nullptr; // OtfViewDelegate*, type is file-local to otf_app.cc

  // Resolved screen profile JSON.
  //
  // In the MAIN process this is populated once during OnContextInitialized
  // via otf::ResolveScreenProfileJson() (which reads ~/.otf-browser/ and
  // queries CefDisplay) and then attached to every CefBrowserView via
  // extra_info.
  //
  // In a RENDERER process this is populated by OnBrowserCreated from the
  // extra_info dictionary the main process sent, and read by
  // OnContextCreated when building the per-page policy script. The
  // renderer never resolves or persists anything — strictly read-only.
  std::string screen_profile_json_;

  IMPLEMENT_REFCOUNTING(OtfApp);
};

} // namespace otf

#endif // OTF_BROWSER_APP_H_
