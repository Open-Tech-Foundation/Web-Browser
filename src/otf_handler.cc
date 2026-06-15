#include "otf_handler.h"
#include "otf_app.h"
#include "otf_certificate_runtime.h"
#include "otf_doc_preview_runtime.h"
#include "otf_downloads_runtime.h"
#include "otf_history_bookmarks_rpc.h"
#include "otf_image_preview_runtime.h"
#include "otf_keyboard_shortcuts.h"
#include "otf_message_router_handler.h"
#include "otf_memory_runtime.h"
#include "otf_native_rpc.h"
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

#if !defined(_WIN32) && !defined(__APPLE__)
static bool HasCommand(const char* command) {
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

namespace otf {

namespace {

void SetBrowserWindowVisible(CefRefPtr<CefBrowser> browser, bool visible) {
  if (!browser) return;
  browser->GetHost()->WasHidden(!visible);
}

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
constexpr std::array<int, 4> kBlockedContextMenuCommandIds = {
    IDC_VIEW_SOURCE,
    IDC_CONTENT_CONTEXT_VIEWFRAMESOURCE,
    IDC_CONTENT_CONTEXT_OPENLINKNEWTAB,
    IDC_CONTENT_CONTEXT_OPENLINKNEWWINDOW,
};

using ::otf::ParseIntStrict;
using ::otf::ParseUint32Strict;
using ::otf::ParseUint64Strict;

constexpr int kPopupMinWidth = 320;
constexpr int kPopupMinHeight = 240;
constexpr int kPopupDefaultWidth = 600;
constexpr int kPopupDefaultHeight = 700;

std::string GetDataURI(const std::string& data, const std::string& mime_type);
bool IsRestorableWorkspaceTab(const WorkspaceTab& tab);

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

int ResolveRealTabIdForBrowser(CefRefPtr<CefBrowser> browser,
                               TabManager* tab_manager) {
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && !IsNonTabBrowserViewId(view->GetID())) {
    return view->GetID();
  }
  if (tab_manager) {
    return tab_manager->GetId(browser);
  }
  return -1;
}

class DeferredTabRedirectTask : public CefTask {
 public:
  DeferredTabRedirectTask(const std::string& url, int old_tab_id)
      : url_(url), old_tab_id_(old_tab_id) {}

  void Execute() override {
    OtfApp* app = OtfApp::GetInstance();
    OtfHandler* handler = OtfHandler::GetInstance();
    if (!app || !handler) return;
    // Preserve the redirected tab's private flag so the JS-permission
    // re-open of a private tab stays in the ephemeral context.
    const bool was_private =
        handler->tab_manager_ && handler->tab_manager_->IsPrivate(old_tab_id_);
    int new_id = app->CreateTab(url_, -1, was_private);
    if (new_id < 0) return;
    handler->NotifyNewTab(new_id, -1);
    app->SwitchTab(new_id);
    handler->CloseTabAndNotify(old_tab_id_);
  }

 private:
  std::string url_;
  int old_tab_id_;
  IMPLEMENT_REFCOUNTING(DeferredTabRedirectTask);
};

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

std::string NormalizeMenuLabel(std::string label) {
  std::string normalized;
  normalized.reserve(label.size());
  bool previous_space = false;
  for (char c : label) {
    if (c == '&') {
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!previous_space) {
        normalized.push_back(' ');
        previous_space = true;
      }
      continue;
    }
    normalized.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    previous_space = false;
  }
  return TrimWhitespaceCopy(normalized);
}

bool IsSourceViewMenuItem(int command_id, const std::string& label) {
  if (std::find(kBlockedContextMenuCommandIds.begin(),
                kBlockedContextMenuCommandIds.end(),
                command_id) != kBlockedContextMenuCommandIds.end()) {
    return true;
  }

  const std::string normalized_label = NormalizeMenuLabel(label);
  return normalized_label.find("view") != std::string::npos &&
         normalized_label.find("source") != std::string::npos;
}

bool IsBlockedContextMenuCommand(int command_id) {
  return std::find(kBlockedContextMenuCommandIds.begin(),
                   kBlockedContextMenuCommandIds.end(),
                   command_id) != kBlockedContextMenuCommandIds.end();
}

void RemoveCommandEverywhere(CefRefPtr<CefMenuModel> model, int command_id) {
  if (!model) {
    return;
  }

  for (int index = static_cast<int>(model->GetCount()) - 1; index >= 0; --index) {
    CefRefPtr<CefMenuModel> sub_menu = model->GetSubMenuAt(static_cast<size_t>(index));
    if (sub_menu) {
      RemoveCommandEverywhere(sub_menu, command_id);
    }
    if (model->GetCommandIdAt(static_cast<size_t>(index)) == command_id) {
      model->RemoveAt(static_cast<size_t>(index));
    }
  }
}

void RemoveLabeledSourceItemsEverywhere(CefRefPtr<CefMenuModel> model) {
  if (!model) {
    return;
  }

  for (int index = static_cast<int>(model->GetCount()) - 1; index >= 0; --index) {
    CefRefPtr<CefMenuModel> sub_menu = model->GetSubMenuAt(static_cast<size_t>(index));
    if (sub_menu) {
      RemoveLabeledSourceItemsEverywhere(sub_menu);
    }

    const std::string label =
        model->GetLabelAt(static_cast<size_t>(index)).ToString();
    const int command_id = model->GetCommandIdAt(static_cast<size_t>(index));
    if (IsSourceViewMenuItem(command_id, label)) {
      model->RemoveAt(static_cast<size_t>(index));
    }
  }
}

void SanitizeContextMenu(CefRefPtr<CefMenuModel> model) {
  for (int command_id : kBlockedContextMenuCommandIds) {
    RemoveCommandEverywhere(model, command_id);
  }
  RemoveLabeledSourceItemsEverywhere(model);
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

std::string BuildFindResultEvent(int count,
                                 int active,
                                 int tab_id,
                                 const std::string& text,
                                 bool final_update,
                                 int seq = 0) {
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

static void ApplyJsPermission(CefBrowserSettings& settings,
                              OtfStore* store,
                              const std::string& url) {
  if (!store) return;
  const std::string origin = ExtractOrigin(url);
  if (!origin.empty() &&
      store->GetSitePermission(origin, "javascript") == "block") {
    settings.javascript = STATE_DISABLED;
  }
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

CefRefPtr<CefRequestContext> OtfHandler::GetActiveWorkspaceRequestContext() {
  if (guest_session_active_) {
    return GetGuestRequestContext();
  }
  return GetWorkspaceRequestContext(active_workspace_id_);
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

CefRefPtr<CefRequestContext> OtfHandler::GetWorkspaceRequestContext(int workspace_id) {
  // The default workspace shares the global context so we don't have to
  // migrate existing on-disk state, and so that "no workspaces created"
  // behaves identically to the pre-workspaces build.
  if (workspace_id <= 1) {
    return nullptr;
  }
  auto it = workspace_contexts_.find(workspace_id);
  if (it != workspace_contexts_.end() && it->second) {
    return it->second;
  }
  std::filesystem::path base = otf::GetWorkspaceCefCacheDir(workspace_id);
  if (base.empty()) {
    base = std::filesystem::temp_directory_path() / "otf-browser" / "cache" /
           "cef" / "workspaces" / std::to_string(workspace_id);
  }
  std::error_code ec;
  std::filesystem::create_directories(base, ec);

  CefRequestContextSettings settings;
#if defined(_WIN32)
  CefString(&settings.cache_path) = base.wstring();
#else
  CefString(&settings.cache_path).FromString(base.string());
#endif
  CefRefPtr<CefRequestContext> ctx =
      CefRequestContext::CreateContext(settings, nullptr);
  OtfApp* app = OtfApp::GetInstance();
  if (app) app->RegisterBrowserSchemeForContext(ctx);
  ApplyAlwaysOnPrivacyPreferences(ctx);
  workspace_contexts_[workspace_id] = ctx;
  return ctx;
}

CefRefPtr<CefRequestContext> OtfHandler::GetGuestRequestContext() {
  CEF_REQUIRE_UI_THREAD();
  if (guest_context_) {
    return guest_context_;
  }

  CefRequestContextSettings settings;
  CefRefPtr<CefRequestContext> ctx =
      CefRequestContext::CreateContext(settings, nullptr);
  OtfApp* app = OtfApp::GetInstance();
  if (app) app->RegisterBrowserSchemeForContext(ctx);
  ApplyAlwaysOnPrivacyPreferences(ctx);
  guest_context_ = ctx;
  return ctx;
}

bool OtfHandler::IsGuestTab(int tab_id) const {
  if (tab_id < 0) {
    return guest_session_active_;
  }
  return tab_manager_ && tab_manager_->GetWorkspaceId(tab_id) == 0;
}

void OtfHandler::StartGuestSession() {
  CEF_REQUIRE_UI_THREAD();
  OtfApp* app = OtfApp::GetInstance();
  if (!app) return;
  if (guest_session_active_) {
    const auto guest_tabs =
        tab_manager_ ? tab_manager_->GetTabIdsForWorkspace(0) : std::vector<int>{};
    if (!guest_tabs.empty()) {
      app->SwitchTab(guest_tabs.front());
      return;
    }
  }

  pre_guest_workspace_id_ = active_workspace_id_;
  pre_guest_tab_id_ = app->GetCurrentTabId();
  PersistWorkspaceTabs(pre_guest_workspace_id_);
  if (pre_guest_tab_id_ >= 0) {
    workspace_last_active_tab_[pre_guest_workspace_id_] = pre_guest_tab_id_;
  }
  app->ClearSplitView();

  guest_session_active_ = true;
  const int tab_id = app->CreateTab("browser://newtab");
  if (tab_manager_) tab_manager_->SetWorkspaceId(tab_id, 0);
  NotifyNewTab(tab_id, -1);
  app->SwitchTab(tab_id);
  SendEvent(JsonObjectBuilder()
                .AddString("key", "guest-session-changed")
                .AddBool("active", true)
                .Build());
  SendEvent(JsonObjectBuilder().AddString("key", "workspaces-updated").Build());
}

void OtfHandler::EndGuestSession(bool restore_normal_tabs) {
  CEF_REQUIRE_UI_THREAD();
  if (!guest_session_active_) return;
  OtfApp* app = OtfApp::GetInstance();

  guest_session_active_ = false;
  if (tab_manager_) {
    tab_manager_->ClearWorkspaceOriginZooms(0);
    tab_manager_->ClearPrivateWorkspaceOriginZooms(0);
  }
  for (auto it = downloads_.begin(); it != downloads_.end();) {
    if (it->first < 0) {
      it = downloads_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = runtime_download_ids_.begin(); it != runtime_download_ids_.end();) {
    if (it->second < 0) {
      it = runtime_download_ids_.erase(it);
    } else {
      ++it;
    }
  }
  guest_context_ = nullptr;

  active_workspace_id_ = pre_guest_workspace_id_ > 0 ? pre_guest_workspace_id_ : 1;
  if (store_) store_->SetActiveWorkspace(active_workspace_id_);

  if (restore_normal_tabs) {
    int restore_tab = -1;
    if (tab_manager_ && pre_guest_tab_id_ >= 0) {
      const auto tabs = tab_manager_->GetTabIdsForWorkspace(active_workspace_id_);
      if (std::find(tabs.begin(), tabs.end(), pre_guest_tab_id_) != tabs.end()) {
        restore_tab = pre_guest_tab_id_;
      } else if (!tabs.empty()) {
        restore_tab = tabs.front();
      }
    }
    if (app) {
      if (restore_tab >= 0) {
        app->SwitchTab(restore_tab);
      } else {
        const int new_id = app->CreateTab("browser://newtab");
        NotifyNewTab(new_id, -1);
        app->SwitchTab(new_id);
      }
    }
  }

  pre_guest_tab_id_ = -1;
  SendEvent(JsonObjectBuilder()
                .AddString("key", "guest-session-changed")
                .AddBool("active", false)
                .Build());
  SendEvent(JsonObjectBuilder().AddString("key", "workspaces-updated").Build());
  SendEvent(JsonObjectBuilder()
                .AddString("key", "workspace-changed")
                .AddInt("id", active_workspace_id_)
                .Build());
}

CefRefPtr<CefRequestContext> OtfHandler::GetPrivateRequestContext() {
  CEF_REQUIRE_UI_THREAD();
  if (private_context_) {
    return private_context_;
  }
  // Empty cache_path => in-memory only. Nothing is written to disk and the
  // session is destroyed when the context is released.
  CefRequestContextSettings settings;
  CefRefPtr<CefRequestContext> ctx =
      CefRequestContext::CreateContext(settings, nullptr);
  OtfApp* app = OtfApp::GetInstance();
  if (app) app->RegisterBrowserSchemeForContext(ctx);
  ApplyAlwaysOnPrivacyPreferences(ctx);
  private_context_ = ctx;
  return ctx;
}

void OtfHandler::MaybeReleasePrivateContext() {
  CEF_REQUIRE_UI_THREAD();
  if (private_context_ && tab_manager_ && !tab_manager_->HasPrivateTabs()) {
    private_context_ = nullptr;
    tab_manager_->ClearPrivateOriginZooms();
  }
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

void OtfHandler::ApplyAlwaysOnPrivacyPreferences(
    CefRefPtr<CefRequestContext> ctx) {
  if (!ctx) return;
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefValue> val = CefValue::Create();
  val->SetBool(true);
  CefString error;
  ctx->SetPreference("enable_do_not_track", val, error);
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

void OtfHandler::SendEvent(const std::string& event_json) {
  if (subscription_callback_) {
    subscription_callback_->Success(event_json);
  }
}

void OtfHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                               const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && !IsNonTabBrowserViewId(view->GetID())) {
    if (tab_manager_) tab_manager_->SetTitle(view->GetID(), title.ToString());
    const std::string url = tab_manager_ ? tab_manager_->GetUrl(view->GetID()) : "";
    if (store_ && otf::IsHistoryEnabled() && IsPersistableWebUrl(url) &&
        !IsInternalUiUrl(url) && !IsGuestTab(view->GetID())) {
      const int workspace_id =
          tab_manager_ ? tab_manager_->GetWorkspaceId(view->GetID()) : active_workspace_id_;
      store_->UpdateHistoryTitle(url, title.ToString(), workspace_id);
    }
    SendEvent(BuildTabPropertyEvent(view->GetID(), "title", title.ToString()));
    PersistWorkspaceForTab(view->GetID());
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->UpdateWindowTitle(view->GetID());
    }
  }
}

void OtfHandler::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (frame->IsMain()) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view && !IsNonTabBrowserViewId(view->GetID())) {
      std::string url_str = url.ToString();
      if (url_str.rfind("browser://insecure-blocked", 0) == 0) {
        return;
      }
      if (otf::IsLocalFilesystemPathLike(url_str)) {
        return;
      }


      if (tab_manager_) {
        const bool is_image_preview_url =
            url_str == "browser://imagepreview" ||
            url_str.rfind("browser://image-preview/", 0) == 0 ||
            url_str.find("/imagepreview.html") != std::string::npos;
        const ImagePreviewMode preview_mode =
            tab_manager_->GetImagePreviewMode(view->GetID());
        if (preview_mode == ImagePreviewMode::kDedicated && !is_image_preview_url) {
          tab_manager_->SetSchemeUrl(view->GetID(), "");
          tab_manager_->SetImagePreviewMode(view->GetID(), ImagePreviewMode::kNone);
          SetImagePreviewUrlForTab(view->GetID(), "");
          if (OtfApp* app = OtfApp::GetInstance()) {
            app->HideImagePreviewOverlay();
          }
        }
        if (preview_mode != ImagePreviewMode::kDedicated || !is_image_preview_url) {
          tab_manager_->SetUrl(view->GetID(), url_str);
        }

        const bool is_doc_preview_url =
            url_str == "browser://docpreview" ||
            url_str.rfind("browser://doc-preview/", 0) == 0 ||
            url_str.find("/docpreview.html") != std::string::npos;
        const DocPreviewMode doc_mode =
            tab_manager_->GetDocPreviewMode(view->GetID());
        if (doc_mode == DocPreviewMode::kDedicated && !is_doc_preview_url) {
          tab_manager_->SetSchemeUrl(view->GetID(), "");
          tab_manager_->SetDocPreviewMode(view->GetID(), DocPreviewMode::kNone);
          SetDocPreviewUrlForTab(view->GetID(), "");
          if (OtfApp* app = OtfApp::GetInstance()) {
            app->HideDocPreviewOverlay();
          }
        }
        if (doc_mode != DocPreviewMode::kDedicated || !is_doc_preview_url) {
          tab_manager_->SetUrl(view->GetID(), url_str);
        }
      }

      if (url_str.find("browser://") == 0) {
        return;
      }

      if (tab_manager_) {
        const std::string suppressed_url =
            tab_manager_->GetHistorySuppressedUrl(view->GetID());
        if (!suppressed_url.empty() && url_str != suppressed_url) {
          tab_manager_->SetHistorySuppressedUrl(view->GetID(), "");
        }
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
      // Clear stale favicon when navigating to a different origin.
      if (tab_manager_) {
        const std::string old_url = tab_manager_->GetUrl(view->GetID());
        const std::string old_origin = ExtractOrigin(old_url);
        const std::string new_origin = ExtractOrigin(url_str);
        if (old_origin != new_origin) {
          tab_manager_->SetFaviconUrl(view->GetID(), "");
          SendEvent(BuildTabPropertyEvent(view->GetID(), "favicon", ""));
        }
      }
      if (store_ && IsPersistableWebUrl(url_str)) {
        SendEvent(BuildBookmarkSyncEvent(
            view->GetID(), url_str,
            !IsGuestTab(view->GetID()) &&
                store_->IsBookmarked(NormalizeBookmarkUrl(url_str))));
      }
      PersistWorkspaceForTab(view->GetID());
    }
  }
}

void OtfHandler::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                     const std::vector<CefString>& icon_urls) {
  CEF_REQUIRE_UI_THREAD();

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && !IsNonTabBrowserViewId(view->GetID())) {
    const int tab_id = view->GetID();
    std::string favicon_url;
    if (!icon_urls.empty()) {
      favicon_url = icon_urls[0].ToString();
    }
    if (favicon_url.empty()) return;
    const std::string page_url = tab_manager_ ? NormalizeBookmarkUrl(tab_manager_->GetUrl(tab_id))
                                               : "";
    if (store_ && !page_url.empty() && IsPersistableWebUrl(page_url) &&
        store_->IsBookmarked(page_url)) {
      store_->AddBookmark(page_url, tab_manager_->GetTitle(tab_id), favicon_url);
    }
    if (tab_manager_) {
      tab_manager_->SetFaviconUrl(tab_id, favicon_url);
    }
    SendEvent(BuildTabPropertyEvent(tab_id, "favicon", favicon_url));
    PersistWorkspaceForTab(tab_id);
  }
}

void OtfHandler::OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                                        bool fullscreen) {
  CEF_REQUIRE_UI_THREAD();
  if (auto* app = OtfApp::GetInstance()) {
    app->SetContentFullscreen(fullscreen);
  }
}

void OtfHandler::OnStatusMessage(CefRefPtr<CefBrowser> browser,
                                 const CefString& value) {
  CEF_REQUIRE_UI_THREAD();
  if (!link_preview_browser_) return;
  // Only forward status from content tabs, not the overlay browsers themselves.
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (!view || IsNonTabBrowserViewId(view->GetID())) return;

  std::string url = value.ToString();
  OtfApp* app = OtfApp::GetInstance();
  if (url.empty()) {
    if (app) app->SetLinkPreviewVisible(false);
    return;
  }
  // Escape backslashes and single-quotes for safe embedding in a JS string.
  for (size_t i = 0; (i = url.find('\\', i)) != std::string::npos; i += 2)
    url.replace(i, 1, "\\\\");
  for (size_t i = 0; (i = url.find('\'', i)) != std::string::npos; i += 2)
    url.replace(i, 1, "\\'");
  if (app) app->SetLinkPreviewVisible(true);
  const std::string js = "window.__otfSetLinkPreview('" + url + "');";
  link_preview_browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
}

bool OtfHandler::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                  cef_log_severity_t level,
                                  const CefString& message,
                                  const CefString& source,
                                  int line) {
  CEF_REQUIRE_UI_THREAD();

  // Mirror ALL renderer console output to the CEF log (debug.txt) — including
  // the UI shell and overlay browsers that aren't content tabs — so a blank
  // window or a module/JS failure stays diagnosable when DevTools is
  // unavailable (e.g. command-line flags are blocked).
  LOG(INFO) << "[otf][console:" << level << "] " << message.ToString()
            << " (" << source.ToString() << ":" << line << ")";
  otf::DiagLog("console[" + std::to_string(level) + "]: " + message.ToString() +
               " (" + source.ToString() + ":" + std::to_string(line) + ")");

  if (!tab_manager_) return false;

  // Only capture messages from real content tabs.
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && IsNonTabBrowserViewId(view->GetID())) return false;

  const int tab_id = ResolveRealTabIdForBrowser(browser, tab_manager_);
  if (tab_id < 0) return false;

  // Suppress the ResizeObserver loop warning — it is a browser-internal
  // notification fired when the content panel is resized (e.g. console open/
  // resize) and is not actionable user code output.
  {
    const std::string msg = message.ToString();
    if (msg.find("ResizeObserver loop") != std::string::npos) return false;
  }

  // Timestamp in milliseconds since epoch.
  const int64_t now_ms = static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());

  ConsoleEntry entry{
    static_cast<int>(level),
    message.ToString(),
    source.ToString(),
    line,
    now_ms,
  };
  tab_manager_->AddConsoleEntry(tab_id, entry);

  if (console_subscription_) {
    // Escape JSON strings.
    auto esc = [](const std::string& s) -> std::string {
      std::string out;
      out.reserve(s.size() + 4);
      for (unsigned char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += static_cast<char>(c);
      }
      return out;
    };
    std::string event =
        "{\"key\":\"console-entry\",\"tabId\":" + std::to_string(tab_id) +
        ",\"level\":" + std::to_string(static_cast<int>(level)) +
        ",\"message\":\"" + esc(entry.message) + "\"" +
        ",\"source\":\"" + esc(entry.source) + "\"" +
        ",\"line\":" + std::to_string(line) +
        ",\"ts\":" + std::to_string(now_ms) + "}";
    console_subscription_->Success(event);
  }
  return false;
}

void OtfHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                      bool isLoading,
                                      bool canGoBack,
                                      bool canGoForward) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && view->GetID() == kUiBrowserViewId) {
    LOG(INFO) << "[otf] UI shell OnLoadingStateChange: isLoading="
              << (isLoading ? "true" : "false") << " url="
              << (browser->GetMainFrame()
                      ? browser->GetMainFrame()->GetURL().ToString()
                      : std::string());
  }
  if (view && !IsNonTabBrowserViewId(view->GetID())) {
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
  if (view && view->GetID() == kUiBrowserViewId) {
    LOG(INFO) << "[otf] UI shell OnLoadEnd: httpStatus=" << httpStatusCode
              << " url=" << frame->GetURL().ToString();
    otf::DiagLog("UI shell OnLoadEnd: httpStatus=" +
                 std::to_string(httpStatusCode) + " url=" +
                 frame->GetURL().ToString());
  }
  if (!view || IsNonTabBrowserViewId(view->GetID())) return;
  const int tab_id = view->GetID();

  // Clear any pending HTTP→HTTPS upgrade tracking — the page loaded
  // successfully (or a different navigation superseded the upgrade).
  http_upgraded_urls_.erase(tab_id);

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
    const std::string suppressed_url =
        tab_manager_->GetHistorySuppressedUrl(tab_id);
    const int workspace_id = tab_manager_->GetWorkspaceId(tab_id);
    if (otf::IsHistoryEnabled() && !tab_manager_->IsPrivate(tab_id) &&
        !IsGuestTab(tab_id) &&
        IsPersistableWebUrl(url) &&
        !IsInternalUiUrl(url) && (current.empty() ||
        current.rfind("browser://", 0) != 0) &&
        (suppressed_url.empty() || suppressed_url != url)) {
      store_->RecordVisit(url, tab_manager_->GetTitle(tab_id), "link",
                          workspace_id);
    }
    int zoom_percent = 100;
    if (ApplyPrivateTabZoom(browser, tab_manager_, tab_id, &zoom_percent) ||
        ApplyWorkspaceOriginZoom(browser, tab_manager_, tab_id, &zoom_percent)) {
      SendEvent(BuildTabPropertyEvent(
          tab_id, "zoomPercent", std::to_string(zoom_percent)));
      if (zoombar_subscription_) {
        zoombar_subscription_->Success(
            BuildZoomUpdateEvent(tab_id, zoom_percent));
      }
    } else if (!tab_manager_->IsPrivate(tab_id) && !IsPersistableZoomUrl(url)) {
      browser->GetHost()->SetZoomLevel(otf::ZoomReset());
      tab_manager_->SetZoomPercent(tab_id, 100);
      SendEvent(BuildTabPropertyEvent(tab_id, "zoomPercent", "100"));
      if (zoombar_subscription_) {
        zoombar_subscription_->Success(BuildZoomUpdateEvent(tab_id, 100));
      }
    }
    SendEvent(BuildBookmarkSyncEvent(
        tab_id, url,
        !IsGuestTab(tab_id) &&
            store_->IsBookmarked(NormalizeBookmarkUrl(url))));
  }

  SendEvent(BuildTabPropertyEvent(tab_id, "load-end", true));
}

void OtfHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  {
    CefRefPtr<CefBrowserView> bv = CefBrowserView::GetForBrowser(browser);
    const int vid = bv ? bv->GetID() : -1;
    const std::string u =
        browser->GetMainFrame() ? browser->GetMainFrame()->GetURL().ToString()
                                : std::string();
    LOG(INFO) << "[otf] browser OnAfterCreated: view_id=" << vid
              << (vid == kUiBrowserViewId ? " (UI SHELL)" : "")
              << " url=" << u;
    otf::DiagLog("browser OnAfterCreated: view_id=" + std::to_string(vid) +
                 (vid == kUiBrowserViewId ? " (UI SHELL)" : "") + " url=" + u);
  }

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
      // Lazily build the DevTools bridge and attach it to the UI shell
      // browser. The bridge routes async CDP responses back to per-call
      // callbacks (Storage.getUsageAndQuota and friends).
      if (!devtools_bridge_) {
        devtools_bridge_ = new DevToolsBridge();
      }
      devtools_bridge_->Attach(browser);
    } else if (browser_view->GetID() == kFindBarBrowserViewId ||
               browser_view->GetID() == kZoomBarBrowserViewId ||
               browser_view->GetID() == kDownloadsBrowserViewId ||
               browser_view->GetID() == kCertificateBrowserViewId ||
                browser_view->GetID() == kImagePreviewBrowserViewId ||
                browser_view->GetID() == kLinkPreviewBrowserViewId ||
                browser_view->GetID() == kToastNotificationBrowserViewId ||
                 browser_view->GetID() == kConsoleBrowserViewId ||
                 browser_view->GetID() == kSnipPreviewBrowserViewId) {
      if (browser_view->GetID() == kFindBarBrowserViewId) {
        findbar_browser_ = browser;
      } else if (browser_view->GetID() == kLinkPreviewBrowserViewId) {
        link_preview_browser_ = browser;
      } else if (browser_view->GetID() == kToastNotificationBrowserViewId) {
        toast_browser_ = browser;
      } else if (browser_view->GetID() == kSnipPreviewBrowserViewId) {
        snip_preview_browser_ = browser;
      } else if (browser_view->GetID() == kCertificateBrowserViewId) {
        certificate_browser_ = browser;
        OtfApp* app = OtfApp::GetInstance();
        if (app) {
          app->RefreshCertificateOverlay();
          if (app->certificate_overlay_ && app->certificate_overlay_->IsVisible()) {
            browser->GetHost()->SetFocus(true);
          }
        }
      } else if (browser_view->GetID() == kImagePreviewBrowserViewId) {
        // The floating image preview overlay is not a workspace tab.
      }
    } else if (OtfApp* app = OtfApp::GetInstance();
               app && app->DispatchPopupBrowserCreated(browser_view->GetID(),
                                                      browser)) {
      // Routed to a PopupOverlay (cleardata, etc.). Nothing else to do.
    } else if (pending_external_popups_ > 0) {
      --pending_external_popups_;
    } else if (tab_manager_) {
      int tab_id = browser_view->GetID();
      tab_manager_->SetBrowser(tab_id, browser);
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        bool is_visible = (tab_id == app->GetCurrentTabId());
        if (app->HasSplitView() && (tab_id == app->GetSplitLeftTabId() || tab_id == app->GetSplitRightTabId())) {
          is_visible = true;
        }
        SetBrowserWindowVisible(browser, is_visible);
      }
      int zoom_percent = 100;
      if (!ApplyPrivateTabZoom(browser, tab_manager_, tab_id, &zoom_percent) &&
          !ApplyWorkspaceOriginZoom(browser, tab_manager_, tab_id,
                                    &zoom_percent)) {
        zoom_percent = ToRoundedZoomPercent(browser->GetHost()->GetZoomLevel());
        tab_manager_->SetZoomPercent(tab_id, zoom_percent);
      }
      std::string current = browser->GetMainFrame()->GetURL().ToString();
      if (current.empty() || current == "about:blank") {
        std::string stored = tab_manager_->GetUrl(tab_id);
        if (!stored.empty()) {
          if (tab_manager_->GetImagePreviewMode(tab_id) ==
                  ImagePreviewMode::kDedicated &&
              stored.rfind("browser://image-preview/", 0) != 0) {
            stored = "browser://imagepreview";
          }
          browser->GetMainFrame()->LoadURL(stored);
        }
      }
      // Start periodic memory logging on first content tab
      StartMemoryLogging();
    }
  } else if (pending_external_popups_ > 0) {
    // Browser created via CefBrowserHost::CreateBrowser (no CefBrowserView).
    --pending_external_popups_;
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

  // Empty or dangerous targets are denied. Chromium may strip dangerous
  // javascript:/data: targets down to empty before delivering this callback.
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

  // User activation is required for any popup/tab creation from this hook.
  // Middle/ctrl/cmd-clicks are handled separately by OnOpenURLFromTab().
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

  // Ask mode: block now, remember the exact policy decision for Allow once.
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

  if (OtfApp* a = OtfApp::GetInstance()) {
    if (auto* ov = a->GetPopup("blockedpopup")) {
      int* pid = &popup_ask_pending_id_;
      std::string* purl = &popup_ask_pending_url_;
      std::string* porigin = &popup_ask_pending_origin_;
      ov->SetRestoreProducer([pid, purl, porigin]() {
        return JsonObjectBuilder()
            .AddInt("id", *pid)
            .AddString("url", *purl)
            .AddString("origin", *porigin)
            .Build();
      });
      ov->Show();
    }
  }
  SendEvent(JsonObjectBuilder()
                .AddString("key", "popup-blocked")
                .AddString("origin", origin)
                .AddInt("count", static_cast<int>(pending_popups_.size()))
                .Build());
  return true;
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

  if (IsBlockedContextMenuCommand(command_id)) {
    return true;
  }

  if (command_id == MENU_ID_TAB_CLOSE || command_id == MENU_ID_TAB_CLOSE_OTHERS ||
      command_id == MENU_ID_TAB_MUTE || command_id == MENU_ID_TAB_UNMUTE ||
      command_id == MENU_ID_TAB_PIN || command_id == MENU_ID_TAB_UNPIN ||
      command_id == MENU_ID_TAB_ADD_TO_SPLIT) {
    std::string link_url = params->GetLinkUrl().ToString();
    if (link_url.rfind("tab-context-menu:", 0) == 0) {
      std::string tab_id_str = link_url.substr(17);
      const auto tab_id_opt = ParseIntStrict(tab_id_str);
      if (!tab_id_opt) {
        return true;
      }
      int tab_id = *tab_id_opt;
      if (OtfApp::GetInstance() && tab_manager_) {
        if (command_id == MENU_ID_TAB_CLOSE) {
          CloseTabAndNotify(tab_id);
        } else if (command_id == MENU_ID_TAB_CLOSE_OTHERS) {
          int active_workspace_id = active_workspace_id_;
          std::vector<int> ids = tab_manager_->GetTabIdsForWorkspace(active_workspace_id);
          for (int id : ids) {
            if (id != tab_id && !tab_manager_->IsPinned(id)) {
              CloseTabAndNotify(id);
            }
          }
        } else if (command_id == MENU_ID_TAB_MUTE) {
          CefRefPtr<CefBrowser> b = tab_manager_->GetBrowser(tab_id);
          if (b) {
            b->GetHost()->SetAudioMuted(true);
            tab_manager_->SetMuted(tab_id, true);
            SendEvent(BuildTabPropertyEvent(tab_id, "muted", true));
          }
        } else if (command_id == MENU_ID_TAB_UNMUTE) {
          CefRefPtr<CefBrowser> b = tab_manager_->GetBrowser(tab_id);
          if (b) {
            b->GetHost()->SetAudioMuted(false);
            tab_manager_->SetMuted(tab_id, false);
            SendEvent(BuildTabPropertyEvent(tab_id, "muted", false));
          }
        } else if (command_id == MENU_ID_TAB_PIN) {
          tab_manager_->SetPinned(tab_id, true);
          SendEvent(BuildTabPropertyEvent(tab_id, "pinned", true));
          PersistWorkspaceForTab(tab_id);
        } else if (command_id == MENU_ID_TAB_UNPIN) {
          tab_manager_->SetPinned(tab_id, false);
          SendEvent(BuildTabPropertyEvent(tab_id, "pinned", false));
          PersistWorkspaceForTab(tab_id);
        } else if (command_id == MENU_ID_TAB_ADD_TO_SPLIT) {
          OtfApp* app = OtfApp::GetInstance();
          auto state = GetSplitViewState(active_workspace_id_);
          if (app && state.enabled && !IsSplitTab(tab_id)) {
            const bool left_is_placeholder =
                IsSplitPlaceholderTab(tab_manager_, state.left_tab_id);
            const bool right_is_placeholder =
                IsSplitPlaceholderTab(tab_manager_, state.right_tab_id);
            const bool replace_right =
                right_is_placeholder ? true :
                left_is_placeholder ? false :
                state.active_tab_id != state.right_tab_id;
            const int replaced_tab_id = replace_right ? state.right_tab_id : state.left_tab_id;
            const int next_left = replace_right ? state.left_tab_id : tab_id;
            const int next_right = replace_right ? tab_id : state.right_tab_id;
            app->OpenSplitView(next_left, next_right, tab_id);
            SetSplitViewTabs(active_workspace_id_, next_left, next_right, tab_id);
            app->ActivateSplitPane(tab_id, true);
            if (IsSplitPlaceholderTab(tab_manager_, replaced_tab_id)) {
              CloseTabAndNotify(replaced_tab_id);
            }
            PersistWorkspaceForTab(tab_id);
            NotifySplitStateChanged(active_workspace_id_);
          }
        }
        return true;
      }
    }
  }

  if (command_id == MENU_ID_TAB_NEW) {
    OtfApp* app = OtfApp::GetInstance();
    if (app) {
      int new_id = app->CreateTab("browser://newtab", -1);
      NotifyNewTab(new_id, -1);
      app->SwitchTab(new_id);
      PersistWorkspaceForTab(new_id);
      return true;
    }
  }

  if (command_id == MENU_ID_TAB_NEW_PRIVATE) {
    OtfApp* app = OtfApp::GetInstance();
    if (app) {
      int new_id = app->CreateTab("browser://newtab", -1, true);
      NotifyNewTab(new_id, -1);
      app->SwitchTab(new_id);
      return true;
    }
  }
  
  if (command_id == MENU_ID_OPEN_IN_NEW_TAB) {
    std::string url = params->GetLinkUrl().ToString();
    OtfApp* app = OtfApp::GetInstance();
    if (!app || !tab_manager_) return false;
    int parent_id = tab_manager_->GetId(browser);
    int new_id = app->CreateTab(url, parent_id, tab_manager_->IsPrivate(parent_id));
    if (url.rfind("browser://", 0) == 0) {
      tab_manager_->SetSchemeUrl(new_id, url);
    }
    if (OtfHandler* ui_handler = OtfHandler::GetInstance()) {
      ui_handler->NotifyNewTab(new_id, parent_id);
    }
    app->FocusCurrentTabContent();

    return true;
  }

  if (command_id == MENU_ID_PREVIEW_IMAGE) {
    std::string image_url = params->GetSourceUrl().ToString();
    if (image_url.empty()) {
      image_url = params->GetLinkUrl().ToString();
    }
    if (!image_url.empty()) {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        int tab_id = app->GetCurrentTabId();
        if (tab_manager_) {
          tab_manager_->SetSchemeUrl(tab_id, "");
          tab_manager_->SetImagePreviewMode(tab_id, ImagePreviewMode::kInline);
        }
        this->SetImagePreviewUrlForTab(tab_id, image_url);
        app->ShowImagePreviewOverlay();
        ScheduleImagePreviewPushForTab(tab_id);
      }
    }
    return true;
  }

  if (command_id == MENU_ID_PREVIEW_DOC) {
    std::string doc_url = params->GetLinkUrl().ToString();
    if (!doc_url.empty()) {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        int tab_id = app->GetCurrentTabId();
        ResetDocPreviewFetchStateForTab(tab_id);

        if (tab_manager_) {
          tab_manager_->SetSchemeUrl(tab_id, "");
          tab_manager_->SetDocPreviewMode(tab_id, DocPreviewMode::kInline);
        }
        this->SetDocPreviewUrlForTab(tab_id, doc_url);
        app->ShowDocPreviewOverlay();
        // Immediate content-less push → the overlay resets to its loading
        // indicator now, before the fetch completes (mirrors image preview).
        ScheduleDocPreviewPushForTab(tab_id);
        ScheduleDocPreviewFetchForTab(tab_id, doc_url);
      }
    }
    return true;
  }

  if (command_id == MENU_ID_SEARCH_SELECTION) {
    OtfApp* app = OtfApp::GetInstance();
    if (!app || !tab_manager_) {
      return false;
    }

    const std::string selection_text = TrimWhitespaceCopy(params->GetSelectionText().ToString());
    const std::optional<std::string> search_engine_id = otf::GetCurrentSearchEngineId();
    if (selection_text.empty() || !search_engine_id.has_value()) {
      return false;
    }

    const std::string search_url =
        otf::BuildSearchUrl(*search_engine_id, selection_text);
    if (search_url.empty()) {
      return false;
    }

    int parent_id = tab_manager_->GetId(browser);
    int new_id = app->CreateTab(search_url, parent_id, tab_manager_->IsPrivate(parent_id));
    if (OtfHandler* ui_handler = OtfHandler::GetInstance()) {
      ui_handler->NotifyNewTab(new_id, parent_id);
    }
    app->SwitchTab(new_id);
    return true;
  }

  if (command_id == IDC_CONTENT_CONTEXT_COPYLINKLOCATION) {
    // CEF Alloy does not route this command to a native handler, so we
    // handle it ourselves using the platform clipboard API.
    WriteToClipboard(StripTrackingParamsFromUrl(params->GetLinkUrl().ToString()));
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->ShowToast("copy", "Link copied");
    }
    return true;
  }

  if (command_id == MENU_ID_PASTE_GO) {
    if (browser) {
#if defined(__APPLE__)
      const int kModifier = EVENTFLAG_COMMAND_DOWN;
#else
      const int kModifier = EVENTFLAG_CONTROL_DOWN;
#endif
      CefKeyEvent ev;
      ev.type = KEYEVENT_RAWKEYDOWN;
      ev.modifiers = kModifier;
      ev.windows_key_code = 86;
      browser->GetHost()->SendKeyEvent(ev);
      ev.type = KEYEVENT_CHAR;
      ev.modifiers = kModifier;
      ev.windows_key_code = 22;
      browser->GetHost()->SendKeyEvent(ev);
      ev.type = KEYEVENT_KEYUP;
      ev.modifiers = kModifier;
      ev.windows_key_code = 86;
      browser->GetHost()->SendKeyEvent(ev);
      browser->GetMainFrame()->ExecuteJavaScript(
          "setTimeout(function(){"
          "  var el=document.activeElement;"
          "  if(el)el.dispatchEvent(new KeyboardEvent('keydown',"
          "    {key:'Enter',code:'Enter',bubbles:true,cancelable:true}));"
          "},30);",
          browser->GetMainFrame()->GetURL(), 0);
    }
    return true;
  }

  if (command_id == MENU_ID_COPY_EMAIL) {
    std::string link_url = params->GetLinkUrl().ToString();
    // Extract email from mailto:user@example.com or mailto:user@example.com?subject=...
    if (link_url.rfind("mailto:", 0) == 0) {
      std::string email = link_url.substr(7);
      size_t qpos = email.find('?');
      if (qpos != std::string::npos) {
        email = email.substr(0, qpos);
      }
      WriteToClipboard(email);
    }
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->ShowToast("copy", "Email copied");
    }
    return true;
  }

  if (command_id == MENU_ID_RELOAD) {
    browser->Reload();
    return true;
  }

  // Return false for all unhandled commands — CEF will execute built-in commands
  // like IDC_CONTENT_CONTEXT_COPYLINKLOCATION natively (clipboard copy).
  return false;
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

bool OtfHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefRequest> request,
                                 bool user_gesture,
                                 bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();

  // Block javascript: and vbscript: navigations in any frame — covers
  // iframe.src = 'javascript:...' which bypasses the OnBeforePopup check.
  {
    const std::string url = request->GetURL().ToString();
    if (url.rfind("javascript:", 0) == 0 || url.rfind("vbscript:", 0) == 0) {
      return true;
    }
  }

  if (frame->IsMain()) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view && tab_manager_ && !IsNonTabBrowserViewId(view->GetID())) {
      const std::string next_url = request->GetURL().ToString();
      const std::string origin = ExtractOrigin(next_url);
      const int tab_id = view->GetID();

      if (!origin.empty() && store_ && !IsGuestTab(tab_id)) {
        const bool dest_blocked =
            store_->GetSitePermission(origin, "javascript") == "block";
        const bool tab_js_disabled = IsTabJsDisabled(tab_id);

        // Normal tab → blocked site: open a new JS-disabled tab.
        // JS-disabled tab → non-blocked site: open a new JS-enabled tab.
        // Both cases use the same redirect task; CreateTab picks the right
        // browser_settings based on the destination origin.
        if (dest_blocked != tab_js_disabled) {
          // Must notify the message router before cancelling so it can clean
          // up pending queries on this frame — skipping causes use-after-free
          // on CefFrame when the old tab is closed by the redirect task.
          if (message_router_) message_router_->OnBeforeBrowse(browser, frame);
          CefPostTask(TID_UI, new DeferredTabRedirectTask(next_url, tab_id));
          return true;
        }
      }

      const std::string current_url = tab_manager_->GetUrl(view->GetID());
      const bool is_image_preview_url =
          next_url == "browser://imagepreview" ||
          next_url.rfind("browser://image-preview/", 0) == 0 ||
          next_url.find("/imagepreview.html") != std::string::npos;
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
      if (tab_manager_->GetImagePreviewMode(view->GetID()) ==
              ImagePreviewMode::kDedicated &&
          !is_image_preview_url) {
        tab_manager_->SetSchemeUrl(view->GetID(), "");
        tab_manager_->SetImagePreviewMode(view->GetID(), ImagePreviewMode::kNone);
        SetImagePreviewUrlForTab(view->GetID(), "");
        if (OtfApp* app = OtfApp::GetInstance()) {
          app->HideImagePreviewOverlay();
        }
      }
      const bool is_doc_preview_url =
          next_url == "browser://docpreview" ||
          next_url.rfind("browser://doc-preview/", 0) == 0 ||
          next_url.find("/docpreview.html") != std::string::npos;
      if (tab_manager_->GetDocPreviewMode(view->GetID()) ==
              DocPreviewMode::kDedicated &&
          !is_doc_preview_url) {
        tab_manager_->SetSchemeUrl(view->GetID(), "");
        tab_manager_->SetDocPreviewMode(view->GetID(), DocPreviewMode::kNone);
        SetDocPreviewUrlForTab(view->GetID(), "");
        if (OtfApp* app = OtfApp::GetInstance()) {
          app->HideDocPreviewOverlay();
        }
      }
    }
    // If this URL is being opened in a new tab via middle-click or ctrl+click,
    // cancel the navigation in the source tab.
    if (!pending_new_tab_urls_.empty()) {
      std::string nav_url = request->GetURL().ToString();
      auto it = pending_new_tab_urls_.find(nav_url);
      if (it != pending_new_tab_urls_.end()) {
        pending_new_tab_urls_.erase(it);
        return true;
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
      url.rfind("about:srcdoc", 0) == 0) {
    return true;
  }

  // data: and blob: are blocked at the top level (address-bar phishing
  // surface) but allowed in subframes so the antifingerprint policy can be
  // exercised across those realms.
  if (is_main_frame &&
      (url.rfind("data:", 0) == 0 || url.rfind("blob:", 0) == 0)) {
    return true;
  }

  // Block file:// everywhere. The app UI is served through browser://; local
  // files must not be reachable as browser content or privileged UI frames.
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (otf::IsLocalFilesystemPathLike(url)) {
    return true;
  }
  if (url.rfind("file://", 0) == 0) {
    return true;
  }

  if (is_main_frame && url.rfind("http://", 0) == 0 &&
      !IsAllowedHttpUrl(url)) {
    // Upgrade to HTTPS silently.  If the HTTPS version fails to load,
    // OnLoadError will fall back to the insecure-blocked page.
    const std::string https_url = "https://" + url.substr(7);
    if (view && tab_manager_) {
      const int tab_id = view->GetID();
      http_upgraded_urls_[tab_id] = url;
      tab_manager_->SetSslError(tab_id, false);
    }
    request->SetURL(https_url);
    return false;
  }

  std::string dev_ui_url = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");

  if (!dev_ui_url.empty() && IsAllowedBrowserPageUrl(url)) {
    std::string transformed = GetBrowserPageDevUrl(dev_ui_url, url);
    request->SetURL(transformed);
  }

  return false;
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
      std::string zoom_action = "reset";
      if (action == Shortcut::kZoomIn) {
        zoom_action = "in";
      } else if (action == Shortcut::kZoomOut) {
        zoom_action = "out";
      }
      ApplyTabZoomAction(cur, zoom_action);
    }
  };

  // ── Navigation & Page ─────────────────────────────────────
  if (M(Mod::kAlt,  Key::kLeft))   { nav(Shortcut::kBack);    return true; }
  if (M(Mod::kAlt,  Key::kRight))  { nav(Shortcut::kForward); return true; }
  if (M(Mod::kNone, Key::kF5) || M(Mod::kCtrl, Key::kR)) {
    *is_keyboard_shortcut = true; nav(Shortcut::kReload); return true;
  }
  if (M(Mod::kCtrl|Mod::kShift, Key::kR)) {
    *is_keyboard_shortcut = true;
    auto b = tab_manager_->GetBrowser(cur);
    if (b) b->ReloadIgnoreCache();
    return true;
  }
  // ── Fullscreen ────────────────────────────────────────────
  if (M(Mod::kNone, Key::kF11)) {
    *is_keyboard_shortcut = true;
    if (app->IsContentFullscreen()) {
      auto b = tab_manager_ ? tab_manager_->GetBrowser(cur) : nullptr;
      if (b) b->GetHost()->ExitFullscreen(true);
    } else {
      app->ToggleFullscreen();
    }
    return true;
  }

  if (M(Mod::kNone, Key::kEscape)) {
    // During content-initiated fullscreen, let Chromium handle ESC so
    // that fullscreenchange fires and document.fullscreenElement is
    // properly cleared.  Only intercept for browser-initiated fullscreen.
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
    if (otf::PopupOverlay* p = app->GetPopup("cleardata");
        p && p->IsVisible()) {
      p->Hide();
      return true;
    }
    if (otf::PopupOverlay* p = app->GetPopup("workspace");
        p && p->IsVisible()) {
      p->Hide();
      return true;
    }
    if (otf::PopupOverlay* p = app->GetPopup("qr");
        p && p->IsVisible()) {
      p->Hide();
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
    if (app->IsContentFullscreen()) {
      auto b = tab_manager_ ? tab_manager_->GetBrowser(cur) : nullptr;
      if (b) b->GetHost()->ExitFullscreen(true);
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
      if (IsPersistableWebUrl(url) && !IsGuestTab(cur)) {
        bool bookmarked = false;
        if (store_->IsBookmarked(url)) {
          store_->RemoveBookmarkByUrl(url);
        } else {
          store_->AddBookmark(url, tab_manager_->GetTitle(cur),
                              tab_manager_->GetFaviconUrl(cur));
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
    if (OtfHandler* ui_handler = OtfHandler::GetInstance()) { ui_handler->NotifyNewTab(id, tab_manager_->GetId(browser)); }
    app->SwitchTab(id);
    return true;
  }
  if (M(Mod::kCtrl, Key::kJ)) {
    int id = app->CreateTab("browser://downloads");
    if (OtfHandler* ui_handler = OtfHandler::GetInstance()) { ui_handler->NotifyNewTab(id, tab_manager_->GetId(browser)); }
    app->SwitchTab(id);
    return true;
  }
  if (M(Mod::kCtrl|Mod::kShift, Key::kJ) || M(Mod::kNone, Key::kF12)) {
    *is_keyboard_shortcut = true;
    app->ToggleConsoleOverlay();
    return true;
  }

  // ── Snip (Web Screenshot Tool) ─────────────────────────────
  if (M(Mod::kCtrl|Mod::kShift, Key::kS)) {
    *is_keyboard_shortcut = true;
    auto b = tab_manager_->GetBrowser(cur);
    if (b && devtools_bridge_) {
      devtools_bridge_->Attach(b);
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("format", "png");
      devtools_bridge_->Execute(
          "Page.captureScreenshot", params,
          [](bool ok, const std::string& result_json) {
            if (!ok) return;
            OtfApp* app = OtfApp::GetInstance();
            OtfHandler* handler = OtfHandler::GetInstance();
            if (!app || !handler || !handler->snip_preview_browser_) return;
            app->ShowSnipPreviewOverlay();
            const std::string js = "window.__otfSetSnipImage(" + result_json + ");";
            handler->snip_preview_browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
          });
    }
    return true;
  }

  // ── Tabs (C++ action + frontend notification) ─────────────
  if (M(Mod::kCtrl, Key::kT)) {
    int id = app->CreateTab("browser://newtab");
    if (OtfHandler* ui_handler = OtfHandler::GetInstance()) { ui_handler->NotifyNewTab(id, -1); } // Ctrl+T always opens at the end
    app->SwitchTab(id);
    return true;
  }
  if (M(Mod::kCtrl|Mod::kShift, Key::kN)) {
    int id = app->CreateTab("browser://newtab", -1, /*is_private=*/true);
    if (OtfHandler* ui_handler = OtfHandler::GetInstance()) { ui_handler->NotifyNewTab(id, -1); }
    app->SwitchTab(id);
    return true;
  }
  if (M(Mod::kCtrl, Key::kW)) {
    CloseTabAndNotify(cur);
    return true;
  }
  if (M(Mod::kCtrl|Mod::kShift, Key::kT)) {
    while (!recently_closed_tabs_.empty()) {
      ClosedTabInfo info = std::move(recently_closed_tabs_.front());
      recently_closed_tabs_.pop_front();
      if (info.workspace_id != active_workspace_id_) {
        continue;
      }
      int id = -1;
      if (info.is_image_preview) {
        WorkspaceTab wt;
        wt.url = info.url;
        wt.title = info.title;
        wt.is_image_preview = true;
        wt.preview_local_path = info.preview_local_path;
        wt.preview_page = info.preview_page;
        id = app->CreateRestoredTab(wt);
      } else if (info.is_doc_preview) {
        WorkspaceTab wt;
        wt.url = info.url;
        wt.title = info.title;
        wt.is_doc_preview = true;
        wt.preview_local_path = info.preview_local_path;
        id = app->CreateRestoredTab(wt);
      } else if (!info.url.empty() && !otf::IsLocalFilesystemPathLike(info.url) &&
                 otf::IsAllowedStartupUrl(info.url)) {
        id = app->CreateTab(info.url);
      }
      if (id >= 0) {
        if (!info.favicon.empty() && tab_manager_) {
          tab_manager_->SetFaviconUrl(id, info.favicon);
        }
        if (OtfHandler* ui_handler = OtfHandler::GetInstance()) {
          ui_handler->NotifyNewTab(id, -1);
        }
        app->SwitchTab(id);
        break;
      }
    }
    return true;
  }
  if (M(Mod::kCtrl, Key::kTab) || M(Mod::kCtrl|Mod::kShift, Key::kTab)) {
    auto ids = tab_manager_->GetTabIdsForWorkspace(active_workspace_id_);
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

  // Only forward results for the currently focused tab
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
    findbar_subscription_->Success(
        BuildFindResultEvent(count, activeMatchOrdinal, tab_id, "", finalUpdate, pending_find_seq_));
  }
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
