#include "otf_lifecycle_runtime.h"

#include <string>

#include "include/base/cef_logging.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_message_router_handler.h"
#include "otf_utils.h"
#include "otf_zoom_runtime.h"

namespace otf {
namespace {

void SetBrowserWindowVisible(CefRefPtr<CefBrowser> browser, bool visible) {
  if (!browser) {
    return;
  }
  browser->GetHost()->WasHidden(!visible);
}

void LogBrowserCreated(CefRefPtr<CefBrowser> browser) {
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  const int view_id = view ? view->GetID() : -1;
  const std::string url =
      browser->GetMainFrame() ? browser->GetMainFrame()->GetURL().ToString()
                              : std::string();
  LOG(INFO) << "[otf] browser OnAfterCreated: view_id=" << view_id
            << (view_id == kUiBrowserViewId ? " (UI SHELL)" : "")
            << " url=" << url;
  otf::DiagLog("browser OnAfterCreated: view_id=" + std::to_string(view_id) +
               (view_id == kUiBrowserViewId ? " (UI SHELL)" : "") +
               " url=" + url);
}

bool IsOverlayBrowserViewId(int view_id) {
  return view_id == kFindBarBrowserViewId ||
         view_id == kZoomBarBrowserViewId ||
         view_id == kDownloadsBrowserViewId ||
         view_id == kCertificateBrowserViewId ||
         view_id == kImagePreviewBrowserViewId ||
         view_id == kLinkPreviewBrowserViewId ||
         view_id == kToastNotificationBrowserViewId ||
         view_id == kConsoleBrowserViewId ||
         view_id == kSnipPreviewBrowserViewId;
}

void BindOverlayBrowser(OtfHandler* handler,
                        CefRefPtr<CefBrowser> browser,
                        int view_id) {
  if (view_id == kFindBarBrowserViewId) {
    handler->findbar_browser_ = browser;
    return;
  }
  if (view_id == kLinkPreviewBrowserViewId) {
    handler->link_preview_browser_ = browser;
    return;
  }
  if (view_id == kToastNotificationBrowserViewId) {
    handler->toast_browser_ = browser;
    return;
  }
  if (view_id == kSnipPreviewBrowserViewId) {
    handler->snip_preview_browser_ = browser;
    return;
  }
  if (view_id == kCertificateBrowserViewId) {
    handler->certificate_browser_ = browser;
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->RefreshCertificateOverlay();
      if (app->certificate_overlay_ && app->certificate_overlay_->IsVisible()) {
        browser->GetHost()->SetFocus(true);
      }
    }
  }
}

void AttachUiShellBrowser(OtfHandler* handler, CefRefPtr<CefBrowser> browser) {
  handler->ui_browser_ = browser;
  if (!handler->devtools_bridge_) {
    handler->devtools_bridge_ = new DevToolsBridge();
  }
  handler->devtools_bridge_->Attach(browser);
}

void InitializeContentTabBrowser(OtfHandler* handler,
                                 CefRefPtr<CefBrowser> browser,
                                 int tab_id) {
  if (!handler->tab_manager_) {
    return;
  }
  handler->tab_manager_->SetBrowser(tab_id, browser);

  if (OtfApp* app = OtfApp::GetInstance()) {
    bool is_visible = (tab_id == app->GetCurrentTabId());
    if (app->HasSplitView() &&
        (tab_id == app->GetSplitLeftTabId() ||
         tab_id == app->GetSplitRightTabId())) {
      is_visible = true;
    }
    SetBrowserWindowVisible(browser, is_visible);
  }

  int zoom_percent = 100;
  if (!ApplyPrivateTabZoom(browser, handler->tab_manager_, tab_id,
                           &zoom_percent) &&
      !ApplyWorkspaceOriginZoom(browser, handler->tab_manager_, tab_id,
                                &zoom_percent)) {
    zoom_percent = ToRoundedZoomPercent(browser->GetHost()->GetZoomLevel());
    handler->tab_manager_->SetZoomPercent(tab_id, zoom_percent);
  }

  std::string current = browser->GetMainFrame()->GetURL().ToString();
  if (current.empty() || current == "about:blank") {
    std::string stored = handler->tab_manager_->GetUrl(tab_id);
    if (!stored.empty()) {
      if (handler->tab_manager_->GetImagePreviewMode(tab_id) ==
              ImagePreviewMode::kDedicated &&
          stored.rfind("browser://image-preview/", 0) != 0) {
        stored = "browser://imagepreview";
      }
      browser->GetMainFrame()->LoadURL(stored);
    }
  }

  handler->StartMemoryLogging();
}

}  // namespace

void OtfHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  LogBrowserCreated(browser);
  EnsureMessageRouterInitialized();

  CefRefPtr<CefBrowserView> browser_view = CefBrowserView::GetForBrowser(browser);
  if (browser_view) {
    const int view_id = browser_view->GetID();
    if (view_id == kUiBrowserViewId) {
      AttachUiShellBrowser(this, browser);
    } else if (IsOverlayBrowserViewId(view_id)) {
      BindOverlayBrowser(this, browser, view_id);
    } else if (OtfApp* app = OtfApp::GetInstance();
               app && app->DispatchPopupBrowserCreated(view_id, browser)) {
      // Routed to a PopupOverlay.
    } else if (pending_external_popups_ > 0) {
      --pending_external_popups_;
    } else if (tab_manager_) {
      InitializeContentTabBrowser(this, browser, view_id);
    }
  } else if (pending_external_popups_ > 0) {
    --pending_external_popups_;
  }

  browser_list_.push_back(browser);
}

}  // namespace otf
