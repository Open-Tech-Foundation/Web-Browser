#include "otf_app.h"

#include <string>
#include "otf_utils.h"
#include <unistd.h>
#include <libgen.h>

#include "include/cef_browser.h"
#include "include/internal/cef_types.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_helpers.h"
#include "include/cef_scheme.h"
#include "include/cef_parser.h"
#include "include/cef_urlrequest.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {

namespace {

OtfApp* g_app_instance = nullptr;

class OtfWindowDelegate : public CefWindowDelegate {
 public:
  OtfWindowDelegate(CefRefPtr<CefBrowserView> ui_view,
                    CefRefPtr<CefBrowserView> content_view,
                    cef_runtime_style_t runtime_style,
                    cef_show_state_t initial_show_state)
      : ui_view_(ui_view),
        content_view_(content_view),
        runtime_style_(runtime_style),
        initial_show_state_(initial_show_state) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    CefBoxLayoutSettings layout_settings;
    layout_settings.horizontal = false;
    layout_settings.between_child_spacing = 0;
    layout_settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
    layout_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
    CefRefPtr<CefBoxLayout> layout = window->SetToBoxLayout(layout_settings);
    OtfApp* app = OtfApp::GetInstance();
    app->window_ = window;

    if (ui_view_) {
      window->AddChildView(ui_view_);
      layout->SetFlexForView(ui_view_.get(), 0);
    }

    // Create a container for content tabs with FillLayout
    app->content_panel_ = CefPanel::CreatePanel(nullptr);
    app->content_panel_->SetToFillLayout();
    window->AddChildView(app->content_panel_);
    layout->SetFlexForView(app->content_panel_.get(), 1);

    window->Layout();

    app->CreateFindBarOverlay();
    app->CreateZoomBarOverlay();
    app->CreateDownloadsOverlay();
    app->CreateAppMenuOverlay();

    if (content_view_) {
      app->SwitchTab(content_view_->GetID());
    }

    if (initial_show_state_ != CEF_SHOW_STATE_HIDDEN) {
      window->CenterWindow(CefSize(1280, 800));
      window->Show();
    }
  }

  void OnLayoutChanged(CefRefPtr<CefView> view, const CefRect& new_bounds) override {
    OtfApp* app = OtfApp::GetInstance();
    if (app) {
      app->PositionFindBarOverlay();
      app->PositionZoomBarOverlay();
      app->PositionDownloadsOverlay();
      app->PositionAppMenuOverlay();
    }
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    ui_view_ = nullptr;
    content_view_ = nullptr;
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    CefRefPtr<CefBrowser> browser = ui_view_->GetBrowser();
    if (browser) {
      return browser->GetHost()->TryCloseBrowser();
    }
    return true;
  }

  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return runtime_style_;
  }

 private:
  CefRefPtr<CefBrowserView> ui_view_;
  CefRefPtr<CefBrowserView> content_view_;
  const cef_runtime_style_t runtime_style_;
  const cef_show_state_t initial_show_state_;

  IMPLEMENT_REFCOUNTING(OtfWindowDelegate);
};

class OtfViewDelegate : public CefBrowserViewDelegate {
 public:
  explicit OtfViewDelegate(cef_runtime_style_t runtime_style, int height = 0)
      : runtime_style_(runtime_style), height_(height) {}

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    if (height_ > 0) return CefSize(800, height_);
    return CefSize(800, 600);
  }

  CefSize GetMinimumSize(CefRefPtr<CefView> view) override {
    if (height_ > 0) return CefSize(800, height_);
    return CefSize(0, 0);
  }

  CefSize GetMaximumSize(CefRefPtr<CefView> view) override {
    if (height_ > 0) return CefSize(0, height_); // Max width 0 means unconstrained
    return CefSize(0, 0);
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return runtime_style_;
  }

  cef_color_t GetBackgroundColor(CefRefPtr<CefView> view) {
    return CefColorSetARGB(0, 0, 0, 0);
  }

 private:
  const cef_runtime_style_t runtime_style_;
  const int height_;

  IMPLEMENT_REFCOUNTING(OtfViewDelegate);
};

class BrowserSchemeHandlerFactory : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    std::string url = request->GetURL().ToString();
    std::string dev_ui_url = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");
    std::string html_content;

    std::string full_url = GetBrowserPageDevUrl(dev_ui_url, url);
    if (!full_url.empty()) {
      html_content =
          "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='0;url=" +
          full_url + "'></head><body>Redirecting...</body></html>";
    } else {
      std::string file_path = GetBrowserPageFilePath(url);
      FILE* f = fopen(file_path.c_str(), "rb");
      if (f) {
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<char> buffer(len);
        if (fread(buffer.data(), 1, len, f) == static_cast<size_t>(len)) {
          html_content = std::string(buffer.data(), len);
        }
        fclose(f);
      }
      if (html_content.empty()) {
        html_content =
            "<!DOCTYPE html><html><body><h1>404</h1><p>Page not found.</p></body></html>";
      }
    }

    CefRefPtr<CefStreamResourceHandler> handler(
        new CefStreamResourceHandler("text/html",
            CefStreamReader::CreateForData(const_cast<char*>(html_content.data()),
                                           html_content.length())));
    return handler;
  }

  IMPLEMENT_REFCOUNTING(BrowserSchemeHandlerFactory);
};

}  // namespace

OtfApp::OtfApp() {
  DCHECK(!g_app_instance);
  g_app_instance = this;
}

OtfApp* OtfApp::GetInstance() {
  return g_app_instance;
}

void OtfApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme("browser", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_CORS_ENABLED);
}

int OtfApp::CreateTab(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  
  CefBrowserSettings browser_settings;
  CefRefPtr<CefBrowserView> content_view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, browser_settings, nullptr, nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY));
  
  int tab_id = tab_manager_.AddTab(content_view);
  content_view->SetID(tab_id);
  tab_manager_.SetUrl(tab_id, url);

  if (content_panel_) {
    content_panel_->AddChildView(content_view);
    content_view->SetVisible(false);
  }

  return tab_id;
}

void OtfApp::SwitchTab(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  
  if (current_tab_id_ == tab_id) return;
  OtfHandler* handler = OtfHandler::GetInstance();

  // Clear any active find on the old tab so highlights don't linger
  if (findbar_overlay_ && findbar_overlay_->IsVisible()) {
    CefRefPtr<CefBrowser> old_browser = tab_manager_.GetBrowser(current_tab_id_);
    if (old_browser) old_browser->GetHost()->StopFinding(true);
  }

  CefRefPtr<CefBrowserView> old_view = tab_manager_.GetView(current_tab_id_);
  CefRefPtr<CefBrowserView> new_view = tab_manager_.GetView(tab_id);

  if (new_view && window_ && content_panel_) {
    if (!new_view->GetParentView()) {
      content_panel_->AddChildView(new_view);
    }
    if (old_view) old_view->SetVisible(false);
    new_view->SetVisible(true);
    new_view->RequestFocus();
    content_panel_->InvalidateLayout();
    window_->Layout();
    current_tab_id_ = tab_id;
    if (handler) {
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "active-tab-changed")
                             .AddInt("id", tab_id)
                             .Build());
      handler->NotifyBookmarkStateForTab(tab_id);
    }

    if (tab_manager_.IsFindVisible(tab_id)) {
      RestoreFindSessionForTab(tab_id, false);
      HideZoomBarOverlay();
    } else if (findbar_overlay_ && findbar_overlay_->IsVisible()) {
      findbar_overlay_->SetVisible(false);
      if (handler && handler->findbar_subscription_) {
        handler->findbar_subscription_->Success(
            JsonObjectBuilder()
                .AddString("key", "findbar-closed")
                .AddInt("tabId", tab_id)
                .Build());
        handler->findbar_subscription_->Success(
            JsonObjectBuilder()
                .AddString("key", "find-result")
                .AddInt("count", 0)
                .AddInt("active", 0)
                .AddInt("tabId", -1)
                .AddString("text", "")
                .AddBool("final", true)
                .Build());
      }
    }
    HideZoomBarOverlay();
    return;
  }
}

int OtfApp::CloseTab(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  std::vector<int> tab_ids = tab_manager_.GetAllTabIds();
  int next_active_tab_id = -1;
  if (tab_id == current_tab_id_) {
    next_active_tab_id = SelectNextActiveTabId(tab_ids, tab_id);
  }

  CefRefPtr<CefBrowserView> view = tab_manager_.GetView(tab_id);
  if (view && content_panel_) {
    content_panel_->RemoveChildView(view);
    content_panel_->Layout();
  }
  tab_manager_.RemoveTab(tab_id);
  if (current_tab_id_ == tab_id) {
    current_tab_id_ = -1;
  }

  if (next_active_tab_id >= 0) {
    SwitchTab(next_active_tab_id);
  }

  // Close the window if no tabs are left
  if (tab_manager_.GetAllTabIds().empty() && window_) {
    window_->Close();
  }
  return next_active_tab_id;
}

void OtfApp::CreateFindBarOverlay() {
  if (!window_) return;
  std::string url = "file://" + otf::GetExecutableDir() + "/ui/findbar.html";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/findbar.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, nullptr, nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 34));
  view->SetID(kFindBarBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  findbar_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionFindBarOverlay();
}

void OtfApp::CreateZoomBarOverlay() {
  if (!window_) return;
  std::string url = "file://" + otf::GetExecutableDir() + "/ui/zoombar.html";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/zoombar.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, nullptr, nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 40));
  view->SetID(kZoomBarBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  zoombar_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionZoomBarOverlay();
}

void OtfApp::CreateDownloadsOverlay() {
  if (!window_) return;
  std::string url = "file://" + otf::GetExecutableDir() + "/ui/downloadsbar.html";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/downloadsbar.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, nullptr, nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 380));
  view->SetID(kDownloadsBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  downloads_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionDownloadsOverlay();
}

void OtfApp::CreateAppMenuOverlay() {
  if (!window_) return;
  std::string url = "file://" + otf::GetExecutableDir() + "/ui/appmenu.html";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/appmenu.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, nullptr, nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 140));
  view->SetID(kAppMenuBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  appmenu_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionAppMenuOverlay();
}

void OtfApp::FocusCurrentTabContent() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser = tab_manager_.GetBrowser(current_tab_id_);
  if (browser) {
    browser->GetHost()->SetFocus(true);
  }
}

void OtfApp::RestoreFindSessionForTab(int tab_id, bool focus_findbar) {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !findbar_overlay_) return;

  PositionFindBarOverlay();
  findbar_overlay_->SetVisible(true);
  if (handler->findbar_subscription_) {
    handler->findbar_subscription_->Success(
        JsonObjectBuilder()
            .AddString("key", "find-restore")
            .AddInt("tabId", tab_id)
            .AddString("text", tab_manager_.GetFindText(tab_id))
            .AddBool("matchCase", tab_manager_.GetFindCase(tab_id))
            .Build());
  }

  if (focus_findbar && handler->findbar_browser_) {
    handler->findbar_browser_->GetMainFrame()->ExecuteJavaScript(
        "(function(){try{var e=document.querySelector('input[type=text]');if(e){e.focus();e.select();}}catch(_){}})();",
        CefString(), 0);
    handler->findbar_browser_->GetHost()->SetFocus(true);
  }

  std::string text = tab_manager_.GetFindText(tab_id);
  if (text.empty()) return;

  CefRefPtr<CefBrowser> browser = tab_manager_.GetBrowser(tab_id);
  if (!browser) return;

  handler->pending_find_tab_ = tab_id;
  handler->pending_find_text_ = text;
  handler->restore_find_target_ordinal_ = tab_manager_.GetFindActive(tab_id);
  handler->restore_find_in_progress_ =
      handler->restore_find_target_ordinal_ > 1;
  browser->GetHost()->Find(text, true, tab_manager_.GetFindCase(tab_id), false);
}

void OtfApp::PositionFindBarOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !findbar_overlay_) return;

  constexpr int kOverlayWidth = 380;
  constexpr int kOverlayHeight = 36;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayRightMargin = 20;

  CefRect bounds = window_->GetBounds();
  int x = std::max(0, bounds.width - kOverlayWidth - kOverlayRightMargin);
  findbar_overlay_->SetBounds(
      CefRect(x, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::PositionZoomBarOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !zoombar_overlay_) return;

  constexpr int kOverlayWidth = 146;
  constexpr int kOverlayHeight = 40;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayRightMargin = 54;

  CefRect bounds = window_->GetBounds();
  int x = std::max(0, bounds.width - kOverlayWidth - kOverlayRightMargin);
  zoombar_overlay_->SetBounds(
      CefRect(x, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::ShowZoomBarOverlay() {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !zoombar_overlay_) return;

  PositionZoomBarOverlay();
  zoombar_overlay_->SetVisible(true);
  if (handler->zoombar_subscription_) {
    handler->zoombar_subscription_->Success(
        JsonObjectBuilder()
            .AddString("key", "zoom-restore")
            .AddInt("tabId", current_tab_id_)
            .AddInt("zoomPercent", tab_manager_.GetZoomPercent(current_tab_id_))
            .Build());
  }
}

void OtfApp::HideZoomBarOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (zoombar_overlay_) {
    zoombar_overlay_->SetVisible(false);
  }
}

void OtfApp::PositionDownloadsOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !downloads_overlay_) return;

  constexpr int kOverlayWidth = 420;
  constexpr int kOverlayHeight = 360;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayRightMargin = 18;

  CefRect bounds = window_->GetBounds();
  int x = std::max(0, bounds.width - kOverlayWidth - kOverlayRightMargin);
  downloads_overlay_->SetBounds(
      CefRect(x, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::ShowDownloadsOverlay() {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !downloads_overlay_) return;

  PositionDownloadsOverlay();
  downloads_overlay_->SetVisible(true);
  if (handler->downloads_subscription_) {
    handler->downloads_subscription_->Success(
        JsonObjectBuilder().AddString("key", "downloads-refresh").Build());
  }
}

void OtfApp::HideDownloadsOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (downloads_overlay_) {
    downloads_overlay_->SetVisible(false);
  }
}

void OtfApp::PositionAppMenuOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !appmenu_overlay_) return;

  constexpr int kOverlayWidth = 300;
  constexpr int kOverlayHeight = 140;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayRightMargin = 16;

  CefRect bounds = window_->GetBounds();
  int x = std::max(0, bounds.width - kOverlayWidth - kOverlayRightMargin);
  appmenu_overlay_->SetBounds(
      CefRect(x, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::ShowAppMenuOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!appmenu_overlay_) return;

  PositionAppMenuOverlay();
  appmenu_overlay_->SetVisible(true);
}

void OtfApp::HideAppMenuOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (appmenu_overlay_) {
    appmenu_overlay_->SetVisible(false);
  }
}

void OtfApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  CefRegisterSchemeHandlerFactory("browser", "", new BrowserSchemeHandlerFactory());

  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();

  cef_runtime_style_t runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  
  CefRefPtr<OtfHandler> handler(new OtfHandler(true));
  handler->tab_manager_ = &tab_manager_;

  CefBrowserSettings browser_settings;
  
  // Dynamic UI Path: Loads from the executable's directory
  std::string ui_url = "file://" + otf::GetExecutableDir() + "/ui/index.html";
  std::string start_url = "browser://newtab";

  if (command_line->HasSwitch("dev-ui-url")) {
    ui_url = command_line->GetSwitchValue("dev-ui-url");
  }

  CefRefPtr<CefBrowserView> ui_view = CefBrowserView::CreateBrowserView(
      handler, ui_url, browser_settings, nullptr, nullptr,
      new OtfViewDelegate(runtime_style, 60));
  ui_view->SetID(kUiBrowserViewId);

  CefRefPtr<CefBrowserView> content_view = CefBrowserView::CreateBrowserView(
      handler, start_url, browser_settings, nullptr, nullptr,
      new OtfViewDelegate(runtime_style));
  
  int tab_id = tab_manager_.AddTab(content_view);
  content_view->SetID(tab_id);

  if (ui_view && content_view) {
    CefWindow::CreateTopLevelWindow(new OtfWindowDelegate(
        ui_view, content_view, runtime_style, CEF_SHOW_STATE_NORMAL));
  }
}

CefRefPtr<CefClient> OtfApp::GetDefaultClient() {
  return OtfHandler::GetInstance();
}

void OtfApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context) {
  if (!renderer_side_router_) {
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    renderer_side_router_ = CefMessageRouterRendererSide::Create(config);
  }
  renderer_side_router_->OnContextCreated(browser, frame, context);
}

void OtfApp::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefV8Context> context) {
  renderer_side_router_->OnContextReleased(browser, frame, context);
}

bool OtfApp::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  return renderer_side_router_->OnProcessMessageReceived(browser, frame,
                                                        source_process, message);
}

} // namespace otf
