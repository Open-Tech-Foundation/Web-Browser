#ifndef OTF_POPUP_OVERLAY_H_
#define OTF_POPUP_OVERLAY_H_

#include <functional>
#include <string>

#include "include/cef_browser.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_message_router.h"

namespace otf {

class OtfApp;

// Shared frame for top-right-anchored popup overlays. Encapsulates the
// CefOverlayController, the BrowserView the overlay hosts, the popup's
// Chromium-side browser, and the show/hide/focus/position lifecycle that
// was previously duplicated across CreateCertificateOverlay,
// CreateAppMenuOverlay, CreateClearSiteDataOverlay, CreateDownloadsOverlay,
// CreateBookmarkOverlay.
//
// One instance per popup, owned by OtfApp via a name → unique_ptr map.
// The popup's UI lives at browser://<name> in prod, or
// <dev-ui-url>/<name>.html in dev. Public methods are UI-thread only.
class PopupOverlay {
 public:
  using RestorePayload = std::function<std::string()>;

  PopupOverlay(std::string name,
               int browser_view_id,
               int width,
               int height,
               int top_margin = 60,
               int right_margin = 18,
               int left_margin = -1);

  // Build the BrowserView + overlay controller, start hidden. Idempotent.
  void Create(CefRefPtr<CefWindow> window, OtfApp* app);

  void Show();
  void Hide();
  void Toggle();
  bool IsVisible() const;
  void Reposition();

  // Called by OtfHandler::OnAfterCreated for the popup's browser. Stores
  // the host so Show() can set focus into the popup (without this the
  // popup's keydown events never fire — Esc and the like silently break).
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser);

  // Push a fresh state-restore message to the popup's renderer. Carries
  // arbitrary JSON the popup needs to reset to (e.g. the target origin
  // for cleardata, the tab id for certificate). Called by Show().
  void PublishRestore(const std::string& json_payload);

  // Caller registers a sink for state-restore messages. The popup's UI
  // subscribes to `popup-restore:<name>` and PublishRestore() routes
  // through this callback.
  void SetRestoreSubscriber(
      CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> cb);

  // Latch a producer that builds the restore payload on demand. Used by
  // PublishRestore when the caller didn't supply an explicit payload —
  // e.g. tab-switch redraw of an already-visible popup.
  void SetRestoreProducer(RestorePayload producer);

  const std::string& name() const { return name_; }
  int view_id() const { return view_id_; }
  CefRefPtr<CefBrowser> browser() const { return browser_; }

 private:
  std::string ResolveContentUrl() const;

  const std::string name_;
  const int view_id_;
  const int width_;
  const int height_;
  const int top_margin_;
  const int right_margin_;
  const int left_margin_;

  CefRefPtr<CefOverlayController> overlay_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  OtfApp* app_ = nullptr;

  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> restore_subscriber_;
  RestorePayload restore_producer_;
};

}  // namespace otf

#endif  // OTF_POPUP_OVERLAY_H_
