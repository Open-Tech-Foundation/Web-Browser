// Cross-OS seam for the top-level browser window.
//
// OtfPlatformWindow is the *only* interface the rest of the shim (OtfBrowserMainParts,
// OtfTabHost) uses to talk to the OS window: it hosts the UI WebContents and the
// page-tab views. All toolkit-specific code (aura/views on desktop; Cocoa on mac;
// an Android surface later) lives behind a concrete implementation selected per
// platform in BUILD.gn — no aura/views/Cocoa types leak past this header.
//
// The desktop-aura implementation is otf_window_aura.cc. A new platform is added
// by providing another translation unit that defines OtfPlatformWindow::Create()
// and OtfPlatformWindow::Get() and compiling it instead.

#ifndef OTF_ENGINE_SHIM_OTF_PLATFORM_WINDOW_H_
#define OTF_ENGINE_SHIM_OTF_PLATFORM_WINDOW_H_

#include <memory>

#include "base/functional/callback.h"

namespace content {
class WebContents;
}
namespace gfx {
class Rect;
class Size;
}

namespace otf {

class OtfPlatformWindow {
 public:
  // Builds and shows the top-level window hosting `ui_contents` at `size`.
  // `on_closed` runs when the user closes the window (used to quit the run loop).
  // Platform-provided (one definition per OS backend).
  static std::unique_ptr<OtfPlatformWindow> Create(
      content::WebContents* ui_contents,
      const gfx::Size& size,
      base::OnceClosure on_closed);

  // The single process window, or nullptr before Create() / after close.
  static OtfPlatformWindow* Get();

  virtual ~OtfPlatformWindow() = default;

  // Page-tab hosting. ShowTab attaches `tab`'s view into the content region
  // (the window below the chrome strip), sizes/raises it and marks it visible;
  // HideTab hides it. The window tracks which tab is shown so bounds changes
  // reflow it.
  virtual void ShowTab(content::WebContents* tab) = 0;
  virtual void HideTab(content::WebContents* tab) = 0;

  // Override the content region the shown tab is sized into (px, window-relative).
  // Passing an empty rect restores the default (window minus the chrome strip).
  virtual void SetContentBounds(const gfx::Rect& bounds) = 0;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_PLATFORM_WINDOW_H_
