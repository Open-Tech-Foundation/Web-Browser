#include "otf/shim/otf_popup_overlay.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "otf/shim/otf_browser_context_manager.h"
#include "otf/shim/otf_platform_window.h"
#include "otf/shim/otf_trust.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace otf {

namespace {

// Default on-screen size for a popup, by name. Anchored menus (workspace, app
// menu, split) are narrow side panels; action popups are a bit wider. Placement
// is top-left of the content region for now; callers may pass an anchor later.
gfx::Size PopupSize(const std::string& name) {
  if (name == "workspace") {
    return gfx::Size(300, 440);
  }
  if (name == "splitmenu") {
    return gfx::Size(280, 220);
  }
  if (name == "appmenu") {
    return gfx::Size(300, 420);
  }
  if (name == "blockedpopup" || name == "downloadrequest") {
    return gfx::Size(380, 220);
  }
  return gfx::Size(360, 460);
}

// Place the popup near the top-left of the content region, clamped to fit.
gfx::Rect PlacePopup(const std::string& name, const gfx::Rect& region) {
  // The page context menu is a full-window transparent layer: the React menu
  // positions itself at the click point and its backdrop captures outside clicks.
  if (name == "contextmenu") {
    return region;
  }
  constexpr int kMargin = 12;
  gfx::Size size = PopupSize(name);
  int width = std::min(size.width(), std::max(0, region.width() - 2 * kMargin));
  int height = std::min(size.height(), std::max(0, region.height() - 2 * kMargin));
  return gfx::Rect(region.x() + kMargin, region.y() + kMargin, width, height);
}

}  // namespace

// static
OtfPopupOverlay& OtfPopupOverlay::Get() {
  static base::NoDestructor<OtfPopupOverlay> instance;
  return *instance;
}

OtfPopupOverlay::OtfPopupOverlay() = default;
OtfPopupOverlay::~OtfPopupOverlay() = default;

void OtfPopupOverlay::SetCallbacks(OtfCallbacks callbacks) {
  callbacks_ = callbacks;
}

content::WebContents* OtfPopupOverlay::EnsureContents(const std::string& name) {
  auto it = popups_.find(name);
  if (it != popups_.end()) {
    return it->second.get();
  }
  // Overlays are chrome (browser://), so they use the system context.
  OtfBrowserContextManager* manager = OtfBrowserContextManager::Get();
  content::BrowserContext* context = manager ? manager->System() : nullptr;
  if (!context) {
    return nullptr;
  }
  content::WebContents::CreateParams params(context);
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(params);
  content::WebContents* raw = contents.get();

  // Transparent page background so the rounded popup floats over the page/chrome
  // behind it (the "<name>.html" body uses a transparent backdrop).
  raw->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);

  // Popup content ships next to the UI as "<name>.html" (same origin as the UI,
  // so the bridge trust gate admits it).
  const GURL url = ResolveUiUrl().Resolve(name + ".html");
  raw->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(url));

  popups_[name] = std::move(contents);
  return raw;
}

void OtfPopupOverlay::Show(const std::string& name) {
  OtfPlatformWindow* window = OtfPlatformWindow::Get();
  content::WebContents* wc = EnsureContents(name);
  if (!window || !wc) {
    return;
  }
  const gfx::Rect bounds = PlacePopup(name, window->ContentRegion());
  window->ShowOverlay(
      wc, bounds,
      base::BindRepeating(&OtfPopupOverlay::OnDismissed,
                          base::Unretained(this), name));
}

void OtfPopupOverlay::Hide(const std::string& name) {
  auto it = popups_.find(name);
  if (it == popups_.end()) {
    return;
  }
  if (OtfPlatformWindow* window = OtfPlatformWindow::Get()) {
    window->HideOverlay(it->second.get());
  }
}

void OtfPopupOverlay::OnDismissed(const std::string& name) {
  Hide(name);
  if (callbacks_.on_popup_closed) {
    callbacks_.on_popup_closed(callbacks_.user_data, name.c_str());
  }
}

}  // namespace otf
