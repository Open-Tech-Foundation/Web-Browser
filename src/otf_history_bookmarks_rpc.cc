#include "otf_history_bookmarks_rpc.h"

#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "otf_app.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {
namespace {

using Callback = CefMessageRouterBrowserSide::Handler::Callback;

bool HasOnlyParamKeys(CefRefPtr<CefDictionaryValue> params,
                      const std::set<std::string>& allowed,
                      std::string* error) {
  CefDictionaryValue::KeyList keys;
  params->GetKeys(keys);
  for (const auto& key : keys) {
    const std::string k = key.ToString();
    if (!allowed.count(k)) {
      if (error) *error = "unexpected param: " + k;
      return false;
    }
  }
  return true;
}

bool RequireNoParams(const NativeRpcRequest& request, std::string* error) {
  return request.params && HasOnlyParamKeys(request.params, {}, error);
}

bool ReadPositiveInt(CefRefPtr<CefDictionaryValue> params,
                     const std::string& key,
                     int* value,
                     std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_INT) {
    if (error) *error = key + " must be an integer";
    return false;
  }
  const int parsed = params->GetInt(key);
  if (parsed <= 0) {
    if (error) *error = key + " must be positive";
    return false;
  }
  if (value) *value = parsed;
  return true;
}

bool ReadRequiredString(CefRefPtr<CefDictionaryValue> params,
                        const std::string& key,
                        std::string* value,
                        std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_STRING) {
    if (error) *error = key + " must be a string";
    return false;
  }
  std::string parsed = params->GetString(key).ToString();
  if (parsed.empty()) {
    if (error) *error = key + " must not be empty";
    return false;
  }
  if (value) *value = std::move(parsed);
  return true;
}

bool ReadOptionalString(CefRefPtr<CefDictionaryValue> params,
                        const std::string& key,
                        std::string* value,
                        std::string* error) {
  if (!params || !params->HasKey(key)) {
    if (value) value->clear();
    return true;
  }
  if (params->GetType(key) != VTYPE_STRING) {
    if (error) *error = key + " must be a string";
    return false;
  }
  if (value) *value = params->GetString(key).ToString();
  return true;
}

std::string BuildHistoryJson(const std::vector<HistoryEntry>& items) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (i > 0) out << ",";
    out << JsonObjectBuilder()
               .AddInt("id", item.id)
               .AddInt("workspaceId", item.workspace_id)
               .AddString("url", item.url)
               .AddString("title", item.title)
               .AddInt("visitCount", item.visit_count)
               .AddRaw("lastVisitAt", std::to_string(item.last_visit_at))
               .AddRaw("createdAt", std::to_string(item.created_at))
               .Build();
  }
  out << "]";
  return out.str();
}

std::string BuildBookmarksJson(const std::vector<BookmarkEntry>& items) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (i > 0) out << ",";
    out << JsonObjectBuilder()
               .AddInt("id", item.id)
               .AddString("title", item.title)
               .AddString("url", item.url)
               .AddString("faviconUrl", item.favicon_url)
               .AddInt("position", item.position)
               .AddRaw("createdAt", std::to_string(item.created_at))
               .AddRaw("updatedAt", std::to_string(item.updated_at))
               .Build();
  }
  out << "]";
  return out.str();
}

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

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

bool HandleHistoryRpc(OtfHandler* handler,
                      CefRefPtr<Callback> callback,
                      const NativeRpcRequest& request) {
  std::string error;
  if (request.method == "history.list") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    NativeRpcSuccessRaw(
        callback, request,
        handler->store_ && !handler->guest_session_active_
            ? BuildHistoryJson(handler->store_->GetHistory(
                  200, handler->active_workspace_id_))
            : "[]");
    return true;
  }

  if (request.method == "history.delete") {
    if (!HasOnlyParamKeys(request.params, {"id"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (handler->guest_session_active_) {
      Failure(callback, request, "guest_session",
              "This action is disabled in guest sessions");
      return true;
    }
    int id = 0;
    if (!ReadPositiveInt(request.params, "id", &id, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (handler->store_) handler->store_->DeleteHistoryItem(id);
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  if (request.method == "history.clear") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (handler->guest_session_active_) {
      Failure(callback, request, "guest_session",
              "This action is disabled in guest sessions");
      return true;
    }
    if (handler->store_) {
      handler->store_->ClearHistory(handler->active_workspace_id_);
    }
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  return false;
}

bool HandleBookmarksRpc(OtfHandler* handler,
                        CefRefPtr<Callback> callback,
                        const NativeRpcRequest& request) {
  std::string error;
  if (request.method == "bookmarks.list") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    NativeRpcSuccessRaw(
        callback, request,
        handler->store_ && !handler->guest_session_active_
            ? BuildBookmarksJson(handler->store_->GetBookmarks())
            : "[]");
    return true;
  }

  if (request.method == "bookmarks.remove") {
    if (!HasOnlyParamKeys(request.params, {"id"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (handler->guest_session_active_) {
      Failure(callback, request, "guest_session",
              "This action is disabled in guest sessions");
      return true;
    }
    int id = 0;
    if (!ReadPositiveInt(request.params, "id", &id, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (handler->store_) {
      handler->store_->RemoveBookmark(id);
      if (handler->tab_manager_) {
        for (int tab_id : handler->tab_manager_->GetAllTabIds()) {
          handler->NotifyBookmarkStateForTab(tab_id);
        }
      }
    }
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  if (request.method == "bookmarks.update") {
    if (!HasOnlyParamKeys(request.params, {"id", "url", "title"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (handler->guest_session_active_) {
      Failure(callback, request, "guest_session",
              "This action is disabled in guest sessions");
      return true;
    }
    int id = 0;
    std::string url;
    std::string title;
    if (!ReadPositiveInt(request.params, "id", &id, &error) ||
        !ReadRequiredString(request.params, "url", &url, &error) ||
        !ReadRequiredString(request.params, "title", &title, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    url = NormalizeBookmarkUrl(url);
    if (!handler->store_ || !IsPersistableWebUrl(url)) {
      Failure(callback, request, "invalid_params", "Invalid bookmark payload");
      return true;
    }
    handler->store_->UpdateBookmark(id, url, title);
    if (handler->tab_manager_) {
      for (int tab_id : handler->tab_manager_->GetAllTabIds()) {
        handler->NotifyBookmarkStateForTab(tab_id);
      }
    }
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  if (request.method == "bookmarks.add") {
    if (!HasOnlyParamKeys(request.params, {"url", "title", "faviconUrl"},
                          &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (handler->guest_session_active_) {
      Failure(callback, request, "guest_session",
              "Bookmarks are disabled in guest sessions");
      return true;
    }
    std::string url;
    std::string title;
    std::string favicon;
    if (!ReadRequiredString(request.params, "url", &url, &error) ||
        !ReadRequiredString(request.params, "title", &title, &error) ||
        !ReadOptionalString(request.params, "faviconUrl", &favicon, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    url = NormalizeBookmarkUrl(url);
    if (!handler->store_ || !IsPersistableWebUrl(url)) {
      Failure(callback, request, "invalid_params", "Invalid bookmark payload");
      return true;
    }
    handler->store_->AddBookmark(url, title, favicon);
    handler->SendEvent(BuildBookmarkStateEvent(-1, url, true));
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  if (request.method == "bookmarks.toggleCurrent") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    bool bookmarked = false;
    OtfApp* app = OtfApp::GetInstance();
    if (app && !handler->guest_session_active_ && handler->tab_manager_ &&
        handler->store_) {
      const int tab_id = app->GetCurrentTabId();
      const std::string url =
          NormalizeBookmarkUrl(handler->tab_manager_->GetUrl(tab_id));
      if (IsPersistableWebUrl(url) && !handler->IsGuestTab(tab_id)) {
        if (!handler->store_->IsBookmarked(url)) {
          const std::string title = handler->tab_manager_->GetTitle(tab_id);
          const std::string favicon = handler->tab_manager_->GetFaviconUrl(tab_id);
          handler->store_->AddBookmark(url, title, favicon);
        }
        bookmarked = true;
        handler->SendEvent(BuildBookmarkStateEvent(tab_id, url, bookmarked));
        app->ShowBookmarkOverlay();
      }
    }
    NativeRpcSuccessRaw(callback, request, bookmarked ? "true" : "false");
    return true;
  }

  if (request.method == "bookmarks.isBookmarked") {
    if (!HasOnlyParamKeys(request.params, {"url"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    std::string url;
    if (!ReadRequiredString(request.params, "url", &url, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    url = NormalizeBookmarkUrl(url);
    const bool bookmarked =
        !handler->guest_session_active_ && handler->store_ && !url.empty() &&
        handler->store_->IsBookmarked(url);
    NativeRpcSuccessRaw(callback, request, bookmarked ? "true" : "false");
    return true;
  }

  return false;
}

}  // namespace

bool HandleHistoryBookmarksRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler) return false;
  return HandleHistoryRpc(handler, callback, request) ||
         HandleBookmarksRpc(handler, callback, request);
}

}  // namespace otf
