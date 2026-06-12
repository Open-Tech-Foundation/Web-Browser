#include "otf_popup_overlay.h"

#include <algorithm>
#include <utility>

#include "include/cef_command_line.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_utils.h"

namespace otf {

namespace {

// Post a SetFocus call as a UI-thread task. The click handler that opened
// the popup (e.g. the address-bar icon button) still owns the focus
// thread when Show() returns — focusing the popup browser synchronously
// gets undone as the click event unwinds. Defer to the next tick.
class DeferredFocusTask : public CefTask {
 public:
  explicit DeferredFocusTask(CefRefPtr<CefBrowser> browser)
      : browser_(browser) {}
  void Execute() override {
    if (browser_) browser_->GetHost()->SetFocus(true);
  }
 private:
  CefRefPtr<CefBrowser> browser_;
  IMPLEMENT_REFCOUNTING(DeferredFocusTask);
};

}  // namespace

PopupOverlay::PopupOverlay(std::string name,
                           int browser_view_id,
                           int width,
                           int height,
                           int top_margin,
                           int right_margin,
                           int left_margin)
    : name_(std::move(name)),
      view_id_(browser_view_id),
      width_(width),
      height_(height),
      top_margin_(top_margin),
      right_margin_(right_margin),
      left_margin_(left_margin) {}

std::string PopupOverlay::ResolveContentUrl() const {
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd && cmd->HasSwitch("dev-ui-url")) {
    return cmd->GetSwitchValue("dev-ui-url").ToString() + "/" + name_ + ".html";
  }
  return "browser://" + name_;
}

void PopupOverlay::Create(CefRefPtr<CefWindow> window, OtfApp* app) {
  CEF_REQUIRE_UI_THREAD();
  if (overlay_) return;  // idempotent
  window_ = window;
  app_ = app;

  CefRefPtr<CefBrowserView> view =
      app_->BuildOverlayBrowserView(ResolveContentUrl(), view_id_, height_);
  overlay_ = window_->AddOverlayView(view, CEF_DOCKING_MODE_CUSTOM, true);
  overlay_->SetVisible(false);
  Reposition();
}

void PopupOverlay::Reposition() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !overlay_) return;
  CefRect bounds = window_->GetBounds();
  int x = (left_margin_ >= 0) ? left_margin_
                               : std::max(0, bounds.width - width_ - right_margin_);
  overlay_->SetBounds(CefRect(x, top_margin_, width_, height_));
}

void PopupOverlay::Show() {
  CEF_REQUIRE_UI_THREAD();
  if (!overlay_) return;
  Reposition();
  overlay_->SetVisible(true);
  if (overlay_->GetContentsView()) {
    overlay_->GetContentsView()->RequestFocus();
  }
  // Focus must land in the popup's renderer for window.keydown / blur
  // events (Esc, click-outside-hide) to fire. Synchronous SetFocus gets
  // overridden by the click event that triggered Show; a deferred task
  // runs after the click unwinds and the popup is laid out.
  if (browser_) {
    CefPostTask(TID_UI, new DeferredFocusTask(browser_));
  }
  // Always publish a restore event on show so the React side resets
  // transient state ("Cleared" badges, busy spinners, prior selections)
  // even when the popup is reopened on the same target.
  if (restore_subscriber_) {
    std::string payload = restore_producer_ ? restore_producer_() : "{}";
    restore_subscriber_->Success(
        JsonObjectBuilder()
            .AddString("key", "popup-restore")
            .AddString("name", name_)
            .AddRaw("payload", payload.empty() ? "{}" : payload)
            .Build());
  }
}

void PopupOverlay::Hide() {
  CEF_REQUIRE_UI_THREAD();
  if (overlay_) {
    overlay_->SetVisible(false);
  }
  // Return focus to the active tab — without this the user's typing goes
  // nowhere visible after closing the popup.
  if (app_) {
    app_->FocusCurrentTabContent();
  }
}

void PopupOverlay::Toggle() {
  if (IsVisible()) Hide(); else Show();
}

bool PopupOverlay::IsVisible() const {
  return overlay_ && overlay_->IsVisible();
}

void PopupOverlay::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  // If we were shown before the browser finished initializing, push focus
  // now that the host is available.
  if (overlay_ && overlay_->IsVisible() && browser_) {
    browser_->GetHost()->SetFocus(true);
  }
}

void PopupOverlay::PublishRestore(const std::string& json_payload) {
  if (!restore_subscriber_) return;
  restore_subscriber_->Success(
      JsonObjectBuilder()
          .AddString("key", "popup-restore")
          .AddString("name", name_)
          .AddRaw("payload", json_payload.empty() ? "{}" : json_payload)
          .Build());
}

void PopupOverlay::SetRestoreSubscriber(
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> cb) {
  restore_subscriber_ = cb;
  if (restore_subscriber_ && IsVisible()) {
    std::string payload = restore_producer_ ? restore_producer_() : "{}";
    PublishRestore(payload);
  }
}

void PopupOverlay::SetRestoreProducer(RestorePayload producer) {
  restore_producer_ = std::move(producer);
}

}  // namespace otf
