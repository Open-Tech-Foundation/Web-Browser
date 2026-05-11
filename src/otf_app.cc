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

    if (content_view_) {
      app->SwitchTab(content_view_->GetID());
    }

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

class BrowserSchemeHandlerFactory : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    std::string url = request->GetURL().ToString();
    std::string dev_ui_url = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");
    std::string html_content;

    if (!dev_ui_url.empty()) {
      std::string path = url.substr(10);
      if (!path.empty() && path.back() == '/') path.pop_back();
      std::string full_url = dev_ui_url + "/" + path + ".html";
      html_content = "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='0;url=" + full_url + "'></head><body>Redirecting...</body></html>";
    } else {
      std::string file_path = otf::GetExecutableDir() + "/ui/" + url.substr(10) + ".html";
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
        html_content = "<!DOCTYPE html><html><body>Page not found: " + url + "</body></html>";
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

  return tab_id;
}

void OtfApp::SwitchTab(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  
  if (current_tab_id_ == tab_id) return;
 
  CefRefPtr<CefBrowserView> old_view = tab_manager_.GetView(current_tab_id_);
  CefRefPtr<CefBrowserView> new_view = tab_manager_.GetView(tab_id);

  if (new_view && window_ && content_panel_) {
    if (!new_view->GetParentView()) {
      content_panel_->AddChildView(new_view);
    }
    if (old_view) old_view->SetVisible(false);
    new_view->SetVisible(true);
    content_panel_->InvalidateLayout();
    window_->Layout();
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

  // Close the window if no tabs are left
  if (tab_manager_.GetAllTabIds().empty() && window_) {
    window_->Close();
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
