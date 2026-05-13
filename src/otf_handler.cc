#include "otf_handler.h"
#include "otf_app.h"
#include "otf_keyboard_shortcuts.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <fstream>
#include "otf_utils.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/cef_ssl_info.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "include/cef_command_ids.h"

// Cross-platform clipboard write — CEF Alloy provides no clipboard API,
// so we use platform-native calls.
static void WriteToClipboard(const std::string& text) {
#if defined(_WIN32)
  if (OpenClipboard(nullptr)) {
    EmptyClipboard();
    // Convert UTF-8 to UTF-16 for Windows Unicode clipboard
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(WCHAR));
    if (hg) {
      WCHAR* buffer = static_cast<WCHAR*>(GlobalLock(hg));
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buffer, len);
      GlobalUnlock(hg);
      SetClipboardData(CF_UNICODETEXT, hg);
    }
    CloseClipboard();
  }
#elif defined(__APPLE__)
  FILE* pipe = popen("pbcopy", "w");
  if (pipe) {
    fputs(text.c_str(), pipe);
    pclose(pipe);
  }
#else  // Linux (X11 / Wayland)
  FILE* pipe = popen("wl-copy", "w"); // Try Wayland first
  if (!pipe) pipe = popen("xclip -selection clipboard", "w");
  if (!pipe) pipe = popen("xsel --clipboard --input", "w");
  if (pipe) {
    fputs(text.c_str(), pipe);
    pclose(pipe);
  }
#endif
}

namespace otf {

namespace {

OtfHandler* g_instance = nullptr;
const int MENU_ID_OPEN_IN_NEW_TAB = 10001;

std::string GetDevUiUrl() {
  return CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");
}

std::string QuoteForShell(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

void OpenPathWithSystemApp(const std::string& path) {
#if defined(_WIN32)
  ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  std::string command = "open " + QuoteForShell(path) + " >/dev/null 2>&1 &";
  const int result = std::system(command.c_str());
  (void)result;
#else
  std::string command = "xdg-open " + QuoteForShell(path) + " >/dev/null 2>&1 &";
  const int result = std::system(command.c_str());
  (void)result;
#endif
}

void RevealPathInFolder(const std::string& path) {
#if defined(_WIN32)
  ShellExecuteA(nullptr, "open", std::filesystem::path(path).parent_path().string().c_str(),
                nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  std::string command =
      "open -R " + QuoteForShell(path) + " >/dev/null 2>&1 &";
  const int result = std::system(command.c_str());
  (void)result;
#else
  std::string command =
      "xdg-open " + QuoteForShell(std::filesystem::path(path).parent_path().string()) +
      " >/dev/null 2>&1 &";
  const int result = std::system(command.c_str());
  (void)result;
#endif
}

std::string BuildTabJson(TabManager* tab_manager, OtfStore* store, int tab_id) {
  JsonObjectBuilder builder;
  const std::string url = tab_manager ? tab_manager->GetUrl(tab_id) : "";
  builder.AddInt("id", tab_id)
      .AddString("url", url)
      .AddString("title", tab_manager ? tab_manager->GetTitle(tab_id) : "New Tab");
  if (tab_manager) {
    builder.AddInt("zoomPercent", tab_manager->GetZoomPercent(tab_id));
    builder.AddBool("sslError", tab_manager->HasSslError(tab_id));
  }
  builder.AddBool("bookmarked",
                  store && IsPersistableWebUrl(url) &&
                      store->IsBookmarked(NormalizeBookmarkUrl(url)));
  return builder.Build();
}

std::string BuildTabPropertyEvent(int tab_id,
                                  const std::string& key,
                                  const std::string& value) {
  return JsonObjectBuilder()
      .AddInt("id", tab_id)
      .AddString("key", key)
      .AddString("value", value)
      .Build();
}

std::string BuildTabPropertyEvent(int tab_id,
                                  const std::string& key,
                                  bool value) {
  return JsonObjectBuilder()
      .AddInt("id", tab_id)
      .AddString("key", key)
      .AddBool("value", value)
      .Build();
}

int ToRoundedZoomPercent(double zoom_level) {
  return static_cast<int>(std::lround(otf::ZoomLevelToPercent(zoom_level)));
}

std::string BuildFindResultEvent(int count,
                                 int active,
                                 int tab_id,
                                 const std::string& text,
                                 bool final_update) {
  JsonObjectBuilder builder;
  builder.AddString("key", "find-result")
      .AddInt("count", count)
      .AddInt("active", active)
      .AddInt("tabId", tab_id)
      .AddBool("final", final_update);
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

std::string BuildZoomUpdateEvent(int tab_id, int zoom_percent) {
  return JsonObjectBuilder()
      .AddString("key", "zoom-restore")
      .AddInt("tabId", tab_id)
      .AddInt("zoomPercent", zoom_percent)
      .Build();
}

std::string BuildDownloadItemJson(const OtfHandler::DownloadState& item) {
  return JsonObjectBuilder()
      .AddInt("id", item.id)
      .AddString("url", item.url)
      .AddString("originalUrl", item.original_url)
      .AddString("suggestedName", item.suggested_name)
      .AddString("fullPath", item.full_path)
      .AddString("status", item.status)
      .AddInt("percent", item.percent)
      .AddRaw("receivedBytes", std::to_string(item.received_bytes))
      .AddRaw("totalBytes", std::to_string(item.total_bytes))
      .AddRaw("speedBytesPerSec", std::to_string(item.speed_bytes_per_sec))
      .AddBool("isInProgress", item.is_in_progress)
      .AddBool("isComplete", item.is_complete)
      .AddBool("isCanceled", item.is_canceled)
      .AddBool("isInterrupted", item.is_interrupted)
      .AddBool("isPaused", item.is_paused)
      .AddBool("canCancel", item.can_cancel)
      .AddBool("canPause", item.can_pause)
      .AddBool("canResume", item.can_resume)
      .AddBool("canOpen", item.can_open)
      .AddBool("canShowInFolder", item.can_show_in_folder)
      .AddRaw("endedAt", std::to_string(item.ended_at))
      .Build();
}

bool IsCertificateErrorCode(cef_errorcode_t error_code) {
  return error_code <= ERR_CERT_COMMON_NAME_INVALID && error_code >= ERR_CERT_END;
}

bool IsSecurityErrorDocumentUrl(const std::string& url) {
  return url.rfind("browser://insecure-blocked", 0) == 0 ||
         url.find("/insecure-blocked.html") != std::string::npos ||
         url.rfind("chrome-error://", 0) == 0 ||
         url.rfind("data:", 0) == 0;
}

bool IsSameSecurityUrl(const std::string& a, const std::string& b) {
  if (a == b) {
    return true;
  }
  if (IsPersistableWebUrl(a) && IsPersistableWebUrl(b)) {
    return NormalizeBookmarkUrl(a) == NormalizeBookmarkUrl(b);
  }
  return false;
}

std::string BuildDownloadsJson(const std::map<int, OtfHandler::DownloadState>& downloads) {
  std::ostringstream out;
  out << "[";
  bool first = true;
  for (auto it = downloads.rbegin(); it != downloads.rend(); ++it) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << BuildDownloadItemJson(it->second);
  }
  out << "]";
  return out.str();
}

std::string DownloadStatusLabel(const OtfHandler::DownloadState& item) {
  if (item.is_complete) return "completed";
  if (item.is_canceled) return "canceled";
  if (item.is_interrupted) return "interrupted";
  if (item.is_paused) return "paused";
  if (item.is_in_progress) return "downloading";
  return "pending";
}

std::string DownloadDisplayName(const std::string& suggested_name,
                                const std::string& full_path,
                                const std::string& url) {
  if (!suggested_name.empty()) {
    return suggested_name;
  }
  if (!full_path.empty()) {
    std::filesystem::path path(full_path);
    if (!path.filename().empty()) {
      return path.filename().string();
    }
  }
  if (!url.empty()) {
    const size_t query_pos = url.find_first_of("?#");
    const std::string trimmed =
        query_pos == std::string::npos ? url : url.substr(0, query_pos);
    const size_t slash_pos = trimmed.find_last_of('/');
    if (slash_pos != std::string::npos && slash_pos + 1 < trimmed.size()) {
      return trimmed.substr(slash_pos + 1);
    }
  }
  return "download";
}

int FindDownloadRecordId(const std::map<int, OtfHandler::DownloadState>& downloads,
                         const std::string& url,
                         const std::string& full_path) {
  for (const auto& [id, item] : downloads) {
    if (item.url == url && item.full_path == full_path) {
      return id;
    }
  }
  return -1;
}

std::string ParseLengthPrefixedField(const std::string& input,
                                     size_t* cursor,
                                     bool* ok) {
  *ok = false;
  if (!cursor || *cursor >= input.size()) {
    return "";
  }
  size_t len_end = input.find(':', *cursor);
  if (len_end == std::string::npos) {
    return "";
  }
  const std::string len_str = input.substr(*cursor, len_end - *cursor);
  char* parse_end = nullptr;
  errno = 0;
  unsigned long len = std::strtoul(len_str.c_str(), &parse_end, 10);
  if (errno != 0 || parse_end == len_str.c_str() || *parse_end != '\0') {
    return "";
  }
  const size_t value_start = len_end + 1;
  if (value_start + len > input.size()) {
    return "";
  }
  *cursor = value_start + len;
  *ok = true;
  return input.substr(value_start, len);
}

struct FindbarFindRequest {
  int tab_id = -1;
  std::string text;
  bool forward = true;
  bool match_case = false;
  bool find_next = false;
};

bool ParseFindbarFindRequest(const std::string& raw_json,
                             FindbarFindRequest* request) {
  if (!request) {
    return false;
  }

  CefRefPtr<CefValue> root =
      CefParseJSON(raw_json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    return false;
  }

  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict || !dict->HasKey("tabId") || !dict->HasKey("text") ||
      !dict->HasKey("forward") || !dict->HasKey("matchCase") ||
      !dict->HasKey("findNext")) {
    return false;
  }

  request->tab_id = dict->GetInt("tabId");
  request->text = dict->GetString("text").ToString();
  request->forward = dict->GetBool("forward");
  request->match_case = dict->GetBool("matchCase");
  request->find_next = dict->GetBool("findNext");
  return request->tab_id >= 0;
}

std::string BuildHistoryJson(const std::vector<HistoryEntry>& items) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (i > 0) {
      out << ",";
    }
    out << JsonObjectBuilder()
               .AddInt("id", item.id)
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
    if (i > 0) {
      out << ",";
    }
    out << JsonObjectBuilder()
               .AddInt("id", item.id)
               .AddString("title", item.title)
               .AddString("url", item.url)
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

// Handle messages from the UI Shell (index.html)
class OtfMessageRouterHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  OtfMessageRouterHandler() {}

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    std::string msg = request.ToString();
    OtfHandler* handler = OtfHandler::GetInstance();
    if (!handler || !handler->tab_manager_) return false;

    if (msg == "get-my-tab-id") {
      CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
      callback->Success(view ? std::to_string(view->GetID()) : "0");
      return true;
    }

    if (msg == "subscribe-events") {
      handler->subscription_callback_ = callback;
      return true;
    }

    if (msg == "findbar-subscribe") {
      handler->findbar_subscription_ = callback;
      return true;
    }

    if (msg == "zoombar-subscribe") {
      handler->zoombar_subscription_ = callback;
      return true;
    }

    if (msg == "downloads-subscribe") {
      handler->downloads_subscription_ = callback;
      callback->Success(JsonObjectBuilder()
                            .AddString("key", "downloads-update")
                            .AddRaw("downloads", handler->GetDownloadsJson())
                            .Build());
      return true;
    }

    if (msg == "get-tabs") {
      std::stringstream ss;
      ss << "[";
      std::vector<int> ids = handler->tab_manager_->GetAllTabIds();
      for (size_t i = 0; i < ids.size(); ++i) {
        ss << BuildTabJson(handler->tab_manager_, handler->store_.get(), ids[i]);
        if (i < ids.size() - 1) ss << ",";
      }
      ss << "]";
      callback->Success(ss.str());
      return true;
    }

    if (msg == "get-active-tab") {
      OtfApp* app = OtfApp::GetInstance();
      callback->Success(app ? std::to_string(app->GetCurrentTabId()) : "-1");
      return true;
    }

    if (msg == "get-downloads") {
      callback->Success(handler->GetDownloadsJson());
      return true;
    }

    if (msg == "get-history") {
      callback->Success(handler->store_ ? BuildHistoryJson(handler->store_->GetHistory()) : "[]");
      return true;
    }

    if (msg == "get-bookmarks") {
      callback->Success(handler->store_ ? BuildBookmarksJson(handler->store_->GetBookmarks()) : "[]");
      return true;
    }

    if (msg == "get-settings") {
      callback->Success(otf::LoadSettingsJson());
      return true;
    }

    if (msg.find("set-settings:") == 0) {
      std::string normalized_json;
      if (otf::SaveSettingsJson(msg.substr(13), &normalized_json)) {
        callback->Success("");
        handler->SendEvent(JsonObjectBuilder()
                               .AddString("key", "settings-changed")
                               .AddRaw("settings", normalized_json)
                               .Build());
      } else {
        callback->Failure(1, "Invalid settings payload");
      }
      return true;
    }

    if (msg.find("delete-history-item:") == 0) {
      if (handler->store_) {
        handler->store_->DeleteHistoryItem(std::stoi(msg.substr(20)));
      }
      callback->Success("");
      return true;
    }

    if (msg == "clear-history") {
      if (handler->store_) {
        handler->store_->ClearHistory();
      }
      callback->Success("");
      return true;
    }

    if (msg == "toggle-bookmark-current") {
      OtfApp* app = OtfApp::GetInstance();
      if (app && handler->tab_manager_ && handler->store_) {
        const int tab_id = app->GetCurrentTabId();
        const std::string url = NormalizeBookmarkUrl(handler->tab_manager_->GetUrl(tab_id));
        if (IsPersistableWebUrl(url)) {
          bool bookmarked = false;
          if (handler->store_->IsBookmarked(url)) {
            handler->store_->RemoveBookmarkByUrl(url);
            callback->Success("false");
          } else {
            handler->store_->AddBookmark(url, handler->tab_manager_->GetTitle(tab_id));
            callback->Success("true");
            bookmarked = true;
          }
          handler->SendEvent(BuildBookmarkStateEvent(tab_id, url, bookmarked));
          return true;
        }
      }
      callback->Success("false");
      return true;
    }

    if (msg.find("is-bookmarked-url:") == 0) {
      const std::string encoded = msg.substr(18);
      const std::string url = NormalizeBookmarkUrl(CefURIDecode(encoded, true, UU_NORMAL).ToString());
      const bool bookmarked = handler->store_ && !url.empty() && handler->store_->IsBookmarked(url);
      callback->Success(bookmarked ? "true" : "false");
      return true;
    }

    if (msg.find("remove-bookmark:") == 0) {
      if (handler->store_) {
        handler->store_->RemoveBookmark(std::stoi(msg.substr(16)));
        handler->SendEvent(BuildBookmarkStateEvent(-1, "", false));
      }
      callback->Success("");
      return true;
    }

    if (msg.find("add-bookmark:") == 0) {
      size_t cursor = 13;
      bool ok = false;
      const std::string url = ParseLengthPrefixedField(msg, &cursor, &ok);
      if (!ok || cursor >= msg.size() || msg[cursor] != ':') {
        callback->Failure(1, "Invalid bookmark payload");
        return true;
      }
      ++cursor;
      const std::string title = ParseLengthPrefixedField(msg, &cursor, &ok);
      if (!ok || !handler->store_ || !IsPersistableWebUrl(url)) {
        callback->Failure(1, "Invalid bookmark payload");
        return true;
      }
      handler->store_->AddBookmark(url, title);
      handler->SendEvent(BuildBookmarkStateEvent(-1, url, true));
      callback->Success("");
      return true;
    }

    if (msg.find("update-bookmark:") == 0) {
      size_t cursor = 16;
      size_t id_end = msg.find(':', cursor);
      if (id_end == std::string::npos) {
        callback->Failure(1, "Invalid bookmark payload");
        return true;
      }
      const int bookmark_id = std::stoi(msg.substr(cursor, id_end - cursor));
      cursor = id_end + 1;
      bool ok = false;
      const std::string url = ParseLengthPrefixedField(msg, &cursor, &ok);
      if (!ok || cursor >= msg.size() || msg[cursor] != ':') {
        callback->Failure(1, "Invalid bookmark payload");
        return true;
      }
      ++cursor;
      const std::string title = ParseLengthPrefixedField(msg, &cursor, &ok);
      if (!ok || !handler->store_ || !IsPersistableWebUrl(url)) {
        callback->Failure(1, "Invalid bookmark payload");
        return true;
      }
      handler->store_->UpdateBookmark(bookmark_id, url, title);
      handler->SendEvent(BuildBookmarkStateEvent(-1, url, true));
      callback->Success("");
      return true;
    }

    if (msg == "back-current") {
      OtfApp* app = OtfApp::GetInstance();
      if (app && handler->tab_manager_) {
        CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(app->GetCurrentTabId());
        if (b && b->CanGoBack()) {
          b->GoBack();
        }
      }
      callback->Success("");
      return true;
    }

    if (msg.find("navigate-current:") == 0) {
      std::string url = msg.substr(17);
      CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
      if (view && handler->tab_manager_) {
        int tab_id = view->GetID();
        if (url.find("browser://") == 0) {
          handler->tab_manager_->SetSchemeUrl(tab_id, url);
        }
        browser->GetMainFrame()->LoadURL(url);
      }
      callback->Success("");
    } else if (msg.find("navigate:") == 0) {
      size_t colon = msg.find(':', 9);
      int tab_id = std::stoi(msg.substr(9, colon - 9));
      std::string url = msg.substr(colon + 1);
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) {
        if (url.find("browser://") == 0) {
          handler->tab_manager_->SetSchemeUrl(tab_id, url);
        }
        b->GetMainFrame()->LoadURL(url);
      }
      callback->Success("");
    } else if (msg == "new-tab:") {
      OtfApp* app = OtfApp::GetInstance();
      if (!app) { callback->Failure(1, "App not ready"); return true; }
      int id = app->CreateTab("browser://newtab");
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "new-tab")
                             .AddRaw("tab", BuildTabJson(handler->tab_manager_, handler->store_.get(), id))
                             .Build());
      app->SwitchTab(id);
      callback->Success(std::to_string(id));
    } else if (msg.find("new-tab:") == 0) {
      std::string url = msg.substr(8);
      OtfApp* app = OtfApp::GetInstance();
      if (!app) { callback->Failure(1, "App not ready"); return true; }
      int id = app->CreateTab(url);
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "new-tab")
                             .AddRaw("tab", BuildTabJson(handler->tab_manager_, handler->store_.get(), id))
                             .Build());
      app->SwitchTab(id);
      callback->Success(std::to_string(id));
    } else if (msg.find("close-tab:") == 0) {
      int tab_id = std::stoi(msg.substr(10));
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->CloseTab(tab_id);
        handler->SendEvent(JsonObjectBuilder()
                               .AddString("key", "tab-closed")
                               .AddInt("id", tab_id)
                               .Build());
      }
      callback->Success("");
    } else if (msg.find("switch-tab:") == 0) {
      int tab_id = std::stoi(msg.substr(11));
      OtfApp* app = OtfApp::GetInstance();
      if (app) app->SwitchTab(tab_id);
      callback->Success("");
    } else if (msg.find("back:") == 0) {
      int tab_id = std::stoi(msg.substr(5));
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) b->GoBack();
      callback->Success("");
    } else if (msg.find("forward:") == 0) {
      int tab_id = std::stoi(msg.substr(8));
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) b->GoForward();
      callback->Success("");
    } else if (msg.find("reload:") == 0) {
      int tab_id = std::stoi(msg.substr(7));
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) b->Reload();
      callback->Success("");
    } else if (msg.find("stop:") == 0) {
      int tab_id = std::stoi(msg.substr(5));
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) b->StopLoad();
      callback->Success("");
    } else if (msg.find("zoom-in:") == 0) {
      int tab_id = std::stoi(msg.substr(8));
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) {
        double zoom = b->GetHost()->GetZoomLevel();
        double next_zoom = otf::ZoomIn(zoom);
        b->GetHost()->SetZoomLevel(next_zoom);
        handler->tab_manager_->SetZoomPercent(tab_id, ToRoundedZoomPercent(next_zoom));
        handler->SendEvent(BuildTabPropertyEvent(
            tab_id, "zoomPercent", std::to_string(handler->tab_manager_->GetZoomPercent(tab_id))));
        if (handler->zoombar_subscription_) {
          handler->zoombar_subscription_->Success(
              BuildZoomUpdateEvent(tab_id, handler->tab_manager_->GetZoomPercent(tab_id)));
        }
      }
      callback->Success("");
    } else if (msg.find("zoom-out:") == 0) {
      int tab_id = std::stoi(msg.substr(9));
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) {
        double zoom = b->GetHost()->GetZoomLevel();
        double next_zoom = otf::ZoomOut(zoom);
        b->GetHost()->SetZoomLevel(next_zoom);
        handler->tab_manager_->SetZoomPercent(tab_id, ToRoundedZoomPercent(next_zoom));
        handler->SendEvent(BuildTabPropertyEvent(
            tab_id, "zoomPercent", std::to_string(handler->tab_manager_->GetZoomPercent(tab_id))));
        if (handler->zoombar_subscription_) {
          handler->zoombar_subscription_->Success(
              BuildZoomUpdateEvent(tab_id, handler->tab_manager_->GetZoomPercent(tab_id)));
        }
      }
      callback->Success("");
    } else if (msg.find("zoom-reset:") == 0) {
      int tab_id = std::stoi(msg.substr(11));
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) {
        double next_zoom = otf::ZoomReset();
        b->GetHost()->SetZoomLevel(next_zoom);
        handler->tab_manager_->SetZoomPercent(tab_id, ToRoundedZoomPercent(next_zoom));
        handler->SendEvent(BuildTabPropertyEvent(
            tab_id, "zoomPercent", std::to_string(handler->tab_manager_->GetZoomPercent(tab_id))));
        if (handler->zoombar_subscription_) {
          handler->zoombar_subscription_->Success(
              BuildZoomUpdateEvent(tab_id, handler->tab_manager_->GetZoomPercent(tab_id)));
        }
      }
      callback->Success("");
    } else if (msg == "toggle-zoombar") {
      OtfApp* app = OtfApp::GetInstance();
      if (app && app->zoombar_overlay_) {
        if (app->zoombar_overlay_->IsVisible()) {
          app->HideZoomBarOverlay();
        } else {
          app->ShowZoomBarOverlay();
        }
      }
      callback->Success("");
    } else if (msg == "hide-zoombar") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideZoomBarOverlay();
      }
      callback->Success("");
    } else if (msg == "toggle-downloadsbar") {
      OtfApp* app = OtfApp::GetInstance();
      if (app && app->downloads_overlay_) {
        if (app->downloads_overlay_->IsVisible()) {
          app->HideDownloadsOverlay();
        } else {
          app->ShowDownloadsOverlay();
        }
      }
      callback->Success("");
    } else if (msg == "hide-downloadsbar") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideDownloadsOverlay();
      }
      callback->Success("");
    } else if (msg == "open-downloads-page") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideDownloadsOverlay();
        int id = app->CreateTab("browser://downloads");
        handler->SendEvent(JsonObjectBuilder()
                      .AddString("key", "new-tab")
                      .AddRaw("tab", BuildTabJson(handler->tab_manager_, handler->store_.get(), id))
                      .Build());
        app->SwitchTab(id);
      }
      callback->Success("");
      return true;
    } else if (msg == "toggle-appmenu") {
      OtfApp* app = OtfApp::GetInstance();
      if (app && app->appmenu_overlay_) {
        if (app->appmenu_overlay_->IsVisible()) {
          app->HideAppMenuOverlay();
        } else {
          app->ShowAppMenuOverlay();
        }
      }
      callback->Success("");
    } else if (msg == "hide-appmenu") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideAppMenuOverlay();
      }
      callback->Success("");
    } else if (msg.find("cancel-download:") == 0) {
      uint32_t download_id = static_cast<uint32_t>(std::stoul(msg.substr(16)));
      auto it = handler->download_callbacks_.find(download_id);
      if (it != handler->download_callbacks_.end()) {
        it->second->Cancel();
      }
      callback->Success("");
    } else if (msg.find("pause-download:") == 0) {
      uint32_t download_id = static_cast<uint32_t>(std::stoul(msg.substr(15)));
      auto it = handler->download_callbacks_.find(download_id);
      if (it != handler->download_callbacks_.end()) {
        it->second->Pause();
      }
      callback->Success("");
    } else if (msg.find("resume-download:") == 0) {
      uint32_t download_id = static_cast<uint32_t>(std::stoul(msg.substr(16)));
      auto it = handler->download_callbacks_.find(download_id);
      if (it != handler->download_callbacks_.end()) {
        it->second->Resume();
      }
      callback->Success("");
    } else if (msg.find("open-download:") == 0) {
      uint32_t download_id = static_cast<uint32_t>(std::stoul(msg.substr(14)));
      auto it = handler->downloads_.find(download_id);
      if (it != handler->downloads_.end() && !it->second.full_path.empty()) {
        OpenPathWithSystemApp(it->second.full_path);
      }
      callback->Success("");
    } else if (msg.find("show-download-in-folder:") == 0) {
      uint32_t download_id = static_cast<uint32_t>(std::stoul(msg.substr(24)));
      auto it = handler->downloads_.find(download_id);
      if (it != handler->downloads_.end() && !it->second.full_path.empty()) {
        RevealPathInFolder(it->second.full_path);
      }
      callback->Success("");
    } else if (msg == "clear-finished-downloads") {
      for (auto it = handler->downloads_.begin(); it != handler->downloads_.end();) {
        if (it->second.is_complete || it->second.is_canceled || it->second.is_interrupted) {
          handler->download_callbacks_.erase(it->first);
          it = handler->downloads_.erase(it);
        } else {
          ++it;
        }
      }
      if (handler->store_) {
        handler->store_->DeleteFinishedDownloads();
      }
      handler->NotifyDownloadsChanged();
      handler->NotifyDownloadBadge();
      callback->Success("");
    } else if (msg.find("find:") == 0) {
      // find:<tab_id>:<text>
      size_t c1 = msg.find(':', 5);
      int tab_id = std::stoi(msg.substr(5, c1 - 5));
      std::string text = msg.substr(c1 + 1);
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) b->GetHost()->Find(text, true, false, false);  // findNext=false: initial search
      callback->Success("");
    } else if (msg.find("stop-find:") == 0) {
      int tab_id = std::stoi(msg.substr(10));
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) b->GetHost()->StopFinding(true);
      callback->Success("");
    } else if (msg.find("findbar-find:") == 0) {
      FindbarFindRequest request;
      if (!ParseFindbarFindRequest(msg.substr(13), &request)) {
        callback->Success("");
        return true;
      }
      OtfApp* app = OtfApp::GetInstance();
      if (!app || !handler->tab_manager_) { callback->Success(""); return true; }
      int tab_id = app->GetCurrentTabId();
      if (tab_id < 0) { callback->Success(""); return true; }
      if (request.tab_id != tab_id) { callback->Success(""); return true; }

      handler->tab_manager_->SetFindVisible(tab_id, true);
      handler->tab_manager_->SetFindText(tab_id, request.text);
      handler->tab_manager_->SetFindCase(tab_id, request.match_case);

      auto b = handler->tab_manager_->GetBrowser(tab_id);
      if (!b) { callback->Success(""); return true; }

      if (request.text.empty()) {
        b->GetHost()->StopFinding(true);
        // Clear counters in UI
        if (handler->findbar_subscription_) {
          handler->findbar_subscription_->Success(
              BuildFindResultEvent(0, 0, tab_id, "", true));
        }
      } else {
        // Track pending so async OnFindResult can correlate and filter
        handler->pending_find_tab_  = tab_id;
        handler->pending_find_text_ = request.text;
        b->GetHost()->Find(request.text, request.forward, request.match_case, request.find_next);
      }
      callback->Success("");
    } else if (msg == "findbar-stop:") {
      OtfApp* app = OtfApp::GetInstance();
      if (app && handler->tab_manager_) {
        int tab_id = app->GetCurrentTabId();
        if (tab_id >= 0) {
          handler->tab_manager_->ClearFindState(tab_id);
          auto b = handler->tab_manager_->GetBrowser(tab_id);
          if (b) b->GetHost()->StopFinding(true);
        }
      }
      handler->pending_find_tab_ = -1;
      handler->pending_find_text_.clear();
      if (handler->findbar_subscription_) {
        handler->findbar_subscription_->Success(
            BuildFindResultEvent(0, 0, -1, "", true));
      }
      callback->Success("");
    } else if (msg == "findbar-close:") {
      OtfApp* app = OtfApp::GetInstance();
      int tab_id = app ? app->GetCurrentTabId() : -1;
      if (app && app->findbar_overlay_) {
        app->findbar_overlay_->SetVisible(false);
        if (handler->tab_manager_) {
          handler->tab_manager_->ClearFindState(tab_id);
          auto b = handler->tab_manager_->GetBrowser(tab_id);
          if (b) b->GetHost()->StopFinding(true);
        }
        if (handler->pending_find_tab_ == tab_id) {
          handler->pending_find_tab_ = -1;
          handler->pending_find_text_.clear();
        }
        app->FocusCurrentTabContent();
      }
      if (handler->findbar_subscription_) {
        handler->findbar_subscription_->Success(
            BuildFindResultEvent(0, 0, -1, "", true));
        handler->findbar_subscription_->Success(BuildFindbarClosedEvent(tab_id));
      }
      callback->Success("");
    } else if (msg == "show-findbar") {
      OtfApp* app = OtfApp::GetInstance();
      if (app && handler->tab_manager_) {
        int tab_id = app->GetCurrentTabId();
        if (tab_id >= 0) {
          handler->tab_manager_->SetFindVisible(tab_id, true);
          app->RestoreFindSessionForTab(tab_id, true);
        }
      }
      callback->Success("");
    } else if (msg == "hide-findbar") {
      OtfApp* app = OtfApp::GetInstance();
      if (app && app->findbar_overlay_) {
        app->findbar_overlay_->SetVisible(false);
        if (handler->tab_manager_) {
          auto b = handler->tab_manager_->GetBrowser(app->GetCurrentTabId());
          if (b) b->GetHost()->StopFinding(true);
        }
      }
      callback->Success("");
    } else if (msg == "focus-ui") {
      if (handler->ui_browser_) {
        handler->ui_browser_->GetHost()->SetFocus(true);
      }
      callback->Success("");
    } else {
      return false;
    }

    return true;
  }
};

// Returns a data: URI with the specified contents.
std::string GetDataURI(const std::string& data, const std::string& mime_type) {
  return "data:" + mime_type + ";base64," +
         CefURIEncode(CefBase64Encode(data.data(), data.size()), false)
             .ToString();
}

}  // namespace

OtfHandler::OtfHandler(bool use_alloy_style)
    : use_alloy_style_(use_alloy_style), is_closing_(false) {
  DCHECK(!g_instance);
  g_instance = this;
  tab_manager_ = nullptr;
  store_ = std::make_unique<OtfStore>();
  if (store_ && store_->IsReady()) {
    for (const auto& item : store_->GetDownloads()) {
      DownloadState state;
      state.id = item.id;
      state.url = item.url;
      state.original_url = item.original_url;
      state.suggested_name = item.filename;
      state.mime_type = item.mime_type;
      state.full_path = item.target_path;
      state.status = item.status;
      state.ended_at = item.ended_at;
      state.received_bytes = item.received_bytes;
      state.total_bytes = item.total_bytes;
      state.is_complete = item.status == "completed";
      state.is_canceled = item.status == "canceled";
      state.is_interrupted = item.status == "interrupted" ||
                             item.status == "downloading" ||
                             item.status == "starting" ||
                             item.status == "paused";
      state.is_paused = false;
      state.is_in_progress = false;
      state.can_cancel = false;
      state.can_pause = false;
      state.can_resume = false;
      state.can_open = state.is_complete && !state.full_path.empty();
      state.can_show_in_folder = !state.full_path.empty();
      downloads_[state.id] = state;
      if (item.status == "downloading" || item.status == "starting" || item.status == "paused") {
        PersistedDownload normalized = item;
        normalized.status = "interrupted";
        if (normalized.ended_at == 0) {
          normalized.ended_at = static_cast<int64_t>(std::time(nullptr));
        }
        store_->UpdateDownload(normalized);
      }
    }
  }
}

OtfHandler::~OtfHandler() {
  g_instance = nullptr;
}

// static
OtfHandler* OtfHandler::GetInstance() {
  return g_instance;
}

bool OtfHandler::CanDownload(CefRefPtr<CefBrowser> browser,
                             const CefString& url,
                             const CefString& request_method) {
  CEF_REQUIRE_UI_THREAD();
  (void)browser;
  (void)request_method;
  return true;
}

std::string OtfHandler::GetDownloadsJson() const {
  return BuildDownloadsJson(downloads_);
}

void OtfHandler::NotifyDownloadsChanged() {
  if (downloads_subscription_) {
    downloads_subscription_->Success(
        JsonObjectBuilder()
            .AddString("key", "downloads-update")
            .AddRaw("downloads", GetDownloadsJson())
            .Build());
  }
}

void OtfHandler::NotifyDownloadBadge() {
  int active_count = 0;
  const int total_count = static_cast<int>(downloads_.size());
  for (const auto& [id, item] : downloads_) {
    if (item.is_in_progress && !item.is_complete && !item.is_canceled &&
        !item.is_interrupted) {
      ++active_count;
    }
  }
  SendEvent(JsonObjectBuilder()
                .AddString("key", "downloads-badge")
                .AddInt("value", active_count)
                .AddInt("total", total_count)
                .Build());
}

void OtfHandler::NotifyBookmarkStateForTab(int tab_id) {
  if (tab_id < 0 || !store_ || !tab_manager_) {
    return;
  }

  const std::string url = tab_manager_->GetUrl(tab_id);
  const bool bookmarked =
      IsPersistableWebUrl(url) && store_->IsBookmarked(NormalizeBookmarkUrl(url));
  SendEvent(BuildBookmarkSyncEvent(tab_id, url, bookmarked));
}

void OtfHandler::SendEvent(const std::string& event_json) {
  if (subscription_callback_) {
    subscription_callback_->Success(event_json);
  }
}

void OtfHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                               const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view) {
    if (tab_manager_) tab_manager_->SetTitle(view->GetID(), title.ToString());
    const std::string url = tab_manager_ ? tab_manager_->GetUrl(view->GetID()) : "";
    if (store_ && IsPersistableWebUrl(url)) {
      store_->UpdateHistoryTitle(url, title.ToString());
    }
    SendEvent(BuildTabPropertyEvent(view->GetID(), "title", title.ToString()));
  }
}

void OtfHandler::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (frame->IsMain()) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view) {
      std::string url_str = url.ToString();
      if (url_str.rfind("browser://insecure-blocked", 0) == 0) {
        return;
      }

      if (tab_manager_) {
        tab_manager_->SetUrl(view->GetID(), url_str);
      }

      if (url_str.find("browser://") == 0) {
        return;
      }

      if (tab_manager_ && tab_manager_->HasSslError(view->GetID()) &&
          !IsSecurityErrorDocumentUrl(url_str) &&
          !IsSameSecurityUrl(url_str, tab_manager_->GetSslErrorUrl(view->GetID()))) {
        tab_manager_->SetSslError(view->GetID(), false);
        SendEvent(JsonObjectBuilder()
                      .AddInt("id", view->GetID())
                      .AddString("key", "sslError")
                      .AddBool("value", false)
                      .Build());
      }

      std::string dev_ui_url = GetDevUiUrl();
      if (!dev_ui_url.empty()) {
        std::string prefix = dev_ui_url + "/";
        if (url_str.find(prefix) == 0) {
          return;
        }
      }

      SendEvent(BuildTabPropertyEvent(view->GetID(), "url", url_str));
      if (store_ && IsPersistableWebUrl(url_str)) {
        SendEvent(BuildBookmarkSyncEvent(
            view->GetID(), url_str,
            store_->IsBookmarked(NormalizeBookmarkUrl(url_str))));
      }
    }
  }
}

void OtfHandler::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                    const std::vector<CefString>& icon_urls) {
  CEF_REQUIRE_UI_THREAD();
  if (icon_urls.empty()) return;

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view) {
    SendEvent(BuildTabPropertyEvent(view->GetID(), "favicon",
                                    icon_urls[0].ToString()));
  }
}

void OtfHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                      bool isLoading,
                                      bool canGoBack,
                                      bool canGoForward) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view) {
    SendEvent(BuildTabPropertyEvent(view->GetID(), "loading", isLoading));
    SendEvent(BuildTabPropertyEvent(view->GetID(), "canGoBack", canGoBack));
    SendEvent(BuildTabPropertyEvent(view->GetID(), "canGoForward", canGoForward));
  }
}

void OtfHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            int httpStatusCode) {
  if (!frame->IsMain()) return;
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (!view || view->GetID() == kUiBrowserViewId) return;
  const int tab_id = view->GetID();

  if (httpStatusCode >= 200 && httpStatusCode < 400 && store_ && tab_manager_) {
    const std::string url = frame->GetURL().ToString();
    CefRefPtr<CefNavigationEntry> entry =
        browser->GetHost() ? browser->GetHost()->GetVisibleNavigationEntry()
                           : nullptr;
    CefRefPtr<CefSSLStatus> ssl_status =
        entry ? entry->GetSSLStatus() : nullptr;
    const bool has_cert_error =
        ssl_status && CefIsCertStatusError(ssl_status->GetCertStatus());
    if (has_cert_error) {
      tab_manager_->SetSslError(tab_id, true);
      tab_manager_->SetSslErrorUrl(tab_id, url);
      SendEvent(JsonObjectBuilder()
                    .AddInt("id", tab_id)
                    .AddString("key", "sslError")
                    .AddBool("value", true)
                    .Build());
    } else if (tab_manager_->HasSslError(tab_id) &&
               url != tab_manager_->GetSslErrorUrl(tab_id) &&
               !IsSecurityErrorDocumentUrl(url)) {
      tab_manager_->SetSslError(tab_id, false);
      SendEvent(JsonObjectBuilder()
                    .AddInt("id", tab_id)
                    .AddString("key", "sslError")
                    .AddBool("value", false)
                    .Build());
    }
    const std::string current = tab_manager_->GetUrl(tab_id);
    if (IsPersistableWebUrl(url) &&
        (current.empty() || current.rfind("browser://", 0) != 0)) {
      store_->RecordVisit(url, tab_manager_->GetTitle(tab_id), "link");
    }
    SendEvent(BuildBookmarkSyncEvent(
        tab_id, url, store_->IsBookmarked(NormalizeBookmarkUrl(url))));
  }

  SendEvent(BuildTabPropertyEvent(tab_id, "load-end", true));
}

void OtfHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  if (!message_router_) {
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    message_router_ = CefMessageRouterBrowserSide::Create(config);
    message_router_->AddHandler(new OtfMessageRouterHandler(), true);
  }

  // Link the browser to the tab model
  CefRefPtr<CefBrowserView> browser_view = CefBrowserView::GetForBrowser(browser);
  if (browser_view) {
    if (browser_view->GetID() == kUiBrowserViewId) {
      ui_browser_ = browser;
    } else if (browser_view->GetID() == kFindBarBrowserViewId ||
               browser_view->GetID() == kZoomBarBrowserViewId ||
               browser_view->GetID() == kDownloadsBrowserViewId) {
      if (browser_view->GetID() == kFindBarBrowserViewId) {
        findbar_browser_ = browser;
      }
    } else if (tab_manager_) {
      int tab_id = browser_view->GetID();
      tab_manager_->SetBrowser(tab_id, browser);
      tab_manager_->SetZoomPercent(
          tab_id, ToRoundedZoomPercent(browser->GetHost()->GetZoomLevel()));
      std::string current = browser->GetMainFrame()->GetURL().ToString();
      if (current.empty() || current == "about:blank") {
        std::string stored = tab_manager_->GetUrl(tab_id);
        if (!stored.empty()) {
          browser->GetMainFrame()->LoadURL(stored);
        }
      }
    }
  }

  browser_list_.push_back(browser);
}

bool OtfHandler::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (browser_list_.size() == 1) {
    is_closing_ = true;
  }
  return false;
}

void OtfHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  BrowserList::iterator bit = browser_list_.begin();
  for (; bit != browser_list_.end(); ++bit) {
    if ((*bit)->IsSame(browser)) {
      browser_list_.erase(bit);
      break;
    }
  }

  if (browser_list_.empty()) {
    CefQuitMessageLoop();
  }
}

void OtfHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             ErrorCode errorCode,
                             const CefString& errorText,
                             const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();
  if (errorCode == ERR_ABORTED) {
    return;
  }
  if (frame->IsMain()) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view && tab_manager_ && IsCertificateErrorCode(errorCode)) {
      const int tab_id = view->GetID();
      const std::string failed_url = failedUrl.ToString();
      tab_manager_->SetUrl(tab_id, failed_url);
      tab_manager_->SetSslError(tab_id, true);
      tab_manager_->SetSslErrorUrl(tab_id, failed_url);
      SendEvent(JsonObjectBuilder()
                    .AddInt("id", tab_id)
                    .AddString("key", "url")
                    .AddString("value", failed_url)
                    .Build());
      SendEvent(JsonObjectBuilder()
                    .AddInt("id", tab_id)
                    .AddString("key", "sslError")
                    .AddBool("value", true)
                    .Build());
    }
  }
  std::stringstream ss;
  ss << "<html><body bgcolor=\"white\">"
        "<h2>Failed to load URL "
     << std::string(failedUrl) << " with error " << std::string(errorText)
     << " (" << errorCode << ").</h2></body></html>";
  frame->LoadURL(GetDataURI(ss.str(), "text/html"));
}

bool OtfHandler::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefDownloadItem> download_item,
                                  const CefString& suggested_name,
                                  CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  (void)browser;
  if (!download_item || !callback) {
    return false;
  }

  const std::string resolved_name = suggested_name.ToString().empty()
                                        ? download_item->GetSuggestedFileName().ToString()
                                        : suggested_name.ToString();
  const std::string target_path = BuildDownloadPath(resolved_name);

  const uint32_t runtime_id = download_item->GetId();
  int record_id = static_cast<int>(runtime_id);
  if (store_) {
    const int persisted_id = store_->CreateDownload(download_item->GetURL(),
                                                     download_item->GetOriginalUrl(),
                                                     target_path, resolved_name, "", "starting");
    if (persisted_id > 0) {
      record_id = persisted_id;
    }
  }
  DownloadState& state = downloads_[record_id];
  state.id = record_id;
  state.runtime_id = runtime_id;
  state.url = download_item->GetURL();
  state.original_url = download_item->GetOriginalUrl();
  state.suggested_name = resolved_name;
  state.full_path = target_path;
  state.mime_type.clear();
  state.status = "starting";
  state.can_cancel = true;
  state.can_pause = true;
  state.can_resume = false;
  state.can_open = false;
  state.can_show_in_folder = false;
  runtime_download_ids_[runtime_id] = record_id;
  callback->Continue(state.full_path, false);
  NotifyDownloadsChanged();
  NotifyDownloadBadge();
  return true;
}

void OtfHandler::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefDownloadItem> download_item,
                                   CefRefPtr<CefDownloadItemCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!download_item) {
    return;
  }

  const uint32_t runtime_id = download_item->GetId();
  int record_id = static_cast<int>(runtime_id);
  auto runtime_it = runtime_download_ids_.find(runtime_id);
  if (runtime_it != runtime_download_ids_.end()) {
    record_id = runtime_it->second;
  } else {
    const int matched_id = FindDownloadRecordId(
        downloads_, download_item->GetURL().ToString(),
        download_item->GetFullPath().ToString());
    if (matched_id > 0) {
      record_id = matched_id;
    } else {
      return;
    }
  }
  DownloadState& state = downloads_[record_id];
  state.id = record_id;
  state.runtime_id = runtime_id;
  state.url = download_item->GetURL();
  state.original_url = download_item->GetOriginalUrl();
  state.suggested_name = DownloadDisplayName(
      download_item->GetSuggestedFileName().ToString(),
      download_item->GetFullPath().ToString(),
      download_item->GetURL().ToString());
  state.mime_type = download_item->GetMimeType().ToString();
  state.full_path = download_item->GetFullPath();
  state.percent = download_item->GetPercentComplete();
  state.received_bytes = download_item->GetReceivedBytes();
  state.total_bytes = download_item->GetTotalBytes();
  state.speed_bytes_per_sec = download_item->GetCurrentSpeed();
  state.is_in_progress = download_item->IsInProgress();
  state.is_complete = download_item->IsComplete();
  state.is_canceled = download_item->IsCanceled();
  state.is_interrupted = download_item->IsInterrupted();
#if CEF_API_ADDED(14400)
  state.is_paused = download_item->IsPaused();
#else
  state.is_paused = false;
#endif
  state.can_cancel = state.is_in_progress;
#if CEF_API_ADDED(14400)
  state.can_pause = state.is_in_progress && !state.is_paused;
  state.can_resume = state.is_paused;
#else
  state.can_pause = false;
  state.can_resume = false;
#endif
  state.can_open = state.is_complete && !state.full_path.empty();
  state.can_show_in_folder = !state.full_path.empty();
  state.status = DownloadStatusLabel(state);
  if (state.is_complete || state.is_canceled || state.is_interrupted) {
    state.is_in_progress = false;
    state.can_cancel = false;
    state.can_pause = false;
    state.can_resume = false;
    if (state.ended_at == 0) {
      state.ended_at = static_cast<int64_t>(std::time(nullptr));
    }
  }

  if (callback) {
    download_callbacks_[record_id] = callback;
  }

  if (!state.is_in_progress) {
    download_callbacks_.erase(record_id);
  }

  if (store_ && state.id > 0) {
    PersistedDownload item;
    item.id = state.id;
    item.url = state.url;
    item.original_url = state.original_url;
    item.target_path = state.full_path;
    item.filename = state.suggested_name;
    item.mime_type = state.mime_type;
    item.total_bytes = state.total_bytes;
    item.received_bytes = state.received_bytes;
    item.status = state.status;
    item.ended_at = state.ended_at;
    store_->UpdateDownload(item);
  }

  NotifyDownloadsChanged();
  NotifyDownloadBadge();
}

void OtfHandler::CloseAllBrowsers(bool force_close) {
  if (!CefCurrentlyOn(TID_UI)) {
    CefPostTask(TID_UI, base::BindOnce(&OtfHandler::CloseAllBrowsers, this,
                                       force_close));
    return;
  }

  if (browser_list_.empty()) {
    return;
  }

  BrowserList::iterator it = browser_list_.begin();
  for (; it != browser_list_.end(); ++it) {
    (*it)->GetHost()->CloseBrowser(force_close);
  }
}

bool OtfHandler::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          CefProcessId source_process,
                                          CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  return message_router_->OnProcessMessageReceived(browser, frame,
                                                  source_process, message);
}

void OtfHandler::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefContextMenuParams> params,
                                     CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  
  if (!params->GetLinkUrl().empty()) {
    // Remove Chromium's default items that don't fit our tabbed architecture
    model->Remove(IDC_CONTENT_CONTEXT_OPENLINKNEWTAB);
    model->Remove(IDC_CONTENT_CONTEXT_OPENLINKNEWWINDOW);
    
    // Insert our custom background-tab opener at the top
    model->InsertItemAt(0, MENU_ID_OPEN_IN_NEW_TAB, "Open in new tab");
    
    // CEF Alloy does not add IDC_CONTENT_CONTEXT_COPYLINKLOCATION to the
    // default link context menu. Add it only if it isn't already present
    // (safe: we never remove then re-insert it, so routing stays intact).
    if (model->GetIndexOf(IDC_CONTENT_CONTEXT_COPYLINKLOCATION) < 0) {
      model->InsertItemAt(1, IDC_CONTENT_CONTEXT_COPYLINKLOCATION, "Copy link address");
    }
  }
}

bool OtfHandler::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefRefPtr<CefContextMenuParams> params,
                                      int command_id,
                                      EventFlags event_flags) {
  CEF_REQUIRE_UI_THREAD();
  
  if (command_id == MENU_ID_OPEN_IN_NEW_TAB) {
    std::string url = params->GetLinkUrl().ToString();
    OtfApp* app = OtfApp::GetInstance();
    if (!app || !tab_manager_) return false;
    int new_id = app->CreateTab(url);
    if (url.rfind("browser://", 0) == 0) {
      tab_manager_->SetSchemeUrl(new_id, url);
    }
    SendEvent(JsonObjectBuilder()
                  .AddString("key", "new-tab")
                  .AddRaw("tab", BuildTabJson(tab_manager_, store_.get(), new_id))
                  .Build());

    return true;
  }

  if (command_id == IDC_CONTENT_CONTEXT_COPYLINKLOCATION) {
    // CEF Alloy does not route this command to a native handler, so we
    // handle it ourselves using the platform clipboard API.
    WriteToClipboard(params->GetLinkUrl().ToString());
    return true;
  }

  // Return false for all unhandled commands — CEF will execute built-in commands
  // like IDC_CONTENT_CONTEXT_COPYLINKLOCATION natively (clipboard copy).
  return false;
}



bool OtfHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefRequest> request,
                                 bool user_gesture,
                                 bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();

  if (frame->IsMain()) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view && tab_manager_) {
      const std::string current_url = tab_manager_->GetUrl(view->GetID());
      const std::string next_url = request->GetURL().ToString();
      const std::string ssl_error_url =
          tab_manager_->GetSslErrorUrl(view->GetID());
      const bool is_real_navigation =
          IsPersistableWebUrl(next_url) ||
          (next_url.rfind("browser://", 0) == 0 &&
           !IsSecurityErrorDocumentUrl(next_url));
      if (tab_manager_->HasSslError(view->GetID()) && current_url != next_url &&
          next_url != ssl_error_url && is_real_navigation) {
        tab_manager_->SetSslError(view->GetID(), false);
        SendEvent(JsonObjectBuilder()
                      .AddInt("id", view->GetID())
                      .AddString("key", "sslError")
                      .AddBool("value", false)
                      .Build());
      }
    }
  }

  message_router_->OnBeforeBrowse(browser, frame);

  std::string url = request->GetURL().ToString();

  const bool is_main_frame = !frame || frame->IsMain();

  // Chromium features such as the built-in PDF viewer can load internal
  // extension/untrusted subframes. Keep blocking direct top-level navigation.
  if ((url.rfind("chrome-extension://", 0) == 0 ||
       url.rfind("chrome-untrusted://", 0) == 0) &&
      !is_main_frame) {
    return false;
  }

  // Block dangerous and internal schemes for top-level/user navigation.
  if (url.rfind("chrome://", 0) == 0 ||
      url.rfind("chrome-devtools://", 0) == 0 ||
      url.rfind("chrome-extension://", 0) == 0 ||
      url.rfind("chrome-search://", 0) == 0 ||
      url.rfind("chrome-untrusted://", 0) == 0 ||
      url.rfind("devtools://", 0) == 0 ||
      url.rfind("javascript:", 0) == 0 ||
      url.rfind("data:", 0) == 0 ||
      url.rfind("blob:", 0) == 0 ||
      url.rfind("about:srcdoc", 0) == 0) {
    return true;
  }

  // Block file:// for all browsers except the UI shell
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (url.rfind("file://", 0) == 0 &&
      (!view || view->GetID() != kUiBrowserViewId)) {
    return true;
  }

  if (is_main_frame && url.rfind("http://", 0) == 0 &&
      !IsAllowedHttpUrl(url)) {
    if (view && tab_manager_) {
      const int tab_id = view->GetID();
      tab_manager_->SetUrl(tab_id, url);
      tab_manager_->SetSslError(tab_id, true);
      SendEvent(JsonObjectBuilder()
                    .AddInt("id", tab_id)
                    .AddString("key", "sslError")
                    .AddBool("value", true)
                    .Build());
      SendEvent(JsonObjectBuilder()
                    .AddInt("id", tab_id)
                    .AddString("key", "url")
                    .AddString("value", url)
                    .Build());
    }
    browser->GetMainFrame()->LoadURL("browser://insecure-blocked?url=" +
                                     CefURIEncode(url, true).ToString());
    return true;
  }

  std::string dev_ui_url = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");

  if (!dev_ui_url.empty() && IsAllowedBrowserPageUrl(url)) {
    std::string transformed = GetBrowserPageDevUrl(dev_ui_url, url);
    request->SetURL(transformed);
  }

  return false;
}

bool OtfHandler::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                               const CefKeyEvent& event,
                               CefEventHandle os_event,
                               bool* is_keyboard_shortcut) {
  if (event.type == KEYEVENT_KEYUP || event.type == KEYEVENT_CHAR) return false;

  const uint32_t m = Mod::Of(event.modifiers);
  const int    key = event.windows_key_code;
  auto const   M   = [=](uint32_t mod, int k) { return m == mod && key == k; };

  OtfApp* const app = OtfApp::GetInstance();
  if (!app || !tab_manager_) return false;
  const int cur = app->GetCurrentTabId();

  auto nav = [&](const char* action) {
    auto b = tab_manager_->GetBrowser(cur);
    if (!b) return;
    if (action == Shortcut::kBack)      b->GoBack();
    if (action == Shortcut::kForward)   b->GoForward();
    if (action == Shortcut::kReload)    b->Reload();
    if (action == Shortcut::kEscape)    b->StopLoad();
    if (action == Shortcut::kZoomIn || action == Shortcut::kZoomOut ||
        action == Shortcut::kZoomReset) {
      double next_zoom = b->GetHost()->GetZoomLevel();
      if (action == Shortcut::kZoomIn) {
        next_zoom = otf::ZoomIn(next_zoom);
      } else if (action == Shortcut::kZoomOut) {
        next_zoom = otf::ZoomOut(next_zoom);
      } else {
        next_zoom = otf::ZoomReset();
      }
      b->GetHost()->SetZoomLevel(next_zoom);
      tab_manager_->SetZoomPercent(cur, ToRoundedZoomPercent(next_zoom));
      SendEvent(BuildTabPropertyEvent(
          cur, "zoomPercent", std::to_string(tab_manager_->GetZoomPercent(cur))));
      if (zoombar_subscription_) {
        zoombar_subscription_->Success(
            BuildZoomUpdateEvent(cur, tab_manager_->GetZoomPercent(cur)));
      }
    }
  };

  // ── Navigation & Page ─────────────────────────────────────
  if (M(Mod::kAlt,  Key::kLeft))   { nav(Shortcut::kBack);    return true; }
  if (M(Mod::kAlt,  Key::kRight))  { nav(Shortcut::kForward); return true; }
  if (M(Mod::kNone, Key::kF5) || M(Mod::kCtrl, Key::kR)) {
    *is_keyboard_shortcut = true; nav(Shortcut::kReload); return true;
  }
  if (M(Mod::kNone, Key::kEscape)) {
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
    if (app->findbar_overlay_ && app->findbar_overlay_->IsVisible()) {
      app->findbar_overlay_->SetVisible(false);
      if (tab_manager_) {
        tab_manager_->ClearFindState(app->GetCurrentTabId());
        auto b = tab_manager_->GetBrowser(app->GetCurrentTabId());
        if (b) b->GetHost()->StopFinding(true);
      }
      if (pending_find_tab_ == app->GetCurrentTabId()) {
        pending_find_tab_ = -1;
        pending_find_text_.clear();
      }
      app->FocusCurrentTabContent();
      if (findbar_subscription_) {
        findbar_subscription_->Success(
            BuildFindResultEvent(0, 0, -1, "", true));
        findbar_subscription_->Success(
            BuildFindbarClosedEvent(app->GetCurrentTabId()));
      }
      return true;
    }
    *is_keyboard_shortcut = true; nav(Shortcut::kEscape); return true;
  }
  if (M(Mod::kCtrl, Key::kPlus) ||
      M(Mod::kCtrl|Mod::kShift, Key::kPlus) ||
      M(Mod::kCtrl, Key::kEquals) ||
      M(Mod::kCtrl|Mod::kShift, Key::kEquals) ||
      M(Mod::kCtrl, Key::kNumAdd)) {
    *is_keyboard_shortcut = true;
    nav(Shortcut::kZoomIn);
    return true;
  }
  if (M(Mod::kCtrl, Key::kMinus) || M(Mod::kCtrl, Key::kNumMinus))  {
    *is_keyboard_shortcut = true;
    nav(Shortcut::kZoomOut);
    return true;
  }
  if (M(Mod::kCtrl, Key::k0) || M(Mod::kCtrl, Key::kNum0))      {
    *is_keyboard_shortcut = true;
    nav(Shortcut::kZoomReset);
    return true;
  }
  if (M(Mod::kCtrl, Key::kHome)) {
    auto b = tab_manager_->GetBrowser(cur);
    if (b) {
      b->GetMainFrame()->ExecuteJavaScript(
          "window.scrollTo({ top: 0, behavior: 'auto' });", "", 0);
    }
    return true;
  }
  if (M(Mod::kCtrl, Key::kEnd)) {
    auto b = tab_manager_->GetBrowser(cur);
    if (b) {
      b->GetMainFrame()->ExecuteJavaScript(
          "window.scrollTo({ top: document.documentElement.scrollHeight, behavior: 'auto' });",
          "", 0);
    }
    return true;
  }
  if (M(Mod::kCtrl, Key::kF)) {
    if (app->findbar_overlay_ && tab_manager_) {
      tab_manager_->SetFindVisible(cur, true);
      app->RestoreFindSessionForTab(cur, true);
    }
    return true;
  }
  if (M(Mod::kCtrl, Key::kD)) {
    if (store_ && tab_manager_) {
      const std::string url = NormalizeBookmarkUrl(tab_manager_->GetUrl(cur));
      if (IsPersistableWebUrl(url)) {
        bool bookmarked = false;
        if (store_->IsBookmarked(url)) {
          store_->RemoveBookmarkByUrl(url);
        } else {
          store_->AddBookmark(url, tab_manager_->GetTitle(cur));
          bookmarked = true;
        }
        SendEvent(BuildBookmarkStateEvent(cur, url, bookmarked));
      }
    }
    return true;
  }
  if (M(Mod::kCtrl, Key::kG)) {
    if (tab_manager_) {
      std::string text = tab_manager_->GetFindText(cur);
      bool case_sensitive = tab_manager_->GetFindCase(cur);
      if (!text.empty()) {
        auto b = tab_manager_->GetBrowser(cur);
        if (b) b->GetHost()->Find(text, true, case_sensitive, true);
      }
    }
    return true;
  }
  if (M(Mod::kCtrl|Mod::kShift, Key::kG)) {
    if (tab_manager_) {
      std::string text = tab_manager_->GetFindText(cur);
      bool case_sensitive = tab_manager_->GetFindCase(cur);
      if (!text.empty()) {
        auto b = tab_manager_->GetBrowser(cur);
        if (b) b->GetHost()->Find(text, false, case_sensitive, true);
      }
    }
    return true;
  }

  // ── Focus bar (frontend-only action) ──────────────────────
  if (M(Mod::kCtrl, Key::kL) || M(Mod::kNone, Key::kF6)) {
    SendShortcut(this, Shortcut::kFocusBar); return true;
  }
  if (M(Mod::kCtrl, Key::kH)) {
    int id = app->CreateTab("browser://history");
    SendEvent(JsonObjectBuilder()
                  .AddString("key", "new-tab")
                  .AddRaw("tab", BuildTabJson(tab_manager_, store_.get(), id))
                  .Build());
    app->SwitchTab(id);
    return true;
  }
  if (M(Mod::kCtrl, Key::kJ)) {
    int id = app->CreateTab("browser://downloads");
    SendEvent(JsonObjectBuilder()
                  .AddString("key", "new-tab")
                  .AddRaw("tab", BuildTabJson(tab_manager_, store_.get(), id))
                  .Build());
    app->SwitchTab(id);
    return true;
  }

  // ── Tabs (C++ action + frontend notification) ─────────────
  if (M(Mod::kCtrl, Key::kT)) {
    int id = app->CreateTab("browser://newtab");
    SendEvent(JsonObjectBuilder()
                  .AddString("key", "new-tab")
                  .AddRaw("tab", BuildTabJson(tab_manager_, store_.get(), id))
                  .Build());
    app->SwitchTab(id);
    return true;
  }
  if (M(Mod::kCtrl, Key::kW)) {
    std::string url = tab_manager_->GetUrl(cur);
    if (!url.empty() && url.find("browser://") != 0) last_closed_url_ = url;
    app->CloseTab(cur);
    SendEvent(JsonObjectBuilder().AddString("key", "tab-closed").AddInt("id", cur).Build());
    return true;
  }
  if (M(Mod::kCtrl|Mod::kShift, Key::kT)) {
    if (!last_closed_url_.empty()) {
      int id = app->CreateTab(last_closed_url_);
      SendEvent(JsonObjectBuilder()
                    .AddString("key", "new-tab")
                    .AddRaw("tab", BuildTabJson(tab_manager_, store_.get(), id))
                    .Build());
      app->SwitchTab(id);
      last_closed_url_.clear();
    }
    return true;
  }
  if (M(Mod::kCtrl, Key::kTab) || M(Mod::kCtrl|Mod::kShift, Key::kTab)) {
    auto ids = tab_manager_->GetAllTabIds();
    if (ids.size() < 2) return true;
    auto it = std::find(ids.begin(), ids.end(), cur);
    if (it == ids.end()) return true;
    int next = M(Mod::kCtrl, Key::kTab)
      ? (it + 1 != ids.end() ? *(it + 1) : ids.front())
      : (it != ids.begin()     ? *(it - 1) : ids.back());
    app->SwitchTab(next);
    return true;
  }

  return false;
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
      tab_manager_->IsFindVisible(tab_id) && !tab_manager_->GetFindText(tab_id).empty();

  if (!is_background_stop_reset) {
    tab_manager_->SetFindCount(tab_id, count);
    tab_manager_->SetFindActive(tab_id, activeMatchOrdinal);
  }

  // Only forward results for the currently focused tab
  if (!is_current_tab) return;

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
    findbar_subscription_->Success(
        BuildFindResultEvent(count, activeMatchOrdinal, tab_id, "", finalUpdate));
  }
}

bool OtfHandler::OnCertificateError(CefRefPtr<CefBrowser> browser,
                                     ErrorCode cert_error,
                                     const CefString& request_url,
                                     CefRefPtr<CefSSLInfo> ssl_info,
                                     CefRefPtr<CefCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && tab_manager_) {
    int tab_id = view->GetID();
    const std::string url = request_url.ToString();
    tab_manager_->SetSslError(tab_id, true);
    tab_manager_->SetSslErrorUrl(tab_id, url);
    SendEvent(JsonObjectBuilder()
                  .AddInt("id", tab_id)
                  .AddString("key", "sslError")
                  .AddBool("value", true)
                  .Build());
  }
  
  return false;
}

} // namespace otf
