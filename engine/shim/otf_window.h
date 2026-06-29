// otf's top-level browser window — our own views::Widget hosting the UI
// WebContents (the React chrome), replacing content_shell's Shell window.
//
// The window is a single full-bleed views::WebView bound to the UI WebContents;
// otf draws all of its chrome inside that WebContents, and page tabs are layered
// on top as child aura windows by OtfTabHost (which parents into GetNativeWindow()).
//
// One window per process for now (OtfWindow::Get()). Created by
// OtfBrowserMainParts once the browser context + UI WebContents exist.

#ifndef OTF_ENGINE_SHIM_OTF_WINDOW_H_
#define OTF_ENGINE_SHIM_OTF_WINDOW_H_

#include <memory>

#include "base/functional/callback.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/widget/widget_observer.h"

namespace aura {
class Window;
}
namespace content {
class WebContents;
}
namespace views {
class Widget;
class WidgetDelegate;
}

namespace otf {

class OtfWindow : public views::WidgetObserver {
 public:
  // Builds and shows the top-level widget hosting `ui_contents` at `size`.
  // `on_closed` runs when the user closes the window (used to quit the run loop).
  OtfWindow(content::WebContents* ui_contents,
            const gfx::Size& size,
            base::OnceClosure on_closed);

  OtfWindow(const OtfWindow&) = delete;
  OtfWindow& operator=(const OtfWindow&) = delete;

  ~OtfWindow() override;

  // The single process window, or nullptr before it is created / after close.
  static OtfWindow* Get();

  // The top-level aura window OtfTabHost parents page tabs into.
  aura::Window* GetNativeWindow();

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  // CLIENT_OWNS_WIDGET: otf owns both the widget and its delegate. The widget
  // must be destroyed before the delegate.
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::OnceClosure on_closed_;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_WINDOW_H_
