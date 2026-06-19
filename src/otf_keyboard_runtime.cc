#include "otf_keyboard_runtime.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_find_runtime.h"
#include "otf_handler.h"
#include "otf_keyboard_shortcuts.h"
#include "otf_popup_overlay.h"
#include "otf_utils.h"

namespace otf {
namespace {

bool HandleNavigationShortcut(OtfHandler* handler,
                              int tab_id,
                              const char* action) {
  if (!handler->tab_manager_) {
    return false;
  }
  CefRefPtr<CefBrowser> browser = handler->tab_manager_->GetBrowser(tab_id);
  if (!browser) {
    return false;
  }

  if (action == Shortcut::kBack) {
    browser->GoBack();
  }
  if (action == Shortcut::kForward) {
    browser->GoForward();
  }
  if (action == Shortcut::kReload) {
    browser->Reload();
  }
  if (action == Shortcut::kEscape) {
    browser->StopLoad();
  }
  if (action == Shortcut::kZoomIn || action == Shortcut::kZoomOut ||
      action == Shortcut::kZoomReset) {
    std::string zoom_action = "reset";
    if (action == Shortcut::kZoomIn) {
      zoom_action = "in";
    } else if (action == Shortcut::kZoomOut) {
      zoom_action = "out";
    }
    handler->ApplyTabZoomAction(tab_id, zoom_action);
  }
  return true;
}

bool HideEscClosablePopup(OtfApp* app, const char* name) {
  if (!app) {
    return false;
  }
  if (otf::PopupOverlay* popup = app->GetPopup(name);
      popup && popup->IsVisible()) {
    popup->Hide();
    return true;
  }
  return false;
}

bool HandleEscapeForFindbar(OtfHandler* handler, OtfApp* app, int tab_id) {
  if (!app || !app->findbar_overlay_ || !app->findbar_overlay_->IsVisible()) {
    return false;
  }

  app->findbar_overlay_->SetVisible(false);
  if (handler->tab_manager_) {
    handler->tab_manager_->ClearFindState(tab_id);
    CefRefPtr<CefBrowser> browser = handler->tab_manager_->GetBrowser(tab_id);
    if (browser) {
      browser->GetHost()->StopFinding(true);
    }
  }
  if (handler->pending_find_tab_ == tab_id) {
    handler->pending_find_tab_ = -1;
    handler->pending_find_text_.clear();
  }
  app->FocusCurrentTabContent();
  if (handler->findbar_subscription_) {
    handler->findbar_subscription_->Success(
        BuildFindResultEvent(0, 0, -1, "", true));
    handler->findbar_subscription_->Success(BuildFindbarClosedEvent(tab_id));
  }
  return true;
}

bool HandleEscapeShortcut(OtfHandler* handler,
                          OtfApp* app,
                          int tab_id,
                          bool* is_keyboard_shortcut) {
  if (!app) {
    return false;
  }

  if (app->IsFullscreen() && !app->IsContentFullscreen()) {
    app->ToggleFullscreen();
    return true;
  }
  if (app->snip_preview_overlay_ && app->snip_preview_overlay_->IsVisible()) {
    app->HideSnipPreviewOverlay();
    app->FocusCurrentTabContent();
    return true;
  }
  if (app->downloads_overlay_ && app->downloads_overlay_->IsVisible()) {
    app->HideDownloadsOverlay();
    app->FocusCurrentTabContent();
    return true;
  }
  if (app->zoombar_overlay_ && app->zoombar_overlay_->IsVisible()) {
    app->HideZoomBarOverlay();
    app->FocusCurrentTabContent();
    return true;
  }
  if (app->certificate_overlay_ && app->certificate_overlay_->IsVisible()) {
    app->HideCertificateOverlay();
    return true;
  }
  if (app->appmenu_overlay_ && app->appmenu_overlay_->IsVisible()) {
    app->HideAppMenuOverlay();
    return true;
  }
  if (app->bookmark_overlay_ && app->bookmark_overlay_->IsVisible()) {
    app->HideBookmarkOverlay();
    return true;
  }
  if (HideEscClosablePopup(app, "cleardata") ||
      HideEscClosablePopup(app, "workspace") ||
      HideEscClosablePopup(app, "qr")) {
    return true;
  }
  if (HandleEscapeForFindbar(handler, app, tab_id)) {
    return true;
  }
  if (app->IsContentFullscreen()) {
    CefRefPtr<CefBrowser> browser =
        handler->tab_manager_ ? handler->tab_manager_->GetBrowser(tab_id) : nullptr;
    if (browser) {
      browser->GetHost()->ExitFullscreen(true);
    }
    return true;
  }
  *is_keyboard_shortcut = true;
  HandleNavigationShortcut(handler, tab_id, Shortcut::kEscape);
  return true;
}

void OpenInternalTab(OtfHandler* handler,
                     OtfApp* app,
                     CefRefPtr<CefBrowser> browser,
                     const std::string& url) {
  const int parent_id = handler->tab_manager_ ? handler->tab_manager_->GetId(browser) : -1;
  const int tab_id = app->CreateTab(url);
  handler->NotifyNewTab(tab_id, parent_id);
  app->SwitchTab(tab_id);
}

void HandleScreenshotShortcut(OtfHandler* handler, OtfApp* app, int tab_id) {
  CefRefPtr<CefBrowser> browser =
      handler->tab_manager_ ? handler->tab_manager_->GetBrowser(tab_id) : nullptr;
  if (!browser || !handler->devtools_bridge_) {
    return;
  }
  handler->devtools_bridge_->Attach(browser);
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("format", "png");
  handler->devtools_bridge_->Execute(
      "Page.captureScreenshot", params,
      [](bool ok, const std::string& result_json) {
        if (!ok) {
          return;
        }
        OtfApp* app = OtfApp::GetInstance();
        OtfHandler* handler = OtfHandler::GetInstance();
        if (!app || !handler || !handler->snip_preview_browser_) {
          return;
        }
        app->ShowSnipPreviewOverlay();
        const std::string js =
            "window.__otfSetSnipImage(" + result_json + ");";
        handler->snip_preview_browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
      });
}

void ReopenClosedTabInActiveWorkspace(OtfHandler* handler, OtfApp* app) {
  while (!handler->recently_closed_tabs_.empty()) {
    OtfHandler::ClosedTabInfo info =
        std::move(handler->recently_closed_tabs_.front());
    handler->recently_closed_tabs_.pop_front();
    if (info.workspace_id != handler->active_workspace_id_) {
      continue;
    }

    int tab_id = -1;
    if (info.is_image_preview) {
      WorkspaceTab tab;
      tab.url = info.url;
      tab.title = info.title;
      tab.is_image_preview = true;
      tab.preview_local_path = info.preview_local_path;
      tab.preview_page = info.preview_page;
      tab_id = app->CreateRestoredTab(tab);
    } else if (info.is_doc_preview) {
      WorkspaceTab tab;
      tab.url = info.url;
      tab.title = info.title;
      tab.is_doc_preview = true;
      tab.preview_local_path = info.preview_local_path;
      tab_id = app->CreateRestoredTab(tab);
    } else if (!info.url.empty() && !otf::IsLocalFilesystemPathLike(info.url) &&
               otf::IsAllowedStartupUrl(info.url)) {
      tab_id = app->CreateTab(info.url);
    }

    if (tab_id < 0) {
      continue;
    }
    if (!info.favicon.empty() && handler->tab_manager_) {
      handler->tab_manager_->SetFaviconUrl(tab_id, info.favicon);
    }
    handler->NotifyNewTab(tab_id, -1);
    app->SwitchTab(tab_id);
    return;
  }
}

bool HandleTabCycleShortcut(OtfHandler* handler,
                            OtfApp* app,
                            int current_tab_id,
                            bool forward) {
  if (!handler->tab_manager_) {
    return false;
  }
  const auto ids =
      handler->tab_manager_->GetTabIdsForWorkspace(handler->active_workspace_id_);
  if (ids.size() < 2) {
    return true;
  }
  const auto it = std::find(ids.begin(), ids.end(), current_tab_id);
  if (it == ids.end()) {
    return true;
  }
  const int next_tab_id =
      forward ? (it + 1 != ids.end() ? *(it + 1) : ids.front())
              : (it != ids.begin() ? *(it - 1) : ids.back());
  app->SwitchTab(next_tab_id);
  return true;
}

}  // namespace

bool OtfHandler::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                               const CefKeyEvent& event,
                               CefEventHandle os_event,
                               bool* is_keyboard_shortcut) {
  if (event.type == KEYEVENT_KEYUP || event.type == KEYEVENT_CHAR) {
    return false;
  }

  const uint32_t modifiers = Mod::Of(event.modifiers);
  const int key = event.windows_key_code;
  const auto matches = [=](uint32_t mod, int expected_key) {
    return modifiers == mod && key == expected_key;
  };

  OtfApp* const app = OtfApp::GetInstance();
  if (!app || !tab_manager_) {
    return false;
  }
  const int current_tab_id = app->GetCurrentTabId();

  if (matches(Mod::kAlt, Key::kLeft)) {
    HandleNavigationShortcut(this, current_tab_id, Shortcut::kBack);
    return true;
  }
  if (matches(Mod::kAlt, Key::kRight)) {
    HandleNavigationShortcut(this, current_tab_id, Shortcut::kForward);
    return true;
  }
  if (matches(Mod::kNone, Key::kF5) || matches(Mod::kCtrl, Key::kR)) {
    *is_keyboard_shortcut = true;
    HandleNavigationShortcut(this, current_tab_id, Shortcut::kReload);
    return true;
  }
  if (matches(Mod::kCtrl | Mod::kShift, Key::kR)) {
    *is_keyboard_shortcut = true;
    CefRefPtr<CefBrowser> active_browser = tab_manager_->GetBrowser(current_tab_id);
    if (active_browser) {
      active_browser->ReloadIgnoreCache();
    }
    return true;
  }
  if (matches(Mod::kNone, Key::kF11)) {
    *is_keyboard_shortcut = true;
    if (app->IsContentFullscreen()) {
      CefRefPtr<CefBrowser> active_browser =
          tab_manager_ ? tab_manager_->GetBrowser(current_tab_id) : nullptr;
      if (active_browser) {
        active_browser->GetHost()->ExitFullscreen(true);
      }
    } else {
      app->ToggleFullscreen();
    }
    return true;
  }
  if (matches(Mod::kNone, Key::kEscape)) {
    return HandleEscapeShortcut(this, app, current_tab_id, is_keyboard_shortcut);
  }
  if (matches(Mod::kCtrl, Key::kPlus) ||
      matches(Mod::kCtrl | Mod::kShift, Key::kPlus) ||
      matches(Mod::kCtrl, Key::kEquals) ||
      matches(Mod::kCtrl | Mod::kShift, Key::kEquals) ||
      matches(Mod::kCtrl, Key::kNumAdd)) {
    *is_keyboard_shortcut = true;
    HandleNavigationShortcut(this, current_tab_id, Shortcut::kZoomIn);
    return true;
  }
  if (matches(Mod::kCtrl, Key::kMinus) ||
      matches(Mod::kCtrl, Key::kNumMinus)) {
    *is_keyboard_shortcut = true;
    HandleNavigationShortcut(this, current_tab_id, Shortcut::kZoomOut);
    return true;
  }
  if (matches(Mod::kCtrl, Key::k0) ||
      matches(Mod::kCtrl, Key::kNum0)) {
    *is_keyboard_shortcut = true;
    HandleNavigationShortcut(this, current_tab_id, Shortcut::kZoomReset);
    return true;
  }
  if (matches(Mod::kCtrl, Key::kHome)) {
    CefRefPtr<CefBrowser> active_browser = tab_manager_->GetBrowser(current_tab_id);
    if (active_browser) {
      active_browser->GetMainFrame()->ExecuteJavaScript(
          "window.scrollTo({ top: 0, behavior: 'auto' });", "", 0);
    }
    return true;
  }
  if (matches(Mod::kCtrl, Key::kEnd)) {
    CefRefPtr<CefBrowser> active_browser = tab_manager_->GetBrowser(current_tab_id);
    if (active_browser) {
      active_browser->GetMainFrame()->ExecuteJavaScript(
          "window.scrollTo({ top: document.documentElement.scrollHeight, behavior: 'auto' });",
          "", 0);
    }
    return true;
  }
  if (matches(Mod::kCtrl, Key::kF)) {
    if (app->findbar_overlay_ && tab_manager_) {
      tab_manager_->SetFindVisible(current_tab_id, true);
      app->RestoreFindSessionForTab(current_tab_id, true);
    }
    return true;
  }
  if (matches(Mod::kCtrl, Key::kD)) {
    ToggleBookmarkForTab(current_tab_id, false, nullptr);
    return true;
  }
  if (matches(Mod::kCtrl, Key::kG) ||
      matches(Mod::kCtrl | Mod::kShift, Key::kG)) {
    std::string text = tab_manager_->GetFindText(current_tab_id);
    const bool case_sensitive = tab_manager_->GetFindCase(current_tab_id);
    if (!text.empty()) {
      CefRefPtr<CefBrowser> active_browser = tab_manager_->GetBrowser(current_tab_id);
      if (active_browser) {
        active_browser->GetHost()->Find(
            text, !matches(Mod::kCtrl | Mod::kShift, Key::kG), case_sensitive, true);
      }
    }
    return true;
  }
  if (matches(Mod::kCtrl, Key::kL) || matches(Mod::kNone, Key::kF6)) {
    SendShortcut(this, Shortcut::kFocusBar);
    return true;
  }
  if (matches(Mod::kCtrl, Key::kH)) {
    OpenInternalTab(this, app, browser, "browser://history");
    return true;
  }
  if (matches(Mod::kCtrl, Key::kJ)) {
    OpenInternalTab(this, app, browser, "browser://downloads");
    return true;
  }
  if (matches(Mod::kCtrl | Mod::kShift, Key::kJ) ||
      matches(Mod::kNone, Key::kF12)) {
    *is_keyboard_shortcut = true;
    app->ToggleConsoleOverlay();
    return true;
  }
  if (matches(Mod::kCtrl | Mod::kShift, Key::kS)) {
    *is_keyboard_shortcut = true;
    HandleScreenshotShortcut(this, app, current_tab_id);
    return true;
  }
  if (matches(Mod::kCtrl, Key::kT)) {
    const int tab_id = app->CreateTab("browser://newtab");
    NotifyNewTab(tab_id, -1);
    app->SwitchTab(tab_id);
    return true;
  }
  if (matches(Mod::kCtrl | Mod::kShift, Key::kN)) {
    const int tab_id = app->CreateTab("browser://newtab", -1, true);
    NotifyNewTab(tab_id, -1);
    app->SwitchTab(tab_id);
    return true;
  }
  if (matches(Mod::kCtrl, Key::kW)) {
    CloseTabAndNotify(current_tab_id, true);
    return true;
  }
  if (matches(Mod::kCtrl | Mod::kShift, Key::kT)) {
    ReopenClosedTabInActiveWorkspace(this, app);
    return true;
  }
  if (matches(Mod::kCtrl, Key::kTab) ||
      matches(Mod::kCtrl | Mod::kShift, Key::kTab)) {
    return HandleTabCycleShortcut(
        this, app, current_tab_id, matches(Mod::kCtrl, Key::kTab));
  }

  return false;
}

}  // namespace otf
