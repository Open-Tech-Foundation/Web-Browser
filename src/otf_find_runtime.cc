#include "otf_find_runtime.h"

#include "include/views/cef_browser_view.h"
#include "otf_app.h"
#include "otf_browser_shell.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {

std::string BuildFindResultEvent(int count,
                                 int active,
                                 int tab_id,
                                 const std::string& text,
                                 bool final_update,
                                 int seq) {
  JsonObjectBuilder builder;
  builder.AddString("key", "find-result")
      .AddInt("count", count)
      .AddInt("active", active)
      .AddInt("tabId", tab_id)
      .AddBool("final", final_update)
      .AddInt("seq", seq);
  if (!text.empty() || tab_id < 0) {
    builder.AddString("text", text);
  }
  return builder.Build();
}

std::string BuildFindbarClosedEvent(int tab_id) {
  return JsonObjectBuilder()
      .AddString("key", "findbar-closed")
      .AddInt("tabId", tab_id)
      .Build();
}

void OtfHandler::OnFindResult(CefRefPtr<CefBrowser> browser,
                              int identifier,
                              int count,
                              const CefRect& selectionRect,
                              int activeMatchOrdinal,
                              bool finalUpdate) {
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (!view || !tab_manager_) return;
  int tab_id = view->GetID();
  if (tab_id == kUiBrowserViewId || tab_id == kFindBarBrowserViewId ||
      tab_id == kZoomBarBrowserViewId || tab_id == kDownloadsBrowserViewId) {
    return;
  }

  OtfApp* app = OtfApp::GetInstance();
  if (!app) return;

  const bool is_current_tab = app->GetCurrentTabId() == tab_id;
  const bool is_background_stop_reset =
      !is_current_tab && finalUpdate && count == 0 && activeMatchOrdinal == 0 &&
      tab_manager_->IsFindVisible(tab_id) &&
      !tab_manager_->GetFindText(tab_id).empty();

  if (!is_current_tab) {
    if (!is_background_stop_reset) {
      tab_manager_->SetFindCount(tab_id, count);
      tab_manager_->SetFindActive(tab_id, activeMatchOrdinal);
    }
    return;
  }

  if (finalUpdate && count > 0 && activeMatchOrdinal == 0 &&
      pending_find_tab_ == tab_id && !pending_find_text_.empty()) {
    browser->GetHost()->Find(pending_find_text_, true,
                             tab_manager_->GetFindCase(tab_id), true);
    return;
  }

  if (!is_background_stop_reset) {
    tab_manager_->SetFindCount(tab_id, count);
    tab_manager_->SetFindActive(tab_id, activeMatchOrdinal);
  }

  if (restore_find_in_progress_ && pending_find_tab_ == tab_id && finalUpdate) {
    if (count <= 0 || restore_find_target_ordinal_ <= 1 ||
        activeMatchOrdinal >= restore_find_target_ordinal_) {
      restore_find_in_progress_ = false;
      restore_find_target_ordinal_ = 0;
    } else {
      browser->GetHost()->Find(pending_find_text_, true,
                               tab_manager_->GetFindCase(tab_id), true);
    }
  } else if (finalUpdate && pending_find_tab_ == tab_id) {
    restore_find_target_ordinal_ = 0;
  }

  if (findbar_subscription_) {
    findbar_subscription_->Success(BuildFindResultEvent(
        count, activeMatchOrdinal, tab_id, "", finalUpdate, pending_find_seq_));
  }
}

}  // namespace otf
