#include "otf_handler.h"
#include "otf_app.h"
#include "otf_bookmark_runtime.h"
#include "otf_browse_runtime.h"
#include "otf_certificate_runtime.h"
#include "otf_context_menu_runtime.h"
#include "otf_doc_preview_runtime.h"
#include "otf_downloads_runtime.h"
#include "otf_find_runtime.h"
#include "otf_history_bookmarks_rpc.h"
#include "otf_image_preview_runtime.h"
#include "otf_keyboard_runtime.h"
#include "otf_keyboard_shortcuts.h"
#include "otf_lifecycle_runtime.h"
#include "otf_message_router_handler.h"
#include "otf_memory_runtime.h"
#include "otf_native_rpc.h"
#include "otf_page_runtime.h"
#include "otf_popup_runtime.h"
#include "otf_request_context_runtime.h"
#include "otf_split_runtime.h"
#include "otf_zoom_runtime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <fstream>
#include <regex>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
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

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_version.h"

#ifndef OTF_VERSION
#define OTF_VERSION "0.0.0-unknown"
#endif
#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/cef_ssl_info.h"
#include "include/cef_values.h"
#include "include/internal/cef_time.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_display.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "include/cef_command_ids.h"
#include "include/cef_urlrequest.h"

namespace otf {

namespace {

OtfHandler* g_instance = nullptr;
const int MENU_ID_OPEN_IN_NEW_TAB = 10001;
const int MENU_ID_SEARCH_SELECTION = 10002;
const int MENU_ID_PREVIEW_IMAGE = 10003;
const int MENU_ID_TAB_CLOSE = 10004;
const int MENU_ID_TAB_CLOSE_OTHERS = 10005;
const int MENU_ID_TAB_NEW = 10006;
const int MENU_ID_TAB_MUTE = 10007;
const int MENU_ID_TAB_UNMUTE = 10008;
const int MENU_ID_COPY_EMAIL = 10009;
const int MENU_ID_TAB_NEW_PRIVATE = 10010;
const int MENU_ID_TAB_PIN = 10011;
const int MENU_ID_TAB_UNPIN = 10012;
const int MENU_ID_PREVIEW_DOC = 10013;
const int MENU_ID_PASTE_GO = 10014;
const int MENU_ID_TAB_ADD_TO_SPLIT = 10015;
const int MENU_ID_RELOAD = 10016;
using ::otf::ParseIntStrict;
using ::otf::ParseUint32Strict;
using ::otf::ParseUint64Strict;

std::string GetDataURI(const std::string& data, const std::string& mime_type);
bool IsRestorableWorkspaceTab(const WorkspaceTab& tab);

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

std::string TrimWhitespaceCopy(const std::string& value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

bool IsDangerousSchemeUrl(const std::string& url) {
  static const char* kDangerousSchemes[] = {
      "javascript:", "data:", "file:", "vbscript:", "blob:"};
  for (const char* s : kDangerousSchemes) {
    if (url.rfind(s, 0) == 0) return true;
  }
  return false;
}

std::string BuildSearchSelectionMenuLabel(const std::string& selection_text) {
  constexpr size_t kMaxLabelChars = 80;
  std::string display_text = selection_text;
  for (char& c : display_text) {
    if (c == '\r' || c == '\n' || c == '\t') {
      c = ' ';
    }
  }
  if (display_text.size() > kMaxLabelChars) {
    display_text = display_text.substr(0, kMaxLabelChars);
    display_text += "...";
  }
  return "Search \"" + display_text + "\"";
}

std::string GetDevUiUrl() {
  return CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");
}

#if !defined(_WIN32)
// Spawn a detached child running `program` with the given argv, with stdio
// pointed at /dev/null. Uses fork+execvp directly so no shell is involved —
// quoting bugs in caller-supplied paths cannot become command injection.
// Double-fork pattern leaves no zombie behind.
bool SpawnDetached(const char* program,
                   const std::vector<std::string>& args) {
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    const pid_t grandchild = fork();
    if (grandchild == 0) {
      setsid();
      const int devnull = open("/dev/null", O_RDWR);
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
  waitpid(pid, &status, 0);  // reap intermediate child
  return true;
}
#endif

std::string BuildTabJson(TabManager* tab_manager, OtfStore* store, int tab_id) {
  JsonObjectBuilder builder;
  const std::string url = tab_manager ? tab_manager->GetUrl(tab_id) : "";
  OtfHandler* handler = OtfHandler::GetInstance();
  const bool is_guest_tab =
      handler && tab_manager && handler->IsGuestTab(tab_id);
  builder.AddInt("id", tab_id)
      .AddString("url", url)
      .AddString("title", tab_manager ? tab_manager->GetTitle(tab_id) : "New Tab");
  if (tab_manager) {
    builder.AddInt("zoomPercent", tab_manager->GetZoomPercent(tab_id));
    builder.AddBool("sslError", tab_manager->HasSslError(tab_id));
    builder.AddBool("muted", tab_manager->GetMuted(tab_id));
    builder.AddBool("private", tab_manager->IsPrivate(tab_id));
    builder.AddBool("pinned", tab_manager->IsPinned(tab_id));
    builder.AddBool("guest", is_guest_tab);
    const std::string favicon = tab_manager->GetFaviconUrl(tab_id);
    if (!favicon.empty()) {
      builder.AddString("favicon", favicon);
    }
  }
  builder.AddBool("bookmarked",
                  store && tab_manager && !is_guest_tab && IsPersistableWebUrl(url) &&
                      store->IsBookmarked(NormalizeBookmarkUrl(url)));
  return builder.Build();
}

bool RestartBrowserProcess() {
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::GetGlobalCommandLine();
  if (!command_line) {
    return false;
  }

  const std::string executable_path = otf::GetExecutablePath();
  if (executable_path.empty()) {
    return false;
  }

  CefCommandLine::ArgumentList arguments;
  command_line->GetArguments(arguments);

#if defined(_WIN32)
  auto quote_windows = [](const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
      if (c == '"') {
        out += "\\\"";
      } else {
        out += c;
      }
    }
    out += "\"";
    return out;
  };

  std::string command_line_str = quote_windows(executable_path);
  for (const auto& arg : arguments) {
    command_line_str += " ";
    command_line_str += quote_windows(arg.ToString());
  }

  STARTUPINFOA startup_info;
  ZeroMemory(&startup_info, sizeof(startup_info));
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info;
  ZeroMemory(&process_info, sizeof(process_info));
  std::vector<char> mutable_command_line(command_line_str.begin(),
                                         command_line_str.end());
  mutable_command_line.push_back('\0');
  const BOOL started = CreateProcessA(
      nullptr, mutable_command_line.data(), nullptr, nullptr, FALSE,
      CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup_info, &process_info);
  if (!started) {
    return false;
  }
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return true;
#else
  std::vector<std::string> args;
  args.reserve(arguments.size());
  for (const auto& arg : arguments) {
    args.push_back(arg.ToString());
  }
  return SpawnDetached(executable_path.c_str(), args);
#endif
}

}  // namespace

namespace {

// Returns a data: URI with the specified contents.
std::string GetDataURI(const std::string& data, const std::string& mime_type) {
  return "data:" + mime_type + ";base64," +
         CefBase64Encode(data.data(), data.size()).ToString();
}

std::string TrimTrailingSlash(std::string value) {
  while (value.size() > 1 && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

bool IsDevUiUrl(const std::string& url) {
  const std::string dev_ui_url = TrimTrailingSlash(GetDevUiUrl());
  return !dev_ui_url.empty() &&
         (url == dev_ui_url || url == dev_ui_url + "/" ||
          (url.rfind(dev_ui_url + "/", 0) == 0 &&
           otf::IsInternalUiPagePath(url)));
}

bool IsRestorableWorkspaceTab(const WorkspaceTab& tab) {
  if (tab.is_image_preview) {
    return otf::IsPersistableWebUrl(tab.url);
  }
  if (tab.is_doc_preview) {
    return otf::IsPersistableWebUrl(tab.url);
  }
  return otf::IsPersistableWebUrl(tab.url) &&
         tab.url.rfind("browser://", 0) != 0 &&
         !IsDevUiUrl(tab.url);
}

}  // namespace

bool OtfHandler::RestartBrowser() {
  return RestartBrowserProcess();
}

bool OtfHandler::StartSnipCapture(bool hide_app_menu, std::string* error) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !tab_manager_ || !devtools_bridge_) {
    if (error) *error = "not ready";
    return false;
  }
  const int current_tab_id = app->GetCurrentTabId();
  CefRefPtr<CefBrowser> target = tab_manager_->GetBrowser(current_tab_id);
  if (!target) {
    if (error) *error = "no active tab";
    return false;
  }

  devtools_bridge_->Attach(target);
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("format", "png");
  devtools_bridge_->Execute(
      "Page.captureScreenshot", params,
      [hide_app_menu](bool ok, const std::string& result_json) {
        if (!ok) return;
        OtfApp* app = OtfApp::GetInstance();
        OtfHandler* handler = OtfHandler::GetInstance();
        if (!app || !handler || !handler->snip_preview_browser_) return;
        if (hide_app_menu) app->HideAppMenuOverlay();
        app->ShowSnipPreviewOverlay();
        const std::string js = "window.__otfSetSnipImage(" + result_json + ");";
        handler->snip_preview_browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
      });
  return true;
}

OtfHandler::OtfHandler(bool use_alloy_style)
    : use_alloy_style_(use_alloy_style), is_closing_(false) {
  DCHECK(!g_instance);
  g_instance = this;
  tab_manager_ = nullptr;
  store_ = std::make_unique<OtfStore>();
  if (store_ && store_->IsReady()) {
    const int persisted_active = store_->GetActiveWorkspace();
    if (persisted_active > 0) {
      active_workspace_id_ = persisted_active;
    }
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

void OtfHandler::PersistWorkspaceTabs(int workspace_id) {
  if (!store_ || !tab_manager_ || workspace_id <= 0) return;

  // Guard: in "newtab" startup mode the auto-opened newtab must not clobber
  // the saved session. Skip persist while every live tab is still browser://newtab
  // (or has no URL yet). Self-clears as soon as a real URL appears.
  const auto tab_ids = tab_manager_->GetTabIdsForWorkspace(workspace_id);
  if (startup_session_guard_ && workspace_id == active_workspace_id_) {
    const bool all_newtab = std::all_of(tab_ids.begin(), tab_ids.end(),
        [this](int id) {
          const std::string url = tab_manager_->GetUrl(id);
          return url.empty() || url == "browser://newtab";
        });
    if (all_newtab) return;
    startup_session_guard_ = false;  // real URL present — resume normal persist
  }

  std::vector<WorkspaceTab> snapshot;
  OtfApp* app = OtfApp::GetInstance();
  const int active_tab = app ? app->GetCurrentTabId() : -1;
  for (int tab_id : tab_ids) {
    // Private tabs are ephemeral and must never be written to the session DB.
    if (tab_manager_->IsPrivate(tab_id)) continue;
    WorkspaceTab t;
    t.workspace_id = workspace_id;
    t.is_image_preview =
        tab_manager_->GetImagePreviewMode(tab_id) == ImagePreviewMode::kDedicated;
    t.is_doc_preview =
        tab_manager_->GetDocPreviewMode(tab_id) == DocPreviewMode::kDedicated;
    if (t.is_image_preview) {
      const std::string local_path = GetImagePreviewLocalFileForTab(tab_id);
      t.url = GetImagePreviewUrlForTab(tab_id);
      t.preview_local_path = local_path;
      t.preview_page = GetImagePreviewPageForTab(tab_id);
    } else if (t.is_doc_preview) {
      const std::string local_path = GetDocPreviewLocalFileForTab(tab_id);
      t.url = GetDocPreviewUrlForTab(tab_id);
      t.preview_local_path = local_path;
    } else {
      const std::string scheme_url = tab_manager_->GetSchemeUrl(tab_id);
      if (scheme_url.rfind("browser://", 0) == 0) continue;
      t.url = tab_manager_->GetUrl(tab_id);
    }
    if (!IsRestorableWorkspaceTab(t)) continue;
    t.title = tab_manager_->GetTitle(tab_id);
    t.favicon = tab_manager_->GetFaviconUrl(tab_id);
    t.was_active = (tab_id == active_tab);
    t.pinned = tab_manager_->IsPinned(tab_id);
    snapshot.push_back(t);
  }
  store_->ReplaceWorkspaceTabs(workspace_id, snapshot);
}

void OtfHandler::PersistWorkspaceForTab(int tab_id) {
  if (!tab_manager_) return;
  const int ws = tab_manager_->GetWorkspaceId(tab_id);
  if (ws > 0) PersistWorkspaceTabs(ws);
}

CefRefPtr<CefBrowser> OtfHandler::ResolveSiteDataBrowser(
    CefRefPtr<CefBrowser> requester) {
  CEF_REQUIRE_UI_THREAD();
  int id = tab_manager_ ? tab_manager_->GetId(requester) : -1;
  if (id < 0) {
    // The requester isn't a tracked content tab (e.g. the cleardata popup
    // overlay). Use the active content tab it's overlaying instead.
    OtfApp* app = OtfApp::GetInstance();
    id = app ? app->GetCurrentTabId() : -1;
  }
  CefRefPtr<CefBrowser> resolved =
      (id >= 0 && tab_manager_) ? tab_manager_->GetBrowser(id) : nullptr;
  return resolved ? resolved : requester;
}

std::string OtfHandler::BuildTabsJson() const {
  std::stringstream ss;
  ss << "[";
  const std::vector<int> ids =
      guest_session_active_
          ? tab_manager_->GetTabIdsForWorkspace(0)
          : tab_manager_->GetTabIdsForWorkspace(active_workspace_id_);
  for (size_t i = 0; i < ids.size(); ++i) {
    ss << BuildTabJson(tab_manager_, store_.get(), ids[i]);
    if (i + 1 < ids.size()) ss << ",";
  }
  ss << "]";
  return ss.str();
}

void OtfHandler::NotifyNewTab(int new_tab_id, int parent_tab_id) {
  SendEvent(JsonObjectBuilder()
                .AddString("key", "new-tab")
                .AddRaw("tab", BuildTabJson(tab_manager_, store_.get(), new_tab_id))
                .AddInt("parentTabId", parent_tab_id)
                .Build());
}

void OtfHandler::SendEvent(const std::string& event_json) {
  if (subscription_callback_) {
    subscription_callback_->Success(event_json);
  }
}

void OtfHandler::NotifyMessageRouterBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                                 CefRefPtr<CefFrame> frame) {
  if (message_router_) {
    message_router_->OnBeforeBrowse(browser, frame);
  }
}

void OtfHandler::EnsureMessageRouterInitialized() {
  if (message_router_) {
    return;
  }
  CefMessageRouterConfig config;
  config.js_query_function = "cefQuery";
  config.js_cancel_function = "cefQueryCancel";
  message_router_ = CefMessageRouterBrowserSide::Create(config);
  message_router_->AddHandler(new OtfMessageRouterHandler(), true);
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
  CefRefPtr<CefBrowserView> browser_view = CefBrowserView::GetForBrowser(browser);
  if (browser_view && browser_view->GetID() == kCertificateBrowserViewId) {
    certificate_browser_ = nullptr;
    certificate_subscription_ = nullptr;
  }

  if (browser_list_.empty()) {
    StopMemoryLogging();
    CefQuitMessageLoop();
  }
}

void OtfHandler::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                           TerminationStatus status,
                                           int error_code,
                                           const CefString& error_string) {
  CEF_REQUIRE_UI_THREAD();
  // A dead renderer paints a blank window. Log it (debug.txt) so a GPU/renderer
  // crash is visible without DevTools.
  std::string url;
  if (browser && browser->GetMainFrame()) {
    url = browser->GetMainFrame()->GetURL().ToString();
  }
  LOG(ERROR) << "[otf] RENDER PROCESS TERMINATED: status=" << status
             << " error_code=" << error_code
             << " error=" << error_string.ToString() << " url=" << url;
  otf::DiagLog("RENDER PROCESS TERMINATED: status=" + std::to_string(status) +
               " error_code=" + std::to_string(error_code) +
               " error=" + error_string.ToString() + " url=" + url);
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
  LOG(ERROR) << "[otf] load error: " << std::string(failedUrl) << " — "
             << std::string(errorText) << " (" << errorCode << ")"
             << (frame->IsMain() ? " [main frame]" : " [subframe]");
  otf::DiagLog("LOAD ERROR: " + std::string(failedUrl) + " — " +
               std::string(errorText) + " (" + std::to_string(errorCode) + ")" +
               (frame->IsMain() ? " [main frame]" : " [subframe]"));

  if (frame->IsMain()) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);

    // If this URL was silently upgraded from HTTP to HTTPS by OnBeforeBrowse
    // and the HTTPS version failed, fall back to the insecure-blocked page
    // with the original HTTP URL.
    if (view && tab_manager_) {
      const int tab_id = view->GetID();
      auto it = http_upgraded_urls_.find(tab_id);
      if (it != http_upgraded_urls_.end()) {
        const std::string original_http_url = it->second;
        http_upgraded_urls_.erase(it);
        browser->GetMainFrame()->LoadURL(
            "browser://insecure-blocked?url=" +
            CefURIEncode(original_http_url, true).ToString());
        return;
      }
    }

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

  SanitizeContextMenu(model);
  
  if (!params->GetLinkUrl().empty()) {
    std::string link_url = params->GetLinkUrl().ToString();
    if (link_url.rfind("tab-context-menu:", 0) == 0) {
      std::string tab_id_str = link_url.substr(17);

      if (tab_id_str == "newtab") {
        model->Clear();
        model->AddItem(MENU_ID_TAB_NEW, "New Tab");
        model->AddItem(MENU_ID_TAB_NEW_PRIVATE, "New Private Tab");
        return;
      }

      const auto tab_id_opt = ParseIntStrict(tab_id_str);
      bool is_muted = false;
      bool is_pinned = false;
      if (tab_id_opt && tab_manager_) {
        is_muted = tab_manager_->GetMuted(*tab_id_opt);
        is_pinned = tab_manager_->IsPinned(*tab_id_opt);
      }

      model->Clear();
      model->AddItem(MENU_ID_TAB_NEW, "New Tab");
      model->AddItem(MENU_ID_TAB_NEW_PRIVATE, "New Private Tab");
      model->AddSeparator();
      if (is_muted) {
        model->AddItem(MENU_ID_TAB_UNMUTE, "Unmute Tab");
      } else {
        model->AddItem(MENU_ID_TAB_MUTE, "Mute Tab");
      }
      model->AddSeparator();
      if (is_pinned) {
        model->AddItem(MENU_ID_TAB_UNPIN, "Unpin Tab");
      } else {
        model->AddItem(MENU_ID_TAB_PIN, "Pin Tab");
      }
      model->AddSeparator();
      if (IsSplitActive() && tab_id_opt && !IsSplitTab(*tab_id_opt)) {
        model->AddItem(MENU_ID_TAB_ADD_TO_SPLIT, "Add to Split View");
        model->AddSeparator();
      }
      model->AddItem(MENU_ID_TAB_CLOSE, "Close Tab");
      model->AddItem(MENU_ID_TAB_CLOSE_OTHERS, "Close Other Tabs");
      return;
    }
  }

  if (!params->GetLinkUrl().empty()) {
    std::string link_url = params->GetLinkUrl().ToString();

    if (link_url.rfind("mailto:", 0) == 0) {
      model->InsertItemAt(0, MENU_ID_COPY_EMAIL, "Copy Email ID");
    } else {
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

  const std::string selection_text = params->GetSelectionText().ToString();
  const std::string search_text = TrimWhitespaceCopy(selection_text);
  const std::optional<std::string> search_engine_id = otf::GetCurrentSearchEngineId();
  if (!search_text.empty() && search_engine_id.has_value()) {
    if (model->GetIndexOf(IDC_CONTENT_CONTEXT_SEARCHWEBFOR) >= 0) {
      model->Remove(IDC_CONTENT_CONTEXT_SEARCHWEBFOR);
    }
    if (model->GetIndexOf(IDC_CONTENT_CONTEXT_SEARCHWEBFORNEWTAB) >= 0) {
      model->Remove(IDC_CONTENT_CONTEXT_SEARCHWEBFORNEWTAB);
    }
    model->InsertItemAt(0, MENU_ID_SEARCH_SELECTION, "Search selected text");
    const int search_index = model->GetIndexOf(MENU_ID_SEARCH_SELECTION);
    if (search_index >= 0) {
      model->SetLabelAt(search_index,
                        CefString(BuildSearchSelectionMenuLabel(selection_text)));
    }
  }

  bool is_image_link = false;
  if (!params->GetLinkUrl().empty()) {
    std::string link_url = params->GetLinkUrl().ToString();
    std::string lower_link = link_url;
    std::transform(lower_link.begin(), lower_link.end(), lower_link.begin(), ::tolower);
    
    size_t query_pos = lower_link.find('?');
    if (query_pos != std::string::npos) {
      lower_link = lower_link.substr(0, query_pos);
    }
    size_t hash_pos = lower_link.find('#');
    if (hash_pos != std::string::npos) {
      lower_link = lower_link.substr(0, hash_pos);
    }
    
    const std::string extensions[] = {
        ".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".svg", ".ico", ".avif",
        ".jfif", ".pjpeg", ".pjp", ".apng", ".tiff", ".tif", ".heic", ".heif"
    };
    for (const auto& ext : extensions) {
      if (lower_link.length() >= ext.length() &&
          lower_link.compare(lower_link.length() - ext.length(), ext.length(), ext) == 0) {
        is_image_link = true;
        break;
      }
    }
  }

  if ((params->HasImageContents() && !params->GetSourceUrl().empty()) || is_image_link) {
    model->InsertItemAt(model->GetCount(), MENU_ID_PREVIEW_IMAGE, "Preview Image");
  }

  if (!params->GetLinkUrl().empty()) {
    std::string link_url = params->GetLinkUrl().ToString();
    if (otf::IsSupportedDocumentUrl(link_url)) {
      model->InsertItemAt(model->GetCount(), MENU_ID_PREVIEW_DOC, "Preview Document");
    }
  }

  const bool is_editable = (params->GetTypeFlags() & CM_TYPEFLAG_EDITABLE) != 0;
  if (is_editable) {
    model->AddItem(MENU_ID_PASTE_GO, "Paste and Go");
  }

  if (params->GetLinkUrl().empty() && search_text.empty() &&
      !params->HasImageContents() && !is_editable) {
    model->AddItem(MENU_ID_RELOAD, "Reload");
  }

  if (ui_browser_ && browser->IsSame(ui_browser_) && !is_editable &&
      params->GetLinkUrl().empty() && search_text.empty() &&
      !(params->HasImageContents() && !params->GetSourceUrl().empty())) {
    model->Clear();
  }
}

bool OtfHandler::RunContextMenu(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefContextMenuParams> params,
                                CefRefPtr<CefMenuModel> model,
                                CefRefPtr<CefRunContextMenuCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  (void)browser;
  (void)frame;
  (void)params;
  (void)callback;

  SanitizeContextMenu(model);
  return false;
}

bool OtfHandler::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefRefPtr<CefContextMenuParams> params,
                                      int command_id,
                                      EventFlags event_flags) {
  CEF_REQUIRE_UI_THREAD();
  (void)frame;
  (void)event_flags;
  return HandleContextMenuCommand(this, browser, params, command_id);
}

namespace {

class ImageBlockHandler : public CefResourceRequestHandler {
 public:
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override {
    return RV_CANCEL;
  }

 private:
  IMPLEMENT_REFCOUNTING(ImageBlockHandler);
};

}  // namespace

CefRefPtr<CefResourceRequestHandler>
OtfHandler::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  const CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  const int tab_id =
      (view && !IsNonTabBrowserViewId(view->GetID())) ? view->GetID() : -1;
  if (store_ && !request_initiator.empty()) {
    const std::string raw = request_initiator.ToString();
    if (raw.rfind("browser://", 0) != 0 &&
        raw.rfind("file://", 0) != 0) {
      const std::string page_origin = ExtractOrigin(raw);
      if (!page_origin.empty()) {
        // Track cross-origin resources.
        const std::string resource_url = request->GetURL().ToString();
        const std::string resource_origin = ExtractOrigin(resource_url);
        if (!resource_origin.empty() && page_origin != resource_origin) {
          std::lock_guard<std::mutex> lock(cross_origin_mutex_);
          cross_origin_resources_[page_origin].insert(resource_origin);
        }

        // Image permission check.
        if (!IsGuestTab(tab_id) && request->GetResourceType() == RT_IMAGE &&
            store_->GetSitePermission(page_origin, "images") == "block") {
          return new ImageBlockHandler;
        }
      }
    }
  }

  return nullptr;
}

bool OtfHandler::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
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

namespace {
constexpr size_t kMaxClosedTabs = 25;
}  // namespace

void OtfHandler::CloseTabAndNotify(int tab_id) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app) {
    return;
  }
  if (tab_manager_ && tab_manager_->IsPinned(tab_id)) return;
  const bool closed_split_tab = IsSplitTab(tab_id);
  if (tab_manager_) {
    std::string url = tab_manager_->GetUrl(tab_id);
    const bool is_image_preview =
        tab_manager_->GetImagePreviewMode(tab_id) == ImagePreviewMode::kDedicated;
    const bool is_doc_preview =
        tab_manager_->GetDocPreviewMode(tab_id) == DocPreviewMode::kDedicated;
    // Private tabs must never be resurrectable via reopen-closed-tab — their
    // URL/session is ephemeral and recording it would leak private browsing.
    if (!tab_manager_->IsPrivate(tab_id) &&
        (otf::IsPersistableWebUrl(url) || is_image_preview || is_doc_preview)) {
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
  app->CloseTab(tab_id);
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

} // namespace otf
