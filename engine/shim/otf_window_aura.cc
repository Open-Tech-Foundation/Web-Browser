// Desktop-aura (Linux/Windows/ChromeOS) implementation of OtfPlatformWindow.
//
// Builds otf's top-level views::Widget hosting the UI WebContents, and parents
// page-tab views as child aura windows layered over the content region. This is
// the only translation unit that touches aura/views; macOS (Cocoa) and Android
// (surface) backends provide their own OtfPlatformWindow::Create/Get instead.

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "content/public/browser/web_contents.h"
#include "otf/shim/otf_platform_window.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"

namespace otf {

namespace {

// Height of otf's own chrome (rendered inside the UI WebContents) reserved at the
// top of the window; page tabs fill the area beneath it.
constexpr int kChromeHeight = 65;

OtfPlatformWindow* g_instance = nullptr;

class OtfWindowAura : public OtfPlatformWindow, public views::WidgetObserver {
 public:
  OtfWindowAura(content::WebContents* ui_contents,
                const gfx::Size& size,
                base::OnceClosure on_closed)
      : on_closed_(std::move(on_closed)) {
    g_instance = this;

    // The whole window is the UI WebContents. No native toolbar — otf renders
    // its own chrome inside the WebContents.
    auto web_view =
        std::make_unique<views::WebView>(ui_contents->GetBrowserContext());
    web_view->SetWebContents(ui_contents);

    delegate_ = std::make_unique<views::WidgetDelegate>();
    delegate_->SetContentsView(std::move(web_view));
    delegate_->SetHasWindowSizeControls(true);
    delegate_->SetTitle(u"otf");

    // CLIENT_OWNS_WIDGET: otf owns the Widget and delegate (reset widget before
    // delegate). The global ViewsDelegate (set up during Shell::Initialize)
    // routes top-level widgets to DesktopNativeWidgetAura.
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

  OtfWindowAura(const OtfWindowAura&) = delete;
  OtfWindowAura& operator=(const OtfWindowAura&) = delete;

  ~OtfWindowAura() override {
    if (g_instance == this) {
      g_instance = nullptr;
    }
    if (widget_) {
      widget_->RemoveObserver(this);
    }
    widget_.reset();
    delegate_.reset();
  }

  // OtfPlatformWindow:
  void ShowTab(content::WebContents* tab) override {
    shown_tab_ = tab;
    aura::Window* host = HostWindow();
    aura::Window* view = tab ? tab->GetNativeView() : nullptr;
    if (host && view) {
      if (view->parent() != host) {
        host->AddChild(view);
      }
      view->SetBounds(ContentBounds());
      view->Show();
      host->StackChildAtTop(view);
    }
    if (tab) {
      tab->WasShown();
    }
  }

  void HideTab(content::WebContents* tab) override {
    if (!tab) {
      return;
    }
    if (shown_tab_ == tab) {
      shown_tab_ = nullptr;
    }
    if (aura::Window* view = tab->GetNativeView()) {
      view->Hide();
    }
    tab->WasHidden();
  }

  void SetContentBounds(const gfx::Rect& bounds) override {
    content_bounds_ = bounds.IsEmpty() ? std::optional<gfx::Rect>()
                                       : std::optional<gfx::Rect>(bounds);
    // Reflow the currently shown tab into the new region.
    if (shown_tab_) {
      if (aura::Window* view = shown_tab_->GetNativeView()) {
        view->SetBounds(ContentBounds());
      }
    }
  }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override {
    // User-initiated close: stop new tabs from targeting a dead window and end
    // the run loop. The Widget object stays owned by us until destruction.
    widget_->RemoveObserver(this);
    if (g_instance == this) {
      g_instance = nullptr;
    }
    if (on_closed_) {
      std::move(on_closed_).Run();
    }
  }

 private:
  aura::Window* HostWindow() {
    return widget_ ? widget_->GetNativeWindow() : nullptr;
  }

  gfx::Rect ContentBounds() {
    if (content_bounds_) {
      return *content_bounds_;
    }
    aura::Window* host = HostWindow();
    if (!host) {
      return gfx::Rect();
    }
    const gfx::Size size = host->bounds().size();
    return gfx::Rect(0, kChromeHeight, size.width(),
                     std::max(0, size.height() - kChromeHeight));
  }

  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::OnceClosure on_closed_;
  raw_ptr<content::WebContents> shown_tab_ = nullptr;
  std::optional<gfx::Rect> content_bounds_;
};

}  // namespace

// static
std::unique_ptr<OtfPlatformWindow> OtfPlatformWindow::Create(
    content::WebContents* ui_contents,
    const gfx::Size& size,
    base::OnceClosure on_closed) {
  return std::make_unique<OtfWindowAura>(ui_contents, size,
                                         std::move(on_closed));
}

// static
OtfPlatformWindow* OtfPlatformWindow::Get() {
  return g_instance;
}

}  // namespace otf
