#include "otf_tab_runtime.h"

#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_doc_preview_runtime.h"
#include "otf_handler.h"
#include "otf_image_preview_runtime.h"
#include "otf_split_runtime.h"
#include "otf_utils.h"

namespace otf {
namespace {

constexpr size_t kMaxClosedTabs = 25;

bool IsDangerousSchemeUrl(const std::string& url) {
  return url.rfind("javascript:", 0) == 0 ||
         url.rfind("data:", 0) == 0 ||
         url.rfind("file:", 0) == 0 ||
         url.rfind("vbscript:", 0) == 0 ||
         url.rfind("blob:", 0) == 0;
}

bool IsNonTabBrowserViewId(int view_id) {
  return view_id == kUiBrowserViewId ||
         view_id == kFindBarBrowserViewId ||
         view_id == kZoomBarBrowserViewId ||
         view_id == kDownloadsBrowserViewId ||
         view_id == kAppMenuBrowserViewId ||
         view_id == kCertificateBrowserViewId ||
         view_id == kBookmarkBrowserViewId ||
         view_id == kImagePreviewBrowserViewId ||
         view_id == kDocPreviewBrowserViewId ||
         view_id == kClearSiteDataBrowserViewId ||
         view_id == kWorkspaceBrowserViewId ||
         view_id == kQrBrowserViewId ||
         view_id == kBlockedPopupBrowserViewId ||
         view_id == kDownloadRequestBrowserViewId ||
         view_id == kLinkPreviewBrowserViewId ||
         view_id == kToastNotificationBrowserViewId ||
         view_id == kConsoleBrowserViewId ||
         view_id == kSnipPreviewBrowserViewId ||
         view_id == kSplitMenuBrowserViewId;
}

}  // namespace

bool OtfHandler::OnOpenURLFromTab(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& target_url,
    cef_window_open_disposition_t target_disposition,
    bool user_gesture) {
  CEF_REQUIRE_UI_THREAD();
  (void)frame;

  const std::string raw_target = target_url.ToString();
  const bool is_new_context_disposition =
      target_disposition == CEF_WOD_NEW_BACKGROUND_TAB ||
      target_disposition == CEF_WOD_NEW_FOREGROUND_TAB ||
      target_disposition == CEF_WOD_NEW_WINDOW;
  if (!is_new_context_disposition) {
    return false;
  }
  if (!user_gesture) {
    return true;
  }
  if (raw_target.empty() || IsDangerousSchemeUrl(raw_target)) {
    return true;
  }

  OtfApp* app = OtfApp::GetInstance();
  if (!app || !tab_manager_) {
    return true;
  }

  const bool opener_private =
      tab_manager_->IsPrivate(tab_manager_->GetId(browser));
  pending_new_tab_urls_.insert(raw_target);
  const int parent_id = tab_manager_->GetId(browser);
  const int new_id = app->CreateTab(raw_target, parent_id, opener_private);
  if (raw_target.rfind("browser://", 0) == 0) {
    tab_manager_->SetSchemeUrl(new_id, raw_target);
  }
  NotifyNewTab(new_id, parent_id);
  if (target_disposition == CEF_WOD_NEW_FOREGROUND_TAB ||
      target_disposition == CEF_WOD_NEW_WINDOW) {
    app->SwitchTab(new_id);
  }
  return true;
}

void OtfHandler::OnGotFocus(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || !tab_manager_) return;
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (!view || IsNonTabBrowserViewId(view->GetID())) return;
  const int tab_id = view->GetID();
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !app->HasSplitView() || !app->IsTabInSplitView(tab_id)) return;
  if (app->GetCurrentTabId() == tab_id) return;
  if (app->ActivateSplitPane(tab_id)) {
    SetSplitViewTabs(active_workspace_id_, app->GetSplitLeftTabId(),
                     app->GetSplitRightTabId(), tab_id);
    NotifySplitStateChanged(active_workspace_id_);
  }
}

void OtfHandler::CloseTabAndNotify(int tab_id, bool allow_pinned) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app) {
    return;
  }
  if (tab_manager_ && tab_manager_->IsPinned(tab_id) && !allow_pinned) return;
  const bool closed_split_tab = IsSplitTab(tab_id);
  if (tab_manager_) {
    std::string url = tab_manager_->GetUrl(tab_id);
    const bool is_image_preview =
        tab_manager_->GetImagePreviewMode(tab_id) ==
        ImagePreviewMode::kDedicated;
    const bool is_doc_preview =
        tab_manager_->GetDocPreviewMode(tab_id) == DocPreviewMode::kDedicated;
    if (!tab_manager_->IsPrivate(tab_id) &&
        (IsPersistableWebUrl(url) || is_image_preview || is_doc_preview)) {
      ClosedTabInfo info;
      info.url = std::move(url);
      info.title = tab_manager_->GetTitle(tab_id);
      info.favicon = tab_manager_->GetFaviconUrl(tab_id);
      info.workspace_id = tab_manager_->GetWorkspaceId(tab_id);
      info.is_image_preview = is_image_preview;
      info.is_doc_preview = is_doc_preview;
      if (is_image_preview) {
        info.preview_local_path = GetImagePreviewLocalFileForTab(tab_id);
        info.preview_page = GetImagePreviewPageForTab(tab_id);
      } else if (is_doc_preview) {
        info.preview_local_path = GetDocPreviewLocalFileForTab(tab_id);
      }
      recently_closed_tabs_.push_front(std::move(info));
      if (recently_closed_tabs_.size() > kMaxClosedTabs) {
        recently_closed_tabs_.pop_back();
      }
    }
    if (is_image_preview) {
      ClearImagePreviewStateForTab(tab_id);
    }
  }
  if (app->CloseTab(tab_id, allow_pinned) < 0) {
    return;
  }
  if (closed_split_tab) {
    SyncSplitStateFromApp();
  } else if (app->HasSplitView() && IsSplitTab(app->GetCurrentTabId())) {
    SyncSplitStateFromApp();
  }
  SendEvent(JsonObjectBuilder()
                .AddString("key", "tab-closed")
                .AddInt("id", tab_id)
                .Build());
}

}  // namespace otf
