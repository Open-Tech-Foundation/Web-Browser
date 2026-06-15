#include "otf_popup_runtime.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <utility>

#include "include/cef_browser.h"
#include "include/cef_parser.h"
#include "include/views/cef_display.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {
namespace {

constexpr int kPopupMinWidth = 320;
constexpr int kPopupMinHeight = 240;
constexpr int kPopupDefaultWidth = 600;
constexpr int kPopupDefaultHeight = 700;

struct PopupPolicyDecision {
  bool open_as_popup = false;
  bool block = false;
  int width = kPopupDefaultWidth;
  int height = kPopupDefaultHeight;
};

std::pair<int, int> GetPopupMaxSize() {
  int screen_width = 1920;
  int screen_height = 1080;
  CefRefPtr<CefDisplay> display = CefDisplay::GetPrimaryDisplay();
  if (display) {
    const CefRect bounds = display->GetBounds();
    if (bounds.width > 0) screen_width = bounds.width;
    if (bounds.height > 0) screen_height = bounds.height;
  }
  return {std::max(kPopupMinWidth, static_cast<int>(screen_width * 0.9)),
          std::max(kPopupMinHeight, static_cast<int>(screen_height * 0.9))};
}

PopupPolicyDecision ClassifyPopupRequest(const CefPopupFeatures& features) {
  PopupPolicyDecision decision;
  const bool has_width = features.widthSet != 0;
  const bool has_height = features.heightSet != 0;
  decision.open_as_popup =
      features.isPopup != 0 || has_width || has_height;
  if (!decision.open_as_popup) {
    return decision;
  }

  const auto [max_width, max_height] = GetPopupMaxSize();
  decision.width = has_width ? features.width : kPopupDefaultWidth;
  decision.height = has_height ? features.height : kPopupDefaultHeight;

  if ((has_width &&
       (decision.width < kPopupMinWidth || decision.width > max_width)) ||
      (has_height &&
       (decision.height < kPopupMinHeight || decision.height > max_height))) {
    decision.block = true;
    return decision;
  }

  decision.width = std::clamp(decision.width, kPopupMinWidth, max_width);
  decision.height = std::clamp(decision.height, kPopupMinHeight, max_height);
  return decision;
}

void ApplyJsPermission(CefBrowserSettings& settings,
                       OtfStore* store,
                       const std::string& url) {
  if (!store) return;
  const std::string origin = ExtractOrigin(url);
  if (!origin.empty() &&
      store->GetSitePermission(origin, "javascript") == "block") {
    settings.javascript = STATE_DISABLED;
  }
}

bool IsDangerousSchemeUrl(const std::string& url) {
  static const char* kDangerousSchemes[] = {
      "javascript:", "data:", "file:", "vbscript:", "blob:"};
  for (const char* scheme : kDangerousSchemes) {
    if (url.rfind(scheme, 0) == 0) return true;
  }
  return false;
}

}  // namespace

void OtfHandler::OpenAcceptedPopup(const PendingPopup& popup) {
  OtfApp* app = OtfApp::GetInstance();
  if (popup.open_as_popup) {
    CefRefPtr<OtfHandler> self = OtfHandler::GetInstance();
    if (!self) {
      return;
    }
    CefWindowInfo wi;
    wi.bounds = CefRect(100, 100, popup.popup_width, popup.popup_height);
    wi.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserSettings bs;
    ApplyJsPermission(bs, IsGuestTab(popup.parent_tab_id) ? nullptr : store_.get(),
                      popup.url);
    CefRefPtr<CefDictionaryValue> extra;
    if (app) extra = app->MakeBrowserExtraInfo();
    CefRefPtr<CefRequestContext> rc =
        popup.opener_private ? GetPrivateRequestContext()
                             : GetActiveWorkspaceRequestContext();
    ++pending_external_popups_;
    CefBrowserHost::CreateBrowser(wi, self, popup.url, bs, extra, rc);
    return;
  }

  if (!app || !tab_manager_) {
    return;
  }
  pending_new_tab_urls_.insert(popup.url);
  const int new_id =
      app->CreateTab(popup.url, popup.parent_tab_id, popup.opener_private);
  if (popup.url.rfind("browser://", 0) == 0) {
    tab_manager_->SetSchemeUrl(new_id, popup.url);
  }
  NotifyNewTab(new_id, popup.parent_tab_id);
  app->SwitchTab(new_id);
}

bool OtfHandler::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               int popup_id,
                               const CefString& target_url,
                               const CefString& target_frame_name,
                               cef_window_open_disposition_t target_disposition,
                               bool user_gesture,
                               const CefPopupFeatures& popupFeatures,
                               CefWindowInfo& windowInfo,
                               CefRefPtr<CefClient>& client,
                               CefBrowserSettings& settings,
                               CefRefPtr<CefDictionaryValue>& extra_info,
                               bool* no_javascript_access) {
  CEF_REQUIRE_UI_THREAD();
  (void)browser;
  (void)popup_id;
  (void)windowInfo;
  (void)client;
  (void)settings;
  (void)extra_info;
  (void)no_javascript_access;

  const std::string raw_target = target_url.ToString();
  if (raw_target.empty() || IsDangerousSchemeUrl(raw_target)) {
    return true;
  }

  const std::string origin =
      frame ? ExtractOrigin(frame->GetURL().ToString()) : std::string();
  if (!store_ || origin.empty()) {
    return true;
  }

  const PopupPolicyDecision decision = ClassifyPopupRequest(popupFeatures);
  if (decision.block) {
    return true;
  }

  if (!user_gesture) {
    return true;
  }

  const int parent_tab_id = tab_manager_ ? tab_manager_->GetId(browser) : 0;
  const bool opener_private =
      tab_manager_ && tab_manager_->IsPrivate(parent_tab_id);
  const std::string target_name = target_frame_name.ToString();
  const bool tab_disposition =
      target_disposition == CEF_WOD_NEW_BACKGROUND_TAB ||
      target_disposition == CEF_WOD_NEW_FOREGROUND_TAB ||
      target_disposition == CEF_WOD_NEW_WINDOW;

  if ((target_name == "_blank" || tab_disposition) && !decision.open_as_popup) {
    PendingPopup tab_request;
    tab_request.url = raw_target;
    tab_request.origin = origin;
    tab_request.parent_tab_id = parent_tab_id;
    tab_request.open_as_popup = false;
    tab_request.opener_private = opener_private;
    OpenAcceptedPopup(tab_request);
    return true;
  }

  const std::string setting =
      IsGuestTab(parent_tab_id) ? "" : store_->GetSitePermission(origin, "popup");

  if (setting == "allow") {
    PendingPopup allowed;
    allowed.url = raw_target;
    allowed.origin = origin;
    allowed.parent_tab_id = parent_tab_id;
    allowed.open_as_popup = decision.open_as_popup;
    allowed.popup_width = decision.width;
    allowed.popup_height = decision.height;
    allowed.opener_private = opener_private;
    OpenAcceptedPopup(allowed);
    return true;
  }

  if (setting == "block") {
    return true;
  }

  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  for (auto pit = pending_popups_.begin(); pit != pending_popups_.end();) {
    if (pit->second.expires_at > 0 && pit->second.expires_at < now)
      pit = pending_popups_.erase(pit);
    else
      ++pit;
  }
  const int pending_id = next_pending_popup_id_++;
  PendingPopup pending;
  pending.url = raw_target;
  pending.origin = origin;
  pending.parent_tab_id = parent_tab_id;
  pending.expires_at = now + 30;
  pending.open_as_popup = decision.open_as_popup;
  pending.popup_width = decision.width;
  pending.popup_height = decision.height;
  pending.opener_private = opener_private;
  pending_popups_[pending_id] = pending;
  popup_ask_pending_id_ = pending_id;
  popup_ask_pending_url_ = raw_target;
  popup_ask_pending_origin_ = origin;

  if (OtfApp* app = OtfApp::GetInstance()) {
    if (auto* overlay = app->GetPopup("blockedpopup")) {
      int* pending_popup_id = &popup_ask_pending_id_;
      std::string* pending_url = &popup_ask_pending_url_;
      std::string* pending_origin = &popup_ask_pending_origin_;
      overlay->SetRestoreProducer(
          [pending_popup_id, pending_url, pending_origin]() {
            return JsonObjectBuilder()
                .AddInt("id", *pending_popup_id)
                .AddString("url", *pending_url)
                .AddString("origin", *pending_origin)
                .Build();
          });
      overlay->Show();
    }
  }
  SendEvent(JsonObjectBuilder()
                .AddString("key", "popup-blocked")
                .AddString("origin", origin)
                .AddInt("count", static_cast<int>(pending_popups_.size()))
                .Build());
  return true;
}

}  // namespace otf
