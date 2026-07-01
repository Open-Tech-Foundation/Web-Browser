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
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "content/public/browser/web_contents.h"
#include "otf/shim/otf_platform_window.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/buildflags.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/wm/core/wm_state.h"

#if BUILDFLAG(ENABLE_DESKTOP_AURA)
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#endif

namespace otf {

namespace {

// Height of otf's own chrome (rendered inside the UI WebContents) reserved at the
// top of the window; page tabs fill the area beneath it.
constexpr int kChromeHeight = 65;

OtfPlatformWindow* g_instance = nullptr;

// Minimal non-test ViewsDelegate: routes top-level widgets to
// DesktopNativeWidgetAura (and child widgets to NativeWidgetAura) on desktop
// aura, which is what content_shell's DesktopTestViewsDelegate does for tests.
class OtfViewsDelegate : public views::ViewsDelegate {
 public:
  OtfViewsDelegate() = default;
  OtfViewsDelegate(const OtfViewsDelegate&) = delete;
  OtfViewsDelegate& operator=(const OtfViewsDelegate&) = delete;
  ~OtfViewsDelegate() override = default;

  void OnBeforeWidgetInit(
      views::Widget::InitParams* params,
      views::internal::NativeWidgetDelegate* delegate) override {
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
    if (params->native_widget) {
      return;
    }
    if (params->parent &&
        params->type != views::Widget::InitParams::TYPE_MENU &&
        params->type != views::Widget::InitParams::TYPE_TOOLTIP) {
      params->native_widget = new views::NativeWidgetAura(delegate);
    } else {
      params->native_widget = new views::DesktopNativeWidgetAura(delegate);
    }
#endif
  }
};

// Process-global views environment (window manager state, screen, ViewsDelegate),
// created once before the first widget. Replaces the setup content_shell did in
// ShellPlatformDelegate::Initialize.
struct ViewsEnv {
  ViewsEnv() {
    if (!display::Screen::HasScreen()) {
      screen = views::CreateDesktopScreen();
    }
  }
  wm::WMState wm_state;
  std::unique_ptr<display::Screen> screen;
  OtfViewsDelegate views_delegate;  // registers itself as the global on ctor
};

void EnsureViewsEnv() {
  static base::NoDestructor<ViewsEnv> env;
  (void)env;
}

class OtfWindowAura : public OtfPlatformWindow, public views::WidgetObserver {
 public:
  OtfWindowAura(content::WebContents* ui_contents,
                const gfx::Size& size,
                base::OnceClosure on_closed)
      : on_closed_(std::move(on_closed)) {
    g_instance = this;

    // Bring up the views environment (ViewsDelegate/wm/screen) before the first
    // widget. content_shell did this in ShellPlatformDelegate::Initialize.
    EnsureViewsEnv();

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
    ReflowShownTab();
  }

  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget* /*widget*/,
                             const gfx::Rect& /*new_bounds*/) override {
    // The UI WebContents is the Widget's contents view and reflows via views
    // layout automatically; the page tab is a child aura window layered over the
    // content region, so it must be reflowed by hand on every window resize.
    ReflowShownTab();
  }

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

  // Resize the currently shown page tab's child view into the content region.
  // No-op when no tab is shown (e.g. a model-only tab that never navigated).
  void ReflowShownTab() {
    if (!shown_tab_) {
      return;
    }
    if (aura::Window* view = shown_tab_->GetNativeView()) {
      view->SetBounds(ContentBounds());
    }
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

// static
void OtfPlatformWindow::InitToolkit() {
  EnsureViewsEnv();
}

}  // namespace otf
