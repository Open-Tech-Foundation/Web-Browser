#include "otf/shim/otf_window.h"

#include <memory>
#include <utility>

#include "build/build_config.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace otf {

namespace {
OtfWindow* g_instance = nullptr;
}  // namespace

OtfWindow::OtfWindow(content::WebContents* ui_contents,
                     const gfx::Size& size,
                     base::OnceClosure on_closed)
    : on_closed_(std::move(on_closed)) {
  g_instance = this;

  // The whole window is the UI WebContents. No native toolbar — otf renders its
  // own chrome inside the WebContents.
  auto web_view =
      std::make_unique<views::WebView>(ui_contents->GetBrowserContext());
  web_view->SetWebContents(ui_contents);

  delegate_ = std::make_unique<views::WidgetDelegate>();
  delegate_->SetContentsView(std::move(web_view));
  delegate_->SetHasWindowSizeControls(true);
  delegate_->SetTitle(u"otf");

  // CLIENT_OWNS_WIDGET (the recommended model): otf owns the Widget and the
  // delegate; both are reset in the destructor. The global ViewsDelegate (set up
  // by ShellPlatformDelegate during Shell::Initialize) routes top-level widgets
  // to DesktopNativeWidgetAura.
  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(size);
  params.delegate = delegate_.get();
#if BUILDFLAG(IS_LINUX)
  params.wm_class_class = "otf-browser";
  params.wm_class_name = params.wm_class_class;
#endif
  widget_->Init(std::move(params));
  widget_->AddObserver(this);

  widget_->GetNativeWindow()->GetHost()->Show();
  widget_->Show();
  ui_contents->Focus();
}

OtfWindow::~OtfWindow() {
  if (g_instance == this) {
    g_instance = nullptr;
  }
  if (widget_) {
    widget_->RemoveObserver(this);
  }
  // Widget before delegate (CLIENT_OWNS_WIDGET teardown order).
  widget_.reset();
  delegate_.reset();
}

// static
OtfWindow* OtfWindow::Get() {
  return g_instance;
}

aura::Window* OtfWindow::GetNativeWindow() {
  return widget_ ? widget_->GetNativeWindow() : nullptr;
}

void OtfWindow::OnWidgetDestroying(views::Widget* widget) {
  // User-initiated close: stop new tabs from targeting a dead window and end the
  // run loop. The Widget object itself stays owned by us until ~OtfWindow.
  widget_->RemoveObserver(this);
  if (g_instance == this) {
    g_instance = nullptr;
  }
  if (on_closed_) {
    std::move(on_closed_).Run();
  }
}

}  // namespace otf
