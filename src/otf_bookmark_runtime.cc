#include "otf_bookmark_runtime.h"

#include "otf_app.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {

std::string BuildBookmarkStateEvent(int tab_id,
                                    const std::string& url,
                                    bool bookmarked) {
  return JsonObjectBuilder()
      .AddString("key", "bookmarks-changed")
      .AddInt("id", tab_id)
      .AddString("url", url)
      .AddBool("bookmarked", bookmarked)
      .Build();
}

std::string BuildBookmarkSyncEvent(int tab_id,
                                   const std::string& url,
                                   bool bookmarked) {
  return JsonObjectBuilder()
      .AddString("key", "bookmark-sync")
      .AddInt("id", tab_id)
      .AddString("url", url)
      .AddBool("bookmarked", bookmarked)
      .Build();
}

void OtfHandler::NotifyBookmarkStateForTab(int tab_id) {
  if (tab_id < 0 || !store_ || !tab_manager_) {
    return;
  }

  const std::string url = tab_manager_->GetUrl(tab_id);
  const bool bookmarked =
      IsPersistableWebUrl(url) && !IsGuestTab(tab_id) &&
      store_->IsBookmarked(NormalizeBookmarkUrl(url));
  SendEvent(BuildBookmarkSyncEvent(tab_id, url, bookmarked));
}

bool OtfHandler::ToggleBookmarkForTab(int tab_id,
                                      bool show_overlay_on_add,
                                      bool* bookmarked) {
  if (bookmarked) *bookmarked = false;
  if (!store_ || !tab_manager_ || IsGuestTab(tab_id)) {
    return false;
  }

  const std::string url = NormalizeBookmarkUrl(tab_manager_->GetUrl(tab_id));
  if (!IsPersistableWebUrl(url)) {
    return false;
  }

  bool is_bookmarked = false;
  if (store_->IsBookmarked(url)) {
    store_->RemoveBookmarkByUrl(url);
  } else {
    store_->AddBookmark(url, tab_manager_->GetTitle(tab_id),
                        tab_manager_->GetFaviconUrl(tab_id));
    is_bookmarked = true;
    if (show_overlay_on_add) {
      if (OtfApp* app = OtfApp::GetInstance()) {
        app->ShowBookmarkOverlay();
      }
    }
  }

  SendEvent(BuildBookmarkStateEvent(tab_id, url, is_bookmarked));
  if (bookmarked) *bookmarked = is_bookmarked;
  return true;
}

}  // namespace otf
