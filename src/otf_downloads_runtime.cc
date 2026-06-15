#include "otf_downloads_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <sys/stat.h>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "include/cef_version.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_native_rpc.h"
#include "otf_store.h"
#include "otf_utils.h"

#ifdef _WIN32
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#ifndef S_IXUSR
#define S_IXUSR 0
#endif

#ifndef S_IXGRP
#define S_IXGRP 0
#endif

#ifndef S_IXOTH
#define S_IXOTH 0
#endif
#endif

namespace otf {
namespace {

#if !defined(_WIN32) && !defined(__APPLE__)
bool HasCommand(const char* command) {
  if (!command || command[0] == '\0') {
    return false;
  }

  if (strchr(command, '/') != nullptr) {
    struct stat buffer;
    return (stat(command, &buffer) == 0) &&
           S_ISREG(buffer.st_mode) &&
           (buffer.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
  }

  const char* path_env = getenv("PATH");
  if (!path_env) {
    return false;
  }

  std::string path_str(path_env);
  size_t start = 0;
  size_t end = path_str.find(':');
  while (end != std::string::npos) {
    std::string dir = path_str.substr(start, end - start);
    if (!dir.empty()) {
      std::string full_path = dir + "/" + command;
      struct stat buffer;
      if (stat(full_path.c_str(), &buffer) == 0 &&
          S_ISREG(buffer.st_mode) &&
          (buffer.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        return true;
      }
    }
    start = end + 1;
    end = path_str.find(':', start);
  }

  std::string dir = path_str.substr(start);
  if (!dir.empty()) {
    std::string full_path = dir + "/" + command;
    struct stat buffer;
    if (stat(full_path.c_str(), &buffer) == 0 &&
        S_ISREG(buffer.st_mode) &&
        (buffer.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
      return true;
    }
  }
  return false;
}
#endif

void WriteToClipboard(const std::string& text) {
#if defined(_WIN32)
  if (OpenClipboard(nullptr)) {
    EmptyClipboard();
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
#else
  FILE* pipe = nullptr;
  if (HasCommand("wl-copy")) {
    pipe = popen("wl-copy", "w");
  } else if (HasCommand("xclip")) {
    pipe = popen("xclip -selection clipboard", "w");
  } else if (HasCommand("xsel")) {
    pipe = popen("xsel --clipboard --input", "w");
  }
  if (pipe) {
    fputs(text.c_str(), pipe);
    pclose(pipe);
  }
#endif
}

#if !defined(_WIN32)
bool SpawnDetached(const char* program, const std::vector<std::string>& args) {
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    setsid();
    pid_t child = fork();
    if (child < 0) _exit(127);
    if (child == 0) {
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) {
          close(devnull);
        }
      }
      std::vector<char*> argv;
      argv.reserve(args.size() + 2);
      argv.push_back(const_cast<char*>(program));
      for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);
      execvp(program, argv.data());
      _exit(127);
    }
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return true;
}
#endif

void OpenPathWithSystemApp(const std::string& path) {
#if defined(_WIN32)
  ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  SpawnDetached("open", {path});
#else
  SpawnDetached("xdg-open", {path});
#endif
}

void RevealPathInFolder(const std::string& path) {
#if defined(_WIN32)
  ShellExecuteA(nullptr, "open",
                std::filesystem::path(path).parent_path().string().c_str(),
                nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  SpawnDetached("open", {"-R", path});
#else
  SpawnDetached("xdg-open",
                {std::filesystem::path(path).parent_path().string()});
#endif
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

std::string DownloadFailureMessage(int reason) {
  switch (reason) {
    case 0: return "";
    case 1: return "File operation failed";
    case 2: return "File access denied";
    case 3: return "No space left on device";
    case 5: return "File name too long";
    case 6: return "File too large";
    case 7: return "File blocked by virus scanner";
    case 10: return "File temporarily unavailable";
    case 11: return "File blocked by policy";
    case 12: return "Security check failed";
    case 13: return "File too short to resume";
    case 14: return "File hash mismatch";
    case 15: return "Source equals destination";
    case 20: return "Network error";
    case 21: return "Connection timed out";
    case 22: return "Connection lost";
    case 23: return "Server unavailable";
    case 24: return "Invalid request";
    case 30: return "Server error";
    case 31: return "Server does not support resume";
    case 33: return "Server returned bad content";
    case 34: return "Unauthorized access";
    case 35: return "Certificate error";
    case 36: return "Access forbidden";
    case 37: return "Server unreachable";
    case 38: return "Content length mismatch";
    case 39: return "Unexpected redirect";
    default: return "Download failed";
  }
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
      .AddBool("canRetry", item.is_interrupted || item.is_canceled)
      .AddInt("failureReason", item.failure_reason)
      .AddString("failureMessage", DownloadFailureMessage(item.failure_reason))
      .AddRaw("endedAt", std::to_string(item.ended_at))
      .Build();
}

std::string BuildDownloadsJson(
    const std::map<int, OtfHandler::DownloadState>& downloads) {
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

int FindDownloadRecordId(
    const std::map<int, OtfHandler::DownloadState>& downloads,
    const std::string& url,
    const std::string& full_path) {
  for (const auto& [id, item] : downloads) {
    if (item.url == url && item.full_path == full_path) {
      return id;
    }
  }
  return -1;
}

std::string ExtractDownloadName(const std::string& url) {
  std::string::size_type q = url.find_first_of("?#");
  std::string path = q != std::string::npos ? url.substr(0, q) : url;
  std::string::size_type s = path.rfind('/');
  if (s == std::string::npos) return {};
  std::string name = path.substr(s + 1);
  if (name.empty()) return {};
  std::string decoded;
  decoded.reserve(name.size());
  for (std::string::size_type i = 0; i < name.size(); ++i) {
    if (name[i] == '%' && i + 2 < name.size()) {
      char hi = name[i + 1];
      char lo = name[i + 2];
      int h = (hi >= '0' && hi <= '9') ? (hi - '0')
              : (hi >= 'A' && hi <= 'F') ? (hi - 'A' + 10)
              : (hi >= 'a' && hi <= 'f') ? (hi - 'a' + 10)
                                         : -1;
      int l = (lo >= '0' && lo <= '9') ? (lo - '0')
              : (lo >= 'A' && lo <= 'F') ? (lo - 'A' + 10)
              : (lo >= 'a' && lo <= 'f') ? (lo - 'a' + 10)
                                         : -1;
      if (h >= 0 && l >= 0) {
        decoded += static_cast<char>((h << 4) | l);
        i += 2;
      } else {
        decoded += name[i];
      }
    } else {
      decoded += name[i];
    }
  }
  return decoded;
}

}  // namespace

bool OtfHandler::CanDownload(CefRefPtr<CefBrowser> browser,
                             const CefString& url,
                             const CefString& request_method) {
  CEF_REQUIRE_UI_THREAD();
  (void)request_method;

  if (!store_) return true;

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && IsNonTabBrowserViewId(view->GetID())) {
    return true;
  }

  CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
  std::string page_origin =
      main_frame ? otf::ExtractOrigin(main_frame->GetURL().ToString()) : "";
  if (page_origin.empty()) return true;

  if (allow_once_downloads_.erase(page_origin) > 0) {
    return true;
  }

  std::string setting =
      IsGuestTab(tab_manager_ ? tab_manager_->GetId(browser) : -1)
          ? ""
          : store_->GetSitePermission(page_origin, "downloads");
  if (setting == "block") {
    return false;
  }
  if (setting == "ask" || setting.empty()) {
    const std::string download_url = url.ToString();
    const std::string display_name = ExtractDownloadName(download_url);
    download_ask_pending_url_ = download_url;
    download_ask_pending_origin_ = page_origin;
    download_ask_pending_name_ = display_name;
    download_ask_pending_browser_ = browser;

    if (OtfApp* app = OtfApp::GetInstance()) {
      if (auto* overlay = app->GetPopup("downloadrequest")) {
        overlay->SetRestoreProducer([this]() {
          return JsonObjectBuilder()
              .AddString("url", download_ask_pending_url_)
              .AddString("origin", download_ask_pending_origin_)
              .AddString("name", download_ask_pending_name_)
              .Build();
        });
        overlay->Show();
      }
    }
    return false;
  }

  return true;
}

std::string OtfHandler::GetDownloadsJson() const {
  return BuildDownloadsJson(downloads_);
}

bool OtfHandler::OpenDownloadsPageFromOverlay(CefRefPtr<CefBrowser> browser,
                                              std::string* error) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app) {
    if (error) *error = "App not ready";
    return false;
  }
  app->HideDownloadsOverlay();
  const int parent_id = tab_manager_ && browser ? tab_manager_->GetId(browser)
                                                : -1;
  const int id = app->CreateTab("browser://downloads");
  NotifyNewTab(id, parent_id);
  app->SwitchTab(id);
  return true;
}

bool OtfHandler::ApplyDownloadAction(uint32_t download_id,
                                     const std::string& action,
                                     CefRefPtr<CefBrowser> browser,
                                     std::string* error) {
  if (guest_session_active_) return true;
  auto it = downloads_.find(download_id);
  if (it == downloads_.end()) return true;

  if (action == "open") {
    if (it->second.full_path.empty()) return true;
    const std::string path = it->second.full_path;
    if (otf::IsSupportedImageUrl(path)) {
      OtfApp* app = OtfApp::GetInstance();
      if (!app) {
        if (error) *error = "App not ready";
        return false;
      }
      const std::string name = SanitizeFilename(
          DownloadDisplayName(it->second.suggested_name, it->second.full_path,
                              it->second.url));
      const std::string public_url =
          "browser://image-preview/download/" + std::to_string(download_id) +
          "/" + name;
      const int new_id = app->CreateTab(public_url);
      SetImagePreviewLocalFileForTab(new_id, public_url, path);
      if (tab_manager_) {
        tab_manager_->SetUrl(new_id, public_url);
        tab_manager_->SetTitle(new_id, name);
        tab_manager_->SetSchemeUrl(new_id, "browser://imagepreview");
        tab_manager_->SetImagePreviewMode(new_id, ImagePreviewMode::kDedicated);
      }
      NotifyNewTab(new_id, -1);
      app->SwitchTab(new_id);
      ScheduleImagePreviewPushForTab(new_id);
      ScheduleDelayedImagePreviewPushForTab(new_id, 100);
      ScheduleDelayedImagePreviewPushForTab(new_id, 300);
      return true;
    }
    if (otf::IsSupportedDocumentUrl(path)) {
      OtfApp* app = OtfApp::GetInstance();
      if (!app) {
        if (error) *error = "App not ready";
        return false;
      }
      const std::string name = SanitizeFilename(
          DownloadDisplayName(it->second.suggested_name, it->second.full_path,
                              it->second.url));
      const std::string content_token =
          "download/" + std::to_string(download_id) + "/" + name;
      const std::string content_url =
          "browser://doc-preview/content/" + content_token;
      const std::string public_url =
          "browser://doc-preview/download/" + std::to_string(download_id) +
          "/" + name;
      otf::RegisterDocContent(content_token, path);
      const int new_id = app->CreateTab(public_url);
      SetDocPreviewLocalFileForTab(new_id, public_url, path);
      SetDocPreviewContentUrlForTab(new_id, content_url);
      if (tab_manager_) {
        tab_manager_->SetUrl(new_id, public_url);
        tab_manager_->SetTitle(new_id, name);
        tab_manager_->SetSchemeUrl(new_id, "browser://docpreview");
        tab_manager_->SetDocPreviewMode(new_id, DocPreviewMode::kDedicated);
      }
      NotifyNewTab(new_id, -1);
      app->SwitchTab(new_id);
      return true;
    }
    OpenPathWithSystemApp(path);
    return true;
  }

  if (action == "showInFolder") {
    if (!it->second.full_path.empty()) {
      RevealPathInFolder(it->second.full_path);
    }
    return true;
  }

  if (action == "retry") {
    const std::string retry_url = it->second.original_url.empty()
                                      ? it->second.url
                                      : it->second.original_url;
    if (!retry_url.empty() && browser) {
      browser->GetHost()->StartDownload(retry_url);
    }
    return true;
  }

  if (action == "copyLink") {
    const std::string link = it->second.original_url.empty()
                                 ? it->second.url
                                 : it->second.original_url;
    WriteToClipboard(link);
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->ShowToast("copy", "Download link copied");
    }
    return true;
  }

  if (error) *error = "Unknown download action";
  return false;
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
  const int total_count =
      guest_session_active_ ? 0 : static_cast<int>(downloads_.size());
  if (!guest_session_active_) {
    for (const auto& [id, item] : downloads_) {
      (void)id;
      if (item.is_in_progress && !item.is_complete && !item.is_canceled &&
          !item.is_interrupted) {
        ++active_count;
      }
    }
  }
  SendEvent(JsonObjectBuilder()
                .AddString("key", "downloads-badge")
                .AddInt("value", active_count)
                .AddInt("total", total_count)
                .Build());
}

bool OtfHandler::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!download_item || !callback) {
    return false;
  }

  const std::string resolved_name =
      suggested_name.ToString().empty()
          ? download_item->GetSuggestedFileName().ToString()
          : suggested_name.ToString();
  const std::string target_path = otf::BuildDownloadPath(resolved_name);
  const std::string download_url = download_item->GetOriginalUrl().ToString();
  const std::string origin = otf::ExtractOrigin(
      download_url.empty() ? download_item->GetURL().ToString()
                           : download_url);
  bool is_guest_download = false;
  if (tab_manager_) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view && !IsNonTabBrowserViewId(view->GetID())) {
      is_guest_download = IsGuestTab(view->GetID());
    }
  }

  if (!origin.empty()) {
    allow_once_downloads_.erase(origin);
  }

  if (store_ && !origin.empty() && !is_guest_download) {
    std::string setting = store_->GetSitePermission(origin, "downloads");
    if (setting == "block") {
      return false;
    }
  }

  const uint32_t runtime_id = download_item->GetId();
  int record_id = static_cast<int>(runtime_id);
  if (is_guest_download) {
    record_id = -static_cast<int>(runtime_id);
  } else if (store_ && otf::IsDownloadsEnabled()) {
    const int persisted_id = store_->CreateDownload(
        download_item->GetURL(), download_item->GetOriginalUrl(), target_path,
        resolved_name, "", "starting");
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

void OtfHandler::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser,
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
  state.failure_reason =
      static_cast<int>(download_item->GetInterruptReason());
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

  if (store_ && state.id > 0 && otf::IsDownloadsEnabled()) {
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

}  // namespace otf
