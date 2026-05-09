#include "otf_app.h"

#include <string>
#include <unistd.h>
#include <libgen.h>

#include "include/cef_browser.h"
#include "include/internal/cef_types.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_handler.h"

namespace otf {

namespace {

OtfApp* g_app_instance = nullptr;

// Helper to get the executable's directory
std::string GetExecutableDir() {
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  if (count != -1) {
    return std::string(dirname(result));
  }
  return "";
}

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

    if (content_view_) {
      app->content_panel_->AddChildView(content_view_);
      content_view_->SetVisible(true);
    }

    window->Layout();

    if (initial_show_state_ != CEF_SHOW_STATE_HIDDEN) {
      window->CenterWindow(CefSize(1280, 800));
      window->Show();
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

 private:
  const cef_runtime_style_t runtime_style_;
  const int height_;

  IMPLEMENT_REFCOUNTING(OtfViewDelegate);
};

}  // namespace

OtfApp::OtfApp() {
  DCHECK(!g_app_instance);
  g_app_instance = this;
}

OtfApp* OtfApp::GetInstance() {
  return g_app_instance;
}

int OtfApp::CreateTab(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  
  CefBrowserSettings browser_settings;
  CefRefPtr<CefBrowserView> content_view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, browser_settings, nullptr, nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY));
  
  int tab_id = tab_manager_.AddTab(content_view);
  content_view->SetID(tab_id);
  
  if (content_panel_) {
    content_panel_->AddChildView(content_view);
    content_view->SetVisible(false); // Hidden by default
  }
  
  return tab_id;
}

void OtfApp::SwitchTab(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  
  if (current_tab_id_ == tab_id) return;

  CefRefPtr<CefBrowserView> old_view = tab_manager_.GetView(current_tab_id_);
  CefRefPtr<CefBrowserView> new_view = tab_manager_.GetView(tab_id);

  if (new_view && window_ && content_panel_) {
    if (old_view) old_view->SetVisible(false);
    new_view->SetVisible(true);
    content_panel_->Layout();
    current_tab_id_ = tab_id;
  }
}

void OtfApp::CloseTab(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowserView> view = tab_manager_.GetView(tab_id);
  if (view && content_panel_) {
    content_panel_->RemoveChildView(view);
    content_panel_->Layout();
  }
  tab_manager_.RemoveTab(tab_id);
}

void OtfApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();

  cef_runtime_style_t runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  
  CefRefPtr<OtfHandler> handler(new OtfHandler(true));
  handler->tab_manager_ = &tab_manager_;

  CefBrowserSettings browser_settings;
  
  // Dynamic UI Path: Loads from the executable's directory
  std::string ui_url = "file://" + GetExecutableDir() + "/ui/index.html";

  // Development Override: Allow pointing to a local dev server (e.g., Vite/Bun) for HMR
  if (command_line->HasSwitch("dev-ui-url")) {
    ui_url = command_line->GetSwitchValue("dev-ui-url");
  }

  CefRefPtr<CefBrowserView> ui_view = CefBrowserView::CreateBrowserView(
      handler, ui_url, browser_settings, nullptr, nullptr,
      new OtfViewDelegate(runtime_style, 60));
  ui_view->SetID(100);

  CefRefPtr<CefBrowserView> content_view = CefBrowserView::CreateBrowserView(
      handler, "https://www.google.com", browser_settings, nullptr, nullptr,
      new OtfViewDelegate(runtime_style));
  
  int tab_id = tab_manager_.AddTab(content_view);
  content_view->SetID(tab_id);
  current_tab_id_ = tab_id;

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
