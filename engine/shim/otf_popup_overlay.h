// Named popup overlays: transparent WebContents ("<name>.html") layered over the
// top-level window, driven from the Rust backend via OtfUiApi (ui.popup.*). The
// window dismisses a popup on a click outside its bounds and reports that back
// through on_popup_closed so Rust's open-state stays in sync.

#ifndef OTF_ENGINE_SHIM_OTF_POPUP_OVERLAY_H_
#define OTF_ENGINE_SHIM_OTF_POPUP_OVERLAY_H_

#include <map>
#include <memory>
#include <string>

#include "base/no_destructor.h"
#include "otf/shim/bridge.h"

namespace content {
class WebContents;
}

namespace otf {

class OtfPopupOverlay {
 public:
  static OtfPopupOverlay& Get();

  // The observer table (carries on_popup_closed + user_data), shared with the
  // bridge/tab hosts. Wired once at lifecycle init.
  void SetCallbacks(OtfCallbacks callbacks);

  // Open/raise (or dismiss) the popup named `name`. Its WebContents is created
  // lazily on first Show and cached, so reopening is cheap and keeps subscriptions.
  void Show(const std::string& name);
  void Hide(const std::string& name);

 private:
  OtfPopupOverlay();
  ~OtfPopupOverlay();

  content::WebContents* EnsureContents(const std::string& name);
  // Click-outside dismissal: hide the view and notify Rust.
  void OnDismissed(const std::string& name);

  std::map<std::string, std::unique_ptr<content::WebContents>> popups_;
  OtfCallbacks callbacks_ = {};

  friend class base::NoDestructor<OtfPopupOverlay>;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_POPUP_OVERLAY_H_
