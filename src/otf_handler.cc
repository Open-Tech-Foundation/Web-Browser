#include "otf_handler.h"
#include "otf_app.h"
#include "otf_clear_data_rpc.h"
#include "otf_console_rpc.h"
#include "otf_downloads_rpc.h"
#include "otf_findbar_rpc.h"
#include "otf_history_bookmarks_rpc.h"
#include "otf_keyboard_shortcuts.h"
#include "otf_navigation_rpc.h"
#include "otf_native_rpc.h"
#include "otf_search_rpc.h"
#include "otf_settings_rpc.h"
#include "otf_permissions_rpc.h"
#include "otf_site_data_rpc.h"
#include "otf_tabs_rpc.h"
#include "otf_ui_rpc.h"
#include "otf_workspaces_rpc.h"

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
#include "include/cef_task_manager.h"
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
constexpr std::array<int, 4> kBlockedContextMenuCommandIds = {
    IDC_VIEW_SOURCE,
    IDC_CONTENT_CONTEXT_VIEWFRAMESOURCE,
    IDC_CONTENT_CONTEXT_OPENLINKNEWTAB,
    IDC_CONTENT_CONTEXT_OPENLINKNEWWINDOW,
};

std::string ParseSiteDataOrigin(const std::string& raw) {
  const std::string extracted = NormalizeOrigin(otf::ExtractOrigin(raw));
  if (extracted.empty()) return {};
  if (extracted.rfind("http://", 0) != 0 &&
      extracted.rfind("https://", 0) != 0) {
    return {};
  }
  const std::string normalized_raw = NormalizeOrigin(raw);
  if (normalized_raw != extracted && normalized_raw != extracted + "/") {
    return {};
  }
  return extracted;
}

bool IsValidSitePermissionSetting(const std::string& permission,
                                  const std::string& setting) {
  if (permission == "popup" || permission == "downloads") {
    return setting == "ask" || setting == "allow" || setting == "block";
  }
  if (permission == "images" || permission == "javascript") {
    return setting == "allow" || setting == "block";
  }
  return false;
}

using ::otf::ParseIntStrict;
using ::otf::ParseUint32Strict;
using ::otf::ParseUint64Strict;

constexpr size_t kMaxTiffInputBytes = 64 * 1024 * 1024;
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

int ResolvePreviewTabId(CefRefPtr<CefBrowser> browser,
                        TabManager* tab_manager,
                        int explicit_tab_id) {
  if (explicit_tab_id >= 0) {
    return explicit_tab_id;
  }
  int tab_id = ResolveRealTabIdForBrowser(browser, tab_manager);
  if (tab_id == -1) {
    OtfApp* app = OtfApp::GetInstance();
    if (app) {
      tab_id = app->GetCurrentTabId();
    }
  }
  return tab_id;
}

class DeferredImagePreviewPushTask : public CefTask {
 public:
  explicit DeferredImagePreviewPushTask(int tab_id)
      : tab_id_(tab_id) {}

  void Execute() override {
    OtfApp* app = OtfApp::GetInstance();
    OtfHandler* handler = OtfHandler::GetInstance();
    if (!app || !handler || tab_id_ < 0) {
      return;
    }

    std::string event = handler->BuildImagePreviewLoadEvent(tab_id_);
    if (event.empty()) {
      return;
    }

    auto tab_sub = handler->tab_image_preview_subscriptions_.find(tab_id_);
    if (tab_sub != handler->tab_image_preview_subscriptions_.end() &&
        tab_sub->second) {
      tab_sub->second->Success(event);
    } else if (handler->image_preview_subscription_) {
      handler->image_preview_subscription_->Success(event);
    }

    CefRefPtr<CefBrowser> tab_browser =
        handler->tab_manager_ ? handler->tab_manager_->GetBrowser(tab_id_) : nullptr;
    CefRefPtr<CefFrame> tab_frame =
        tab_browser ? tab_browser->GetMainFrame() : nullptr;
    if (tab_frame) {
      std::string js =
          "if(window.__otfApplyImagePreview)window.__otfApplyImagePreview(" +
          event + ");";
      tab_frame->ExecuteJavaScript(js, tab_frame->GetURL(), 0);
    }

    if (app->image_preview_overlay_) {
      CefRefPtr<CefBrowserView> preview_view =
          app->image_preview_overlay_->GetContentsView()->AsBrowserView();
      CefRefPtr<CefBrowser> browser =
          preview_view ? preview_view->GetBrowser() : nullptr;
      CefRefPtr<CefFrame> frame = browser ? browser->GetMainFrame() : nullptr;
      if (frame) {
        std::string js =
            "if(window.__otfApplyImagePreview)window.__otfApplyImagePreview(" +
            event + ");";
        frame->ExecuteJavaScript(js, frame->GetURL(), 0);
      }
    }
  }

 private:
  int tab_id_;
  IMPLEMENT_REFCOUNTING(DeferredImagePreviewPushTask);
};

class DeferredDocPreviewPushTask : public CefTask {
 public:
  explicit DeferredDocPreviewPushTask(int tab_id)
      : tab_id_(tab_id) {}

  void Execute() override {
    OtfApp* app = OtfApp::GetInstance();
    OtfHandler* handler = OtfHandler::GetInstance();
    if (!app || !handler || tab_id_ < 0) {
      return;
    }

    std::string event = handler->BuildDocPreviewLoadEvent(tab_id_);
    if (event.empty()) {
      return;
    }

    if (handler->doc_preview_subscription_) {
      handler->doc_preview_subscription_->Success(event);
    }

    if (app->doc_preview_overlay_) {
      CefRefPtr<CefBrowserView> preview_view =
          app->doc_preview_overlay_->GetContentsView()->AsBrowserView();
      CefRefPtr<CefBrowser> browser =
          preview_view ? preview_view->GetBrowser() : nullptr;
      CefRefPtr<CefFrame> frame = browser ? browser->GetMainFrame() : nullptr;
      if (frame) {
        std::string js =
            "if(window.__otfApplyDocPreview)window.__otfApplyDocPreview(" +
            event + ");";
        frame->ExecuteJavaScript(js, frame->GetURL(), 0);
      }
    }
  }

 private:
  int tab_id_;
  IMPLEMENT_REFCOUNTING(DeferredDocPreviewPushTask);
};

class DeferredDocFetchTask : public CefTask {
 public:
  DeferredDocFetchTask(const std::string& url, int tab_id)
      : url_(url), tab_id_(tab_id) {}

  void Execute() override {
    if (url_.empty() || tab_id_ < 0) return;

    CefRefPtr<CefRequest> request = CefRequest::Create();
    request->SetURL(url_);
    request->SetMethod("GET");

    CefRefPtr<FetchDocCallback> callback =
        new FetchDocCallback(url_, tab_id_);
    CefRefPtr<CefURLRequest> url_request = CefURLRequest::Create(
        request, callback, nullptr);
  }

  private:
  class FetchDocCallback : public CefURLRequestClient {
   public:
    FetchDocCallback(const std::string& url, int tab_id)
        : url_(url), tab_id_(tab_id) {}

    void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
      OtfHandler* handler = OtfHandler::GetInstance();
      if (!handler || tab_id_ < 0) return;

      if (response_bytes_.empty()) {
        handler->SetDocPreviewUrlForTab(tab_id_, url_);
      } else {
        // Keep the fetched bytes in memory and serve them from there — a live
        // preview must not touch the disk. No temp file: the scheme handler
        // streams these bytes for the content URL (PDF viewer) and
        // BuildDocPreviewLoadEvent builds the text data: URI from them.
        const std::string name = url_.substr(url_.find_last_of('/') + 1);
        const std::string safe_name = name.empty() ? "document.txt" : name;
        std::string mime;
        CefRefPtr<CefResponse> resp = request->GetResponse();
        if (resp) mime = resp->GetMimeType().ToString();
        if (mime.empty()) mime = otf::GuessDocumentMimeType(safe_name);
        const std::string content_token =
            "fetch/" + std::to_string(tab_id_) + "/" + safe_name;
        const std::string content_url =
            "browser://doc-preview/content/" + content_token;
        otf::RegisterDocContentBytes(
            content_token,
            std::vector<uint8_t>(response_bytes_.begin(), response_bytes_.end()),
            mime);
        handler->SetDocPreviewContentUrlForTab(tab_id_, content_url);
      }

      CefPostTask(TID_UI, new DeferredDocPreviewPushTask(tab_id_));
    }

    void OnUploadProgress(CefRefPtr<CefURLRequest> request,
                          int64_t current,
                          int64_t total) override {}

    void OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                            int64_t current,
                            int64_t total) override {}

    void OnDownloadData(CefRefPtr<CefURLRequest> request,
                        const void* data,
                        size_t data_length) override {
      const char* p = static_cast<const char*>(data);
      response_bytes_.insert(response_bytes_.end(), p, p + data_length);
    }

    bool GetAuthCredentials(bool isProxy,
                            const CefString& host,
                            int port,
                            const CefString& realm,
                            const CefString& scheme,
                            CefRefPtr<CefAuthCallback> callback) override {
      return false;
    }

    std::string url_;
    int tab_id_;
    std::vector<char> response_bytes_;

    IMPLEMENT_REFCOUNTING(FetchDocCallback);
  };

  std::string url_;
  int tab_id_;
  IMPLEMENT_REFCOUNTING(DeferredDocFetchTask);
};

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

bool HasWhitespace(const std::string& value) {
  return std::any_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isspace(c);
  });
}

bool HasStrongUrlIntent(const std::string& value) {
  return value.find_first_of("/:?#") != std::string::npos;
}

std::string ExtractHostCandidate(const std::string& value) {
  size_t end = value.find_first_of("/:?#");
  return value.substr(0, end == std::string::npos ? value.size() : end);
}

std::optional<int> ExtractExplicitPort(const std::string& value) {
  const size_t scheme_pos = value.find("://");
  const size_t host_start = scheme_pos == std::string::npos ? 0 : scheme_pos + 3;
  const size_t host_end = value.find_first_of("/?#", host_start);
  const std::string authority =
      value.substr(host_start, host_end == std::string::npos
                                 ? std::string::npos
                                 : host_end - host_start);
  const size_t colon = authority.rfind(':');
  if (colon == std::string::npos || colon + 1 >= authority.size()) {
    return std::nullopt;
  }

  const std::string port_text = authority.substr(colon + 1);
  if (!std::all_of(port_text.begin(), port_text.end(), [](unsigned char c) {
        return std::isdigit(c);
      })) {
    return std::nullopt;
  }

  int port = 0;
  for (char c : port_text) {
    port = (port * 10) + (c - '0');
    if (port > 65535) {
      return port;
    }
  }
  return port;
}

bool HasValidExplicitPort(const std::string& value) {
  const std::optional<int> port = ExtractExplicitPort(value);
  return !port.has_value() || (*port >= 0 && *port <= 65535);
}

bool LooksLikeHostSyntax(const std::string& value) {
  static const std::regex localhost_pattern(
      R"(^localhost(?::\d{1,5})?(?:[/?#]|$))",
      std::regex_constants::icase);
  static const std::regex ipv4_pattern(
      R"(^(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?:[/?#]|$))");
  static const std::regex domain_pattern(
      R"(^(?=.{1,253}$)(?!-)(?:[a-zA-Z\d-]{1,63}\.)+[a-zA-Z]{2,63}(?::\d{1,5})?(?:[/?#].*)?$)");
  return std::regex_search(value, localhost_pattern) ||
         std::regex_search(value, ipv4_pattern) ||
         std::regex_search(value, domain_pattern);
}

bool HasExplicitScheme(const std::string& value) {
  static const std::regex explicit_scheme_pattern(
      R"(^[a-zA-Z][a-zA-Z\d+.-]*://)");
  return std::regex_search(value, explicit_scheme_pattern);
}

bool IsDangerousSchemeUrl(const std::string& url) {
  static const char* kDangerousSchemes[] = {
      "javascript:", "data:", "file:", "vbscript:", "blob:"};
  for (const char* s : kDangerousSchemes) {
    if (url.rfind(s, 0) == 0) return true;
  }
  return false;
}

bool IsSplitPlaceholderTab(TabManager* tab_manager, int tab_id) {
  if (!tab_manager || tab_id < 0) return false;
  const std::string scheme_url = tab_manager->GetSchemeUrl(tab_id);
  const std::string url = tab_manager->GetUrl(tab_id);
  return scheme_url == "browser://split-placeholder" ||
         url == "browser://split-placeholder" ||
         url.find("/splitplaceholder.html") != std::string::npos;
}

bool IsLocalhostOrIpv4(const std::string& value) {
  static const std::regex localhost_pattern(
      R"(^localhost(?::\d{1,5})?(?:[/?#]|$))",
      std::regex_constants::icase);
  static const std::regex ipv4_pattern(
      R"(^(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?:[/?#]|$))");
  return std::regex_search(value, localhost_pattern) ||
         std::regex_search(value, ipv4_pattern);
}

bool ResolveUserInputUrlWithoutDns(const std::string& input,
                                   const std::string& search_engine_id,
                                   std::string* resolved_url,
                                   std::string* dns_host) {
  const std::string trimmed = TrimWhitespaceCopy(input);
  if (trimmed.rfind("browser://", 0) == 0 ||
      trimmed.rfind("//", 0) == 0 ||
      HasExplicitScheme(trimmed)) {
    if (IsDangerousSchemeUrl(trimmed)) {
      *resolved_url = otf::BuildSearchUrl(search_engine_id, trimmed);
      return true;
    }
    *resolved_url = trimmed;
    return true;
  }

  if (HasWhitespace(trimmed) || !LooksLikeHostSyntax(trimmed)) {
    *resolved_url = otf::BuildSearchUrl(search_engine_id, trimmed);
    return true;
  }

  if (!HasValidExplicitPort(trimmed)) {
    *resolved_url = otf::BuildSearchUrl(search_engine_id, trimmed);
    return true;
  }

  if (IsLocalhostOrIpv4(trimmed)) {
    *resolved_url = "http://" + trimmed;
    return true;
  }

  if (HasStrongUrlIntent(trimmed)) {
    *resolved_url = "https://" + trimmed;
    return true;
  }

  *dns_host = ExtractHostCandidate(trimmed);
  return false;
}

class UserInputResolveCallback : public CefResolveCallback {
 public:
  UserInputResolveCallback(
      CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
      std::string input,
      std::string search_engine_id)
      : callback_(callback),
        input_(std::move(input)),
        search_engine_id_(std::move(search_engine_id)) {}

  void OnResolveCompleted(cef_errorcode_t result,
                          const std::vector<CefString>& resolved_ips) override {
    const std::string trimmed = TrimWhitespaceCopy(input_);
    const bool resolved = result == ERR_NONE && !resolved_ips.empty();
    callback_->Success(resolved ? "https://" + trimmed
                                : otf::BuildSearchUrl(search_engine_id_, trimmed));
  }

 private:
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback_;
  std::string input_;
  std::string search_engine_id_;

  IMPLEMENT_REFCOUNTING(UserInputResolveCallback);
};

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

std::string GuessImageMimeType(const std::string& url) {
  auto ends_with = [&](std::string_view suffix) {
    return url.size() >= suffix.size() &&
           url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  if (ends_with(".png")) return "image/png";
  if (ends_with(".gif")) return "image/gif";
  if (ends_with(".jpg") || ends_with(".jpeg")) return "image/jpeg";
  if (ends_with(".webp")) return "image/webp";
  if (ends_with(".bmp")) return "image/bmp";
  if (ends_with(".ico")) return "image/x-icon";
  if (ends_with(".svg")) return "image/svg+xml";
  if (ends_with(".avif")) return "image/avif";
  if (ends_with(".tif") || ends_with(".tiff")) return "image/tiff";
  return "application/octet-stream";
}

std::string BuildImageContentToken(int tab_id,
                                   const std::string& kind,
                                   int page,
                                   const std::string& name_hint) {
  std::string name = SanitizeFilename(name_hint.empty() ? "preview" : name_hint);
  if (name.empty()) {
    name = "preview";
  }
  return std::to_string(tab_id) + "/" + kind + "/" + std::to_string(page) +
         "/" + name;
}

std::vector<uint8_t> DecodeDataUrlBytes(const std::string& data_url) {
  const size_t comma = data_url.find(',');
  if (comma == std::string::npos) {
    return {};
  }
  CefRefPtr<CefBinaryValue> decoded =
      CefBase64Decode(data_url.substr(comma + 1));
  if (!decoded) {
    return {};
  }
  std::vector<uint8_t> bytes(decoded->GetSize());
  if (!bytes.empty()) {
    decoded->GetData(bytes.data(), bytes.size(), 0);
  }
  return bytes;
}

std::string MimeTypeToPreviewFormat(const std::string& mime_type) {
  if (mime_type == "image/jpeg") return "JPEG";
  if (mime_type == "image/png") return "PNG";
  if (mime_type == "image/gif") return "GIF";
  if (mime_type == "image/webp") return "WEBP";
  if (mime_type == "image/bmp") return "BMP";
  if (mime_type == "image/x-icon" || mime_type == "image/vnd.microsoft.icon") return "ICO";
  if (mime_type == "image/svg+xml") return "SVG";
  if (mime_type == "image/avif") return "AVIF";
  if (mime_type == "image/tiff") return "TIFF";
  return "";
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
  ShellExecuteA(nullptr, "open", std::filesystem::path(path).parent_path().string().c_str(),
                nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  SpawnDetached("open", {"-R", path});
#else
  SpawnDetached("xdg-open",
                {std::filesystem::path(path).parent_path().string()});
#endif
}

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

std::string BuildWorkspacesJson(const std::vector<Workspace>& workspaces,
                                int active_workspace_id) {
  std::stringstream ss;
  ss << "[";
  bool first = true;
  for (size_t i = 0; i < workspaces.size(); ++i) {
    const auto& w = workspaces[i];
    if (!first) ss << ",";
    first = false;
    ss << JsonObjectBuilder()
              .AddInt("id", w.id)
              .AddString("uuid", w.uuid)
              .AddString("name", w.name)
              .AddString("color", w.color)
              .AddInt("position", w.position)
              .AddBool("active", w.id == active_workspace_id)
              .AddBool("guest", false)
              .Build();
  }
  ss << "]";
  return ss.str();
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

std::string BuildTabPropertyEvent(int tab_id,
                                   const std::string& key,
                                   int64_t value) {
  return JsonObjectBuilder()
      .AddInt("id", tab_id)
      .AddString("key", key)
      .AddRaw("value", std::to_string(value))
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

int ToRoundedZoomPercent(double zoom_level) {
  return static_cast<int>(std::lround(otf::ZoomLevelToPercent(zoom_level)));
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

std::string BuildZoomUpdateEvent(int tab_id, int zoom_percent) {
  return JsonObjectBuilder()
      .AddString("key", "zoom-restore")
      .AddInt("tabId", tab_id)
      .AddInt("zoomPercent", zoom_percent)
      .Build();
}

bool IsPersistableZoomUrl(const std::string& url) {
  return IsPersistableWebUrl(url) && !IsInternalUiUrl(url);
}

bool SaveWorkspaceOriginZoom(TabManager* tab_manager,
                             OtfStore* store,
                             int tab_id,
                             int zoom_percent) {
  if (!tab_manager) return false;
  const int workspace_id = tab_manager->GetWorkspaceId(tab_id);
  const std::string url = tab_manager->GetUrl(tab_id);
  if (workspace_id <= 0 || !IsPersistableZoomUrl(url)) return false;

  const std::string origin = otf::ExtractOrigin(url);
  if (origin.empty()) return false;
  if (tab_manager->IsPrivate(tab_id)) {
    tab_manager->SetPrivateOriginZoom(workspace_id, origin, zoom_percent);
    return true;
  }
  if (!store) return false;
  tab_manager->SetOriginZoom(workspace_id, origin, zoom_percent);
  return store->SetWorkspaceOriginZoom(workspace_id, origin, zoom_percent);
}

bool ApplyWorkspaceOriginZoom(CefRefPtr<CefBrowser> browser,
                              TabManager* tab_manager,
                              int tab_id,
                              int* applied_percent) {
  if (!browser || !tab_manager || tab_manager->IsPrivate(tab_id)) return false;
  const int workspace_id = tab_manager->GetWorkspaceId(tab_id);
  const std::string url = tab_manager->GetUrl(tab_id);
  if (workspace_id <= 0 || !IsPersistableZoomUrl(url)) return false;

  const std::string origin = otf::ExtractOrigin(url);
  if (origin.empty()) return false;
  const int zoom_percent = tab_manager->GetOriginZoom(workspace_id, origin);
  browser->GetHost()->SetZoomLevel(otf::PercentToZoomLevel(zoom_percent));
  tab_manager->SetZoomPercent(tab_id, zoom_percent);
  if (applied_percent) *applied_percent = zoom_percent;
  return true;
}

bool ApplyPrivateTabZoom(CefRefPtr<CefBrowser> browser,
                         TabManager* tab_manager,
                         int tab_id,
                         int* applied_percent) {
  if (!browser || !tab_manager || !tab_manager->IsPrivate(tab_id)) return false;
  int zoom_percent = tab_manager->GetZoomPercent(tab_id);
  const int workspace_id = tab_manager->GetWorkspaceId(tab_id);
  const std::string url = tab_manager->GetUrl(tab_id);
  if (workspace_id > 0 && IsPersistableZoomUrl(url)) {
    const std::string origin = otf::ExtractOrigin(url);
    if (!origin.empty()) {
      zoom_percent = tab_manager->GetPrivateOriginZoom(workspace_id, origin);
    }
  }
  browser->GetHost()->SetZoomLevel(otf::PercentToZoomLevel(zoom_percent));
  tab_manager->SetZoomPercent(tab_id, zoom_percent);
  if (applied_percent) *applied_percent = zoom_percent;
  return true;
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

bool IsTiffFileWithinInputLimit(const std::string& file_path,
                                std::string* error_reason) {
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(file_path, ec);
  if (ec) {
    if (error_reason) {
      *error_reason = "Could not read TIFF file";
    }
    return false;
  }
  if (file_size > kMaxTiffInputBytes) {
    if (error_reason) {
      *error_reason = "TIFF exceeds 64 MB size limit";
    }
    return false;
  }
  return true;
}

bool DecodeLocalTiffPreview(const std::string& file_path,
                            int page_index,
                            std::string* png_base64,
                            int* page_count,
                            std::string* error_reason) {
  if (!IsTiffFileWithinInputLimit(file_path, error_reason)) {
    return false;
  }
  std::string local_png;
  int local_page_count = 1;
  if (!otf::DecodeTiffToPngBase64(file_path, page_index, local_png,
                                  local_page_count)) {
    if (error_reason && error_reason->empty()) {
      *error_reason = "Failed to decode TIFF image";
    }
    return false;
  }
  if (png_base64) {
    *png_base64 = std::move(local_png);
  }
  if (page_count) {
    *page_count = local_page_count;
  }
  return true;
}

int64_t GetFileSizeBytes(const std::string& file_path, std::string* error_reason) {
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(file_path, ec);
  if (ec) {
    if (error_reason) {
      *error_reason = "Could not read TIFF file size";
    }
    return -1;
  }
  return static_cast<int64_t>(file_size);
}

std::string GuessPreviewFormat(const std::string& url) {
  const size_t end = url.find_first_of("?#");
  std::string clean_url = (end == std::string::npos) ? url : url.substr(0, end);
  const size_t slash = clean_url.find_last_of('/');
  const size_t dot = clean_url.find_last_of('.');
  if (dot == std::string::npos ||
      (slash != std::string::npos && dot < slash)) {
    return "";
  }
  std::string ext = clean_url.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (ext == "TIF" || ext == "TIFF" || ext == "PNG" || ext == "JPG" || ext == "JPEG" ||
      ext == "GIF" || ext == "WEBP" || ext == "BMP" || ext == "ICO" ||
      ext == "SVG" || ext == "AVIF") {
    return ext;
  }
  return "";
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

struct ResetRequest {
  bool startup = true;
  bool search_engine = true;
  bool cookies = true;
  bool cache = true;
  bool ssl = true;
  bool service_workers = true;
  bool permissions = true;
  bool storage = true;
  bool bookmarks = false;
  bool history = false;
  bool downloads = false;
  bool passwords = false;
};

struct ResetResult {
  std::vector<std::string> completed;
  std::vector<std::string> pending_restart;
  std::vector<std::string> unsupported;
  std::vector<std::string> failed;
};

bool ParseResetRequest(const std::string& raw_json, ResetRequest* request) {
  if (!request) {
    return false;
  }

  CefRefPtr<CefValue> root =
      CefParseJSON(raw_json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    return false;
  }

  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict) {
    return false;
  }

  const char* keys[] = {"startup",      "searchEngine", "cookies",
                        "cache",        "ssl",          "serviceWorkers",
                        "permissions",  "storage",      "bookmarks",
                        "history",      "downloads",    "passwords"};
  for (const auto* key : keys) {
    if (!dict->HasKey(key)) {
      continue;
    }
    if (dict->GetType(key) != VTYPE_BOOL) {
      return false;
    }
  }

  request->startup = dict->HasKey("startup") ? dict->GetBool("startup") : true;
  request->search_engine =
      dict->HasKey("searchEngine") ? dict->GetBool("searchEngine") : true;
  request->cookies = dict->HasKey("cookies") ? dict->GetBool("cookies") : true;
  request->cache = dict->HasKey("cache") ? dict->GetBool("cache") : true;
  request->ssl = dict->HasKey("ssl") ? dict->GetBool("ssl") : true;
  request->service_workers =
      dict->HasKey("serviceWorkers") ? dict->GetBool("serviceWorkers") : true;
  request->permissions =
      dict->HasKey("permissions") ? dict->GetBool("permissions") : true;
  request->storage = dict->HasKey("storage") ? dict->GetBool("storage") : true;
  request->bookmarks =
      dict->HasKey("bookmarks") ? dict->GetBool("bookmarks") : false;
  request->history = dict->HasKey("history") ? dict->GetBool("history") : false;
  request->downloads =
      dict->HasKey("downloads") ? dict->GetBool("downloads") : false;
  request->passwords =
      dict->HasKey("passwords") ? dict->GetBool("passwords") : false;
  return true;
}

class ResetCompletionState
    : public CefBaseRefCounted {
 public:
  explicit ResetCompletionState(
      CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback)
      : callback_(callback) {}

  void AddPending(const std::string& name) {
    (void)name;
    ++pending_ops_;
  }

  void MarkCompleted(const std::string& name) {
    completed_.push_back(name);
  }

  void MarkPendingRestart(const std::string& name) {
    pending_restart_.push_back(name);
  }

  void MarkUnsupported(const std::string& name) {
    unsupported_.push_back(name);
  }

  void MarkFailed(const std::string& name) {
    failed_.push_back(name);
  }

  void FinishOp() {
    if (pending_ops_ > 0) {
      --pending_ops_;
    }
    MaybeFinalize();
  }

  void MaybeFinalize() {
    if (finished_ || pending_ops_ > 0 || !callback_) {
      return;
    }
    finished_ = true;
    JsonObjectBuilder builder;
    builder.AddBool("ok", failed_.empty());

    std::string completed_json = "[";
    for (size_t i = 0; i < completed_.size(); ++i) {
      if (i > 0) completed_json += ",";
      completed_json += JsonString(completed_[i]);
    }
    completed_json += "]";

    std::string pending_json = "[";
    for (size_t i = 0; i < pending_restart_.size(); ++i) {
      if (i > 0) pending_json += ",";
      pending_json += JsonString(pending_restart_[i]);
    }
    pending_json += "]";

    std::string unsupported_json = "[";
    for (size_t i = 0; i < unsupported_.size(); ++i) {
      if (i > 0) unsupported_json += ",";
      unsupported_json += JsonString(unsupported_[i]);
    }
    unsupported_json += "]";

    std::string failed_json = "[";
    for (size_t i = 0; i < failed_.size(); ++i) {
      if (i > 0) failed_json += ",";
      failed_json += JsonString(failed_[i]);
    }
    failed_json += "]";

    builder.AddRaw("completed", completed_json)
        .AddRaw("pendingRestart", pending_json)
        .AddRaw("unsupported", unsupported_json)
        .AddRaw("failed", failed_json);
    builder.AddBool("requiresRestart", !pending_restart_.empty());
    callback_->Success(builder.Build());
  }

  IMPLEMENT_REFCOUNTING(ResetCompletionState);

 private:
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback_;
  int pending_ops_ = 0;
  bool finished_ = false;
  std::vector<std::string> completed_;
  std::vector<std::string> pending_restart_;
  std::vector<std::string> unsupported_;
  std::vector<std::string> failed_;
};

class ResetAsyncCallback : public CefCompletionCallback,
                           public CefDeleteCookiesCallback {
 public:
  ResetAsyncCallback(CefRefPtr<ResetCompletionState> state, std::string name)
      : state_(state), name_(std::move(name)) {}

  void OnComplete() override {
    if (state_) {
      state_->MarkCompleted(name_);
      state_->FinishOp();
    }
  }

  void OnComplete(int num_deleted) override {
    (void)num_deleted;
    if (state_) {
      state_->MarkCompleted(name_);
      state_->FinishOp();
    }
  }

 private:
  CefRefPtr<ResetCompletionState> state_;
  std::string name_;

  IMPLEMENT_REFCOUNTING(ResetAsyncCallback);
};

std::string BuildResetSettingsJson(const std::string& current_settings_json) {
  bool history_enabled = false;
  bool downloads_enabled = false;
  bool https_only = false;
  bool block_insecure = true;
  std::string appearance_mode = "auto";
  auto is_allowed_appearance_mode = [](const std::string& mode) {
    return mode == "auto" || mode == "light" || mode == "dark";
  };
  std::vector<CustomSearchEngine> custom_engines;

  CefRefPtr<CefValue> root =
      CefParseJSON(current_settings_json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (root && root->GetType() == VTYPE_DICTIONARY) {
    CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
    if (dict) {
      if (dict->HasKey("historyEnabled") &&
          dict->GetType("historyEnabled") == VTYPE_BOOL) {
        history_enabled = dict->GetBool("historyEnabled");
      }
      if (dict->HasKey("downloadsEnabled") &&
          dict->GetType("downloadsEnabled") == VTYPE_BOOL) {
        downloads_enabled = dict->GetBool("downloadsEnabled");
      }
      if (dict->HasKey("httpsOnly") &&
          dict->GetType("httpsOnly") == VTYPE_BOOL) {
        https_only = dict->GetBool("httpsOnly");
      }
      if (dict->HasKey("blockInsecure") &&
          dict->GetType("blockInsecure") == VTYPE_BOOL) {
        block_insecure = dict->GetBool("blockInsecure");
      }
      if (dict->HasKey("appearanceMode") &&
          dict->GetType("appearanceMode") == VTYPE_STRING) {
        appearance_mode = dict->GetString("appearanceMode");
        if (!is_allowed_appearance_mode(appearance_mode)) {
          appearance_mode = "auto";
        }
      }
      // Preserve custom search engines
      if (dict->HasKey("customSearchEngines") &&
          dict->GetType("customSearchEngines") == VTYPE_LIST) {
        CefRefPtr<CefListValue> list = dict->GetList("customSearchEngines");
        for (size_t i = 0; i < list->GetSize(); ++i) {
          if (list->GetType(i) != VTYPE_DICTIONARY) continue;
          CefRefPtr<CefDictionaryValue> entry = list->GetDictionary(i);
          if (!entry->HasKey("id") || entry->GetType("id") != VTYPE_STRING) continue;
          if (!entry->HasKey("name") || entry->GetType("name") != VTYPE_STRING) continue;
          if (!entry->HasKey("url") || entry->GetType("url") != VTYPE_STRING) continue;
          std::string id = entry->GetString("id");
          std::string name = entry->GetString("name");
          std::string url = entry->GetString("url");
          if (id.empty() || name.empty() || url.empty()) continue;
          custom_engines.push_back({std::move(id), std::move(name), std::move(url)});
        }
      }
    }
  }

  JsonObjectBuilder builder;
  return builder.AddNull("searchEngine")
      .AddBool("historyEnabled", history_enabled)
      .AddBool("downloadsEnabled", downloads_enabled)
      .AddString("startupBehavior", "newtab")
      .AddRaw("startupUrls", "[]")
      .AddBool("httpsOnly", https_only)
      .AddBool("blockInsecure", block_insecure)
      .AddString("appearanceMode", appearance_mode)
      .AddRaw("customSearchEngines",
              otf::BuildCustomEnginesJson(custom_engines))
      .Build();
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

std::string BuildStringArrayJson(const std::vector<CefString>& values) {
  std::string json = "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      json += ",";
    }
    json += JsonString(values[i].ToString());
  }
  json += "]";
  return json;
}

std::string BuildCertPrincipalJson(CefRefPtr<CefX509CertPrincipal> principal) {
  JsonObjectBuilder builder;
  std::vector<CefString> organizations;
  if (principal) {
    principal->GetOrganizationNames(organizations);
  }

  if (!principal) {
    return builder.AddString("displayName", "")
        .AddString("commonName", "")
        .AddRaw("organization", BuildStringArrayJson(organizations))
        .Build();
  }

  return builder.AddString("displayName", principal->GetDisplayName().ToString())
      .AddString("commonName", principal->GetCommonName().ToString())
      .AddRaw("organization", BuildStringArrayJson(organizations))
      .Build();
}

std::string BuildCertificateValidityJson(CefRefPtr<CefX509Certificate> cert) {
  time_t not_before = 0;
  time_t not_after = 0;
  if (cert) {
    cef_time_t valid_start{};
    cef_time_t valid_expiry{};
    if (cef_time_from_basetime(cert->GetValidStart(), &valid_start)) {
      cef_time_to_timet(&valid_start, &not_before);
    }
    if (cef_time_from_basetime(cert->GetValidExpiry(), &valid_expiry)) {
      cef_time_to_timet(&valid_expiry, &not_after);
    }
  }
  return JsonObjectBuilder()
      .AddRaw("notBefore", std::to_string(static_cast<int64_t>(not_before)))
      .AddRaw("notAfter", std::to_string(static_cast<int64_t>(not_after)))
      .Build();
}

std::string BuildCurrentCertificateJson(CefRefPtr<CefBrowser> browser,
                                       OtfHandler* handler,
                                       int tab_id,
                                       bool* ok,
                                       std::string* reason) {
  if (ok) {
    *ok = false;
  }

  if (!browser || tab_id < 0) {
    if (reason) {
      *reason = "No active tab";
    }
    return JsonObjectBuilder().AddBool("ok", false)
        .AddBool("hasCertificateError", false)
        .AddString("reason", reason ? *reason : "No active tab")
        .Build();
  }

  CefRefPtr<CefNavigationEntry> entry =
      browser->GetHost() ? browser->GetHost()->GetVisibleNavigationEntry()
                         : nullptr;
  CefRefPtr<CefSSLStatus> ssl_status = entry ? entry->GetSSLStatus() : nullptr;
  CefRefPtr<CefX509Certificate> certificate =
      ssl_status ? ssl_status->GetX509Certificate() : nullptr;
  const std::string tab_url =
      handler && handler->tab_manager_ ? handler->tab_manager_->GetUrl(tab_id) : "";
  const std::string entry_url = entry ? entry->GetURL().ToString() : "";
  const std::string url = !tab_url.empty() ? tab_url : entry_url;
  if (!certificate) {
    if (reason) {
      *reason = "No certificate available for current tab";
    }
    return JsonObjectBuilder().AddBool("ok", false)
        .AddBool("hasCertificateError", ssl_status &&
                                        CefIsCertStatusError(ssl_status->GetCertStatus()))
        .AddString("reason", reason ? *reason : "No certificate available for current tab")
        .Build();
  }

  if (ok) {
    *ok = true;
  }
  if (reason) {
    reason->clear();
  }

  return JsonObjectBuilder()
      .AddBool("ok", true)
      .AddBool("hasCertificateError",
               ssl_status && CefIsCertStatusError(ssl_status->GetCertStatus()))
      .AddString("url", url)
      .AddRaw("issuedTo", BuildCertPrincipalJson(certificate->GetSubject()))
      .AddRaw("issuedBy", BuildCertPrincipalJson(certificate->GetIssuer()))
      .AddRaw("validity", BuildCertificateValidityJson(certificate))
      .Build();
}

class OtfSizeRequestClient : public CefURLRequestClient {
 public:
  OtfSizeRequestClient(CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback)
      : callback_(callback) {}

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    if (request->GetRequestStatus() == UR_SUCCESS) {
      CefRefPtr<CefResponse> response = request->GetResponse();
      if (response) {
        std::string len_str = response->GetHeaderByName("Content-Length").ToString();
        if (!len_str.empty()) {
          callback_->Success(len_str);
          return;
        }
      }
    }
    callback_->Failure(0, "Could not fetch size");
  }

  void OnUploadProgress(CefRefPtr<CefURLRequest> request, int64_t current, int64_t total) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request, int64_t current, int64_t total) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data, size_t data_length) override {}
  bool GetAuthCredentials(bool isProxy,
                          const CefString& host,
                          int port,
                          const CefString& realm,
                          const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override {
    return false;
  }

 private:
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback_;

  IMPLEMENT_REFCOUNTING(OtfSizeRequestClient);
};

class OtfTiffDecodeClient : public CefURLRequestClient {
 public:
  OtfTiffDecodeClient(const std::string& source_url,
                      int page,
                      int tab_id,
                      uint64_t decode_nonce,
                      bool thumbnail_request,
                      CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback)
      : source_url_(source_url),
        page_(page),
        tab_id_(tab_id),
        decode_nonce_(decode_nonce),
        thumbnail_request_(thumbnail_request),
        is_tiff_(otf::IsTiffUrl(source_url)),
        callback_(callback) {}

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    if (rejected_) {
      callback_->Failure(1, reject_reason_);
      return;
    }
    OtfHandler* h = OtfHandler::GetInstance();
    if (h && tab_id_ != -1 && h->GetImagePreviewDecodeNonceForTab(tab_id_) != decode_nonce_) {
      callback_->Success(JsonObjectBuilder().AddBool("stale", true).Build());
      return;
    }
    if (request->GetRequestStatus() != UR_SUCCESS || raw_bytes_.empty()) {
      callback_->Failure(0, is_tiff_ ? "Failed to download or decode TIFF image"
                                     : "Failed to download image");
      return;
    }

    std::string mime_type = mime_type_;
    if (mime_type.empty()) {
      CefRefPtr<CefResponse> response = request->GetResponse();
      if (response) {
        mime_type = response->GetMimeType().ToString();
      }
    }
    if (mime_type.empty()) {
      mime_type = GuessImageMimeType(source_url_);
    }

    std::string preview_format = GuessPreviewFormat(source_url_);
    if (preview_format.empty()) {
      preview_format = MimeTypeToPreviewFormat(mime_type);
    }

    if (is_tiff_) {
      std::string png_base64;
      int page_count = 1;
      if (!otf::DecodeTiffBufferToPngBase64(raw_bytes_.data(), raw_bytes_.size(),
                                            page_, png_base64, page_count)) {
        callback_->Failure(0, "Failed to download or decode TIFF image");
        return;
      }
      std::string display_url;
      if (h && tab_id_ != -1) {
        const std::string token = BuildImageContentToken(
            tab_id_, "remote-tiff", page_, source_url_ + ".png");
        std::vector<uint8_t> png_bytes = DecodeDataUrlBytes(png_base64);
        if (png_bytes.empty()) {
          callback_->Failure(0, "Failed to prepare decoded TIFF image");
          return;
        }
        otf::RegisterImageContentBytes(token, std::move(png_bytes), "image/png");
        display_url = "browser://image-preview/content/" + token;
        if (!thumbnail_request_) {
          h->SetImagePreviewPageForTab(tab_id_, page_);
          h->SetImagePreviewPageCountForTab(tab_id_, page_count);
        }
        h->SetImagePreviewFileSizeForTab(tab_id_, static_cast<int64_t>(raw_bytes_.size()));
        h->SetImagePreviewFormatForTab(tab_id_, "TIFF");
        if (!thumbnail_request_) {
          h->tab_image_preview_download_cache_[tab_id_] =
              OtfHandler::ImagePreviewDownloadCache{
                  source_url_, mime_type, raw_bytes_, display_url,
                  static_cast<int64_t>(raw_bytes_.size()), page_, page_count, true};
          h->tab_image_preview_render_cache_[tab_id_] =
              OtfHandler::ImagePreviewRenderCache{source_url_, display_url, page_, page_count};
        }
      }
      callback_->Success(JsonObjectBuilder()
                             .AddString("displayUrl", display_url)
                             .AddInt("pageCount", page_count)
                             .AddInt("currentPage", page_)
                             .AddInt("fileSizeBytes",
                                     static_cast<int>(std::min<size_t>(
                                         raw_bytes_.size(),
                                         static_cast<size_t>(std::numeric_limits<int>::max()))))
                             .AddString("format", "TIFF")
                             .Build());
      return;
    }

    std::string display_url;
    if (h && tab_id_ != -1) {
      const std::string token =
          BuildImageContentToken(tab_id_, "remote", 0, source_url_);
      std::vector<uint8_t> image_bytes(raw_bytes_.begin(), raw_bytes_.end());
      otf::RegisterImageContentBytes(token, std::move(image_bytes), mime_type);
      display_url = "browser://image-preview/content/" + token;
      h->SetImagePreviewFileSizeForTab(tab_id_, static_cast<int64_t>(raw_bytes_.size()));
      h->SetImagePreviewFormatForTab(tab_id_, preview_format);
      h->tab_image_preview_download_cache_[tab_id_] = OtfHandler::ImagePreviewDownloadCache{
          source_url_, mime_type, raw_bytes_, display_url,
          static_cast<int64_t>(raw_bytes_.size()), 0, 1, false};
      h->tab_image_preview_render_cache_[tab_id_] =
          OtfHandler::ImagePreviewRenderCache{source_url_, display_url, 0, 1};
    }
    callback_->Success(JsonObjectBuilder()
                           .AddString("displayUrl", display_url)
                           .AddInt("pageCount", 1)
                           .AddInt("currentPage", 0)
                           .AddInt("fileSizeBytes",
                                   static_cast<int>(std::min<size_t>(
                                       raw_bytes_.size(),
                                       static_cast<size_t>(std::numeric_limits<int>::max()))))
                           .AddString("format", preview_format)
                           .Build());
  }

  void OnUploadProgress(CefRefPtr<CefURLRequest> request, int64_t current, int64_t total) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request, int64_t current, int64_t total) override {
    if (total > 0 && static_cast<uint64_t>(total) > kMaxTiffInputBytes) {
      RejectAndCancel(request);
      return;
    }
    if (rejected_) {
      return;
    }
    OtfHandler* h = OtfHandler::GetInstance();
    if (!h || tab_id_ == -1 || h->GetImagePreviewDecodeNonceForTab(tab_id_) != decode_nonce_) {
      return;
    }
    if (total > 0) {
      h->SetImagePreviewFileSizeForTab(tab_id_, total);
      if (is_tiff_) {
        h->SetImagePreviewFormatForTab(tab_id_, "TIFF");
      }
    }
    const int received_bytes =
        current < 0 ? 0 : static_cast<int>(std::min<int64_t>(current, std::numeric_limits<int>::max()));
    const int total_bytes =
        total < 0 ? -1 : static_cast<int>(std::min<int64_t>(total, std::numeric_limits<int>::max()));
    if (total_bytes > 0) {
      const int percent = static_cast<int>(std::clamp<int64_t>((current * 100) / total, 0, 100));
      if (percent == last_reported_percent_) {
        return;
      }
      last_reported_percent_ = percent;
    } else {
      if (received_bytes - last_reported_received_bytes_ < 256 * 1024 && received_bytes != 0) {
        return;
      }
      last_reported_received_bytes_ = received_bytes;
    }
    h->NotifyImagePreviewDownloadProgress(tab_id_, decode_nonce_, received_bytes, total_bytes);
  }
  
  void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data, size_t data_length) override {
    if (rejected_) {
      return;
    }
    if (data_length > kMaxTiffInputBytes ||
        raw_bytes_.size() > kMaxTiffInputBytes - data_length) {
      RejectAndCancel(request);
      return;
    }
    const char* bytes = static_cast<const char*>(data);
    raw_bytes_.append(bytes, data_length);
  }

  bool GetAuthCredentials(bool isProxy,
                          const CefString& host,
                          int port,
                          const CefString& realm,
                          const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override {
    return false;
  }

 private:
  void RejectAndCancel(CefRefPtr<CefURLRequest> request) {
    if (rejected_) {
      return;
    }
    rejected_ = true;
    reject_reason_ = is_tiff_ ? "Remote TIFF exceeds 64 MB size limit"
                              : "Remote image exceeds 64 MB size limit";
    raw_bytes_.clear();
    if (request) {
      request->Cancel();
    }
  }

  std::string source_url_;
  int page_;
  int tab_id_;
  uint64_t decode_nonce_;
  bool thumbnail_request_;
  bool is_tiff_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback_;
  std::string raw_bytes_;
  std::string mime_type_;
  bool rejected_ = false;
  std::string reject_reason_;
  int last_reported_percent_ = -1;
  int last_reported_received_bytes_ = 0;

  IMPLEMENT_REFCOUNTING(OtfTiffDecodeClient);
};

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

std::string CookiePathForTracking(const CefCookie& cookie);
int64_t CefBaseTimeToUnixSeconds(cef_basetime_t base_time);

// Handle messages from the UI Shell (index.html)
class OtfMessageRouterHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  OtfMessageRouterHandler() {}

  // Aggregates per-origin CDP Storage.getUsageAndQuota responses for the
  // get-site-usage-list query. Quota data replaces the old approach of
  // parsing Chromium's private on-disk formats (LevelDB content scans),
  // which could break silently on any Chromium/CEF upgrade. Origins come
  // from history and the cookie store; usage is read from the UI shell's
  // browser context (the default profile), matching the cookie data
  // gathered alongside it.
  static void QuerySiteUsageOverCdp(OtfHandler* handler,
                                    CefRefPtr<CefBrowser> ui_browser,
                                    CefRefPtr<Callback> callback,
                                    std::vector<std::string> extra_origins,
                                    std::map<std::string, uint64_t> cookie_sizes,
                                    std::map<std::string, uint64_t> cookie_counts) {
    struct Aggregation {
      CefRefPtr<Callback> callback;
      std::vector<std::string> extra_origins;
      std::map<std::string, uint64_t> cookie_sizes;
      std::map<std::string, uint64_t> cookie_counts;
      std::map<std::string, uint64_t> local_storage;
      std::map<std::string, uint64_t> indexed_db;
      std::map<std::string, uint64_t> cache_storage;
      int pending = 0;
      bool resolved = false;
      void Resolve() {
        if (resolved) return;
        resolved = true;
        callback->Success(otf::BuildSiteUsageJson(
            extra_origins, cookie_sizes, cookie_counts, local_storage,
            indexed_db, cache_storage));
      }
    };

    // Quota usage is keyed by exact origin, so raw origins are queried;
    // BuildSiteUsageJson normalizes them for display.
    std::set<std::string> origins(extra_origins.begin(), extra_origins.end());
    for (const auto& [origin, size] : cookie_sizes) origins.insert(origin);

    auto agg = std::make_shared<Aggregation>();
    agg->callback = callback;
    agg->extra_origins = std::move(extra_origins);
    agg->cookie_sizes = std::move(cookie_sizes);
    agg->cookie_counts = std::move(cookie_counts);

    if (!handler || !handler->devtools_bridge_ || !ui_browser ||
        origins.empty()) {
      agg->Resolve();
      return;
    }
    handler->devtools_bridge_->Attach(ui_browser);

    for (const auto& origin : origins) {
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("origin", origin);
      const int message_id = handler->devtools_bridge_->Execute(
          "Storage.getUsageAndQuota", params,
          [agg, origin](bool ok, const std::string& result_json) {
            --agg->pending;
            if (ok) {
              CefRefPtr<CefValue> parsed =
                  CefParseJSON(result_json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
              CefRefPtr<CefDictionaryValue> dict =
                  (parsed && parsed->GetType() == VTYPE_DICTIONARY)
                      ? parsed->GetDictionary()
                      : nullptr;
              CefRefPtr<CefListValue> breakdown =
                  (dict && dict->HasKey("usageBreakdown"))
                      ? dict->GetList("usageBreakdown")
                      : nullptr;
              if (breakdown) {
                for (size_t i = 0; i < breakdown->GetSize(); ++i) {
                  CefRefPtr<CefDictionaryValue> entry =
                      breakdown->GetDictionary(i);
                  if (!entry) continue;
                  const std::string type =
                      entry->GetString("storageType").ToString();
                  CefRefPtr<CefValue> usage_value = entry->GetValue("usage");
                  uint64_t usage = 0;
                  if (usage_value && usage_value->GetType() == VTYPE_DOUBLE) {
                    const double d = usage_value->GetDouble();
                    if (d > 0) usage = static_cast<uint64_t>(d);
                  } else if (usage_value &&
                             usage_value->GetType() == VTYPE_INT) {
                    const int v = usage_value->GetInt();
                    if (v > 0) usage = static_cast<uint64_t>(v);
                  }
                  if (usage == 0) continue;
                  if (type == "indexeddb") {
                    agg->indexed_db[origin] += usage;
                  } else if (type == "cache_storage") {
                    agg->cache_storage[origin] += usage;
                  } else if (type == "local_storage") {
                    agg->local_storage[origin] += usage;
                  }
                }
              }
            }
            if (agg->pending == 0) agg->Resolve();
          });
      if (message_id != 0) ++agg->pending;
    }
    if (agg->pending == 0) {
      agg->Resolve();
      return;
    }
    // Safety net: the bridge fails pending callbacks on agent detach, but a
    // hung target could still strand the query — answer with whatever has
    // arrived rather than never.
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce([](std::shared_ptr<Aggregation> a) { a->Resolve(); },
                       agg),
        5000);
  }

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    // The cefQuery bridge exposes privileged operations (settings I/O, tab
    // navigation, downloads, restart, etc.). Only the internal UI surface
    // (browser:// or the dev-ui server) is trusted to
    // call it — any other frame is denied outright. Web content should never
    // see this API even though CEF injects the JS function globally.
    const std::string frame_url = frame ? frame->GetURL().ToString() : "";
    bool trusted_frame = IsInternalBrowserUiUrl(frame_url);
    if (!trusted_frame) {
      // Dev mode: Vite serves the shell at the dev-ui-url root with no
      // /index.html in the path, so the suffix check above misses it.
      CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
      if (cmd && cmd->HasSwitch("dev-ui-url")) {
        const std::string dev_ui_url = cmd->GetSwitchValue("dev-ui-url").ToString();
        if (!dev_ui_url.empty() &&
            (frame_url == dev_ui_url || frame_url == dev_ui_url + "/" ||
             frame_url.rfind(dev_ui_url + "/", 0) == 0)) {
          trusted_frame = true;
        }
      }
    }
    if (!trusted_frame) {
      callback->Failure(1, "denied");
      return true;
    }

    // Defense-in-depth cap: even trusted frames shouldn't push multi-megabyte
    // payloads through the synchronous bridge.
    constexpr size_t kMaxRequestBytes = 64 * 1024;
    if (request.size() > kMaxRequestBytes) {
      callback->Failure(1, "request too large");
      return true;
    }

    OtfHandler* handler = OtfHandler::GetInstance();
    if (!handler || !handler->tab_manager_) return false;
    const std::string msg = request.ToString();
    const size_t first_non_space = msg.find_first_not_of(" \t\r\n");
    if (first_non_space != std::string::npos && msg[first_non_space] == '{') {
      NativeRpcRequest rpc_request;
      std::string parse_error;
      if (!ParseNativeRpcRequest(msg, &rpc_request, &parse_error)) {
        callback->Failure(1, parse_error);
        return true;
      }
      if (HandleSiteDataRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleClearDataRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleSettingsRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleHistoryBookmarksRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleNavigationRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleDownloadsRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleWorkspacesRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleUiRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleSearchRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandlePermissionsRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleFindbarRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleConsoleRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      if (HandleTabsRpc(handler, browser, callback, rpc_request)) {
        return true;
      }
      NativeRpcFailure(callback, rpc_request, "unknown_method",
                       "Unknown native RPC method");
      return true;
    }

    if (msg == "get-my-tab-id") {
      CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
      callback->Success(view ? std::to_string(view->GetID()) : "0");
      return true;
    }

    if (msg.rfind("get-tab-memory:", 0) == 0) {
      const auto id_opt = ParseIntStrict(
          std::string_view(msg).substr(std::strlen("get-tab-memory:")));
      if (!id_opt) {
        callback->Failure(1, "invalid tab id");
        return true;
      }
      int tab_id = *id_opt;
      CefRefPtr<CefBrowser> tab_browser;
      if (handler && handler->tab_manager_) {
        tab_browser = handler->tab_manager_->GetBrowser(tab_id);
      }
      int64_t memory_bytes = -1;
      if (tab_browser) {
        auto tm = CefTaskManager::GetTaskManager();
        if (tm) {
          int64_t task_id = tm->GetTaskIdForBrowserId(tab_browser->GetIdentifier());
          if (task_id >= 0) {
            CefTaskInfo info;
            if (tm->GetTaskInfo(task_id, info) && info.memory >= 0) {
              memory_bytes = info.memory;
            }
          }
        }
      }
      std::string json = JsonObjectBuilder()
                             .AddRaw("tabId", std::to_string(tab_id))
                             .AddRaw("memoryBytes", std::to_string(memory_bytes))
                             .Build();
      callback->Success(json);
      return true;
    }

    if (msg == "get-tab-private") {
      CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
      int tab_id = view ? view->GetID() : 0;
      bool is_private = handler && handler->tab_manager_ &&
                        handler->tab_manager_->IsPrivate(tab_id);
      callback->Success(is_private ? "true" : "false");
      return true;
    }

    if (msg == "get-version-info") {
      const std::string chromium = std::to_string(CHROME_VERSION_MAJOR) + "." +
                                   std::to_string(CHROME_VERSION_MINOR) + "." +
                                   std::to_string(CHROME_VERSION_BUILD) + "." +
                                   std::to_string(CHROME_VERSION_PATCH);
      std::string json = std::string("{\"browser\":\"") + OTF_VERSION +
                         "\",\"chromium\":\"" + chromium +
                         "\",\"cef\":\"" + CEF_VERSION + "\"}";
      callback->Success(json);
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
      if (!handler->guest_session_active_) {
        handler->downloads_subscription_ = callback;
      }
      callback->Success(JsonObjectBuilder()
                            .AddString("key", "downloads-update")
                            .AddRaw("downloads",
                                    handler->guest_session_active_ ? "[]"
                                                                   : handler->GetDownloadsJson())
                            .Build());
      return true;
    }

    if (msg == "certificate-subscribe") {
      handler->certificate_subscription_ = callback;
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        handler->certificate_subscription_->Success(
            JsonObjectBuilder()
                .AddString("key", "certificate-restore")
                .AddInt("tabId", app->GetCurrentTabId())
                .Build());
      }
      return true;
    }

    if (msg == "bookmark-subscribe") {
      handler->bookmark_subscription_ = callback;
      return true;
    }


    if (msg == "image-preview-subscribe") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        int tab_id = ResolveRealTabIdForBrowser(browser, handler->tab_manager_);
        if (tab_id != -1) {
          handler->tab_image_preview_subscriptions_[tab_id] = callback;
        } else {
          handler->image_preview_subscription_ = callback;
          tab_id = app->GetCurrentTabId();
        }

        std::string event = handler->BuildImagePreviewLoadEvent(tab_id);
        if (!event.empty()) {
          callback->Success(event);
        }
      }
      return true;
    }

    if (msg.rfind("image-preview-meta:", 0) == 0) {
      const std::string prefix = "image-preview-meta:";
      int explicit_tab_id = -1;
      int width = 0;
      int height = 0;
      const size_t second_colon = msg.find(':', prefix.size());
      if (second_colon != std::string::npos) {
        const size_t third_colon = msg.find(':', second_colon + 1);
        if (third_colon != std::string::npos) {
          const auto tab_opt = ParseIntStrict(
              std::string_view(msg).substr(prefix.size(), second_colon - prefix.size()));
          const auto width_opt = ParseIntStrict(
              std::string_view(msg).substr(second_colon + 1,
                                           third_colon - second_colon - 1));
          const auto height_opt =
              ParseIntStrict(std::string_view(msg).substr(third_colon + 1));
          if (tab_opt && *tab_opt >= 0) {
            explicit_tab_id = *tab_opt;
          }
          if (width_opt && height_opt && *width_opt >= 0 && *height_opt >= 0) {
            width = *width_opt;
            height = *height_opt;
          }
        } else {
          const auto width_opt = ParseIntStrict(
              std::string_view(msg).substr(prefix.size(), second_colon - prefix.size()));
          const auto height_opt = ParseIntStrict(
              std::string_view(msg).substr(second_colon + 1));
          if (width_opt && height_opt && *width_opt >= 0 && *height_opt >= 0) {
            width = *width_opt;
            height = *height_opt;
          }
        }
      }
      int tab_id = ResolvePreviewTabId(
          browser, handler ? handler->tab_manager_ : nullptr, explicit_tab_id);
      if (handler && handler->tab_manager_ && tab_id != -1) {
        handler->tab_manager_->SetImagePreviewDimensions(tab_id, width, height);
        std::string event = handler->BuildImagePreviewLoadEvent(tab_id, false);
        callback->Success(event.empty() ? "{}" : event);
      } else {
        callback->Success("{}");
      }
      return true;
    }

    if (msg.rfind("image-preview-info-visible:", 0) == 0) {
      const std::string prefix = "image-preview-info-visible:";
      int explicit_tab_id = -1;
      std::string_view visible_payload = std::string_view(msg).substr(prefix.size());
      const size_t colon = msg.find(':', prefix.size());
      if (colon != std::string::npos) {
        const auto tab_opt = ParseIntStrict(
            std::string_view(msg).substr(prefix.size(), colon - prefix.size()));
        if (tab_opt && *tab_opt >= 0) {
          explicit_tab_id = *tab_opt;
          visible_payload = std::string_view(msg).substr(colon + 1);
        }
      }
      const auto visible_opt = ParseIntStrict(visible_payload);
      const bool visible = visible_opt ? (*visible_opt != 0) : true;
      int tab_id = ResolvePreviewTabId(
          browser, handler ? handler->tab_manager_ : nullptr, explicit_tab_id);
      if (handler && handler->tab_manager_ && tab_id != -1) {
        handler->tab_manager_->SetImagePreviewInfoVisible(tab_id, visible);
        std::string event = handler->BuildImagePreviewLoadEvent(tab_id, false);
        callback->Success(event.empty() ? "{}" : event);
      } else {
        callback->Success("{}");
      }
      return true;
    }

    if (msg == "image-preview-refresh" ||
        msg.rfind("image-preview-refresh:", 0) == 0) {
      // One-shot fetch of the current preview state. Used by the renderer
      // when document.visibilityState flips back to "visible" so the JSX
      // can recover even if CEF cancelled the persistent subscription
      // (or the renderer was reloaded) while the tab was hidden.
      int explicit_tab_id = -1;
      if (msg.rfind("image-preview-refresh:", 0) == 0) {
        const auto tab_opt = ParseIntStrict(
            std::string_view(msg).substr(std::strlen("image-preview-refresh:")));
        if (tab_opt && *tab_opt >= 0) {
          explicit_tab_id = *tab_opt;
        }
      }
      int tab_id = ResolvePreviewTabId(
          browser, handler ? handler->tab_manager_ : nullptr, explicit_tab_id);
      std::string event = (handler && tab_id != -1)
                              ? handler->BuildImagePreviewLoadEvent(tab_id, false)
                              : std::string();
      callback->Success(event.empty() ? "{}" : event);
      return true;
    }

    if (msg == "doc-preview-subscribe") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        int tab_id = ResolveRealTabIdForBrowser(browser, handler->tab_manager_);
        if (tab_id != -1) {
          handler->tab_doc_preview_subscriptions_[tab_id] = callback;
        } else {
          handler->doc_preview_subscription_ = callback;
          tab_id = app->GetCurrentTabId();
        }

        std::string event = handler->BuildDocPreviewLoadEvent(tab_id);
        callback->Success(event.empty() ? "{}" : event);
      }
      return true;
    }

    if (msg == "doc-preview-refresh") {
      OtfApp* app = OtfApp::GetInstance();
      int tab_id = ResolveRealTabIdForBrowser(browser, handler ? handler->tab_manager_ : nullptr);
      if (tab_id == -1 && app) {
        tab_id = app->GetCurrentTabId();
      }
      std::string event = (handler && tab_id != -1)
                              ? handler->BuildDocPreviewLoadEvent(tab_id)
                              : std::string();
      callback->Success(event.empty() ? "{}" : event);
      return true;
    }

    if (msg == "hide-docpreview" || msg.rfind("hide-docpreview:", 0) == 0) {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideDocPreviewOverlay();
        int tab_id = -1;
        if (msg.rfind("hide-docpreview:", 0) == 0) {
          const auto tab_id_opt =
              ParseIntStrict(std::string_view(msg).substr(std::strlen("hide-docpreview:")));
          if (tab_id_opt) {
            tab_id = *tab_id_opt;
          }
        }
        if (tab_id == -1) {
          tab_id = app->GetCurrentTabId();
        }
        if (handler) {
          handler->ClearDocPreviewStateForTab(tab_id);
        }
      }
      callback->Success("");
      return true;
    }

    if (msg == "close-docpreview" || msg.rfind("close-docpreview:", 0) == 0) {
      OtfApp* app = OtfApp::GetInstance();
      if (!app || !handler) {
        callback->Success("");
        return true;
      }
      int tab_id = -1;
      if (msg.rfind("close-docpreview:", 0) == 0) {
        const auto tab_id_opt =
            ParseIntStrict(std::string_view(msg).substr(std::strlen("close-docpreview:")));
        if (tab_id_opt) {
          tab_id = *tab_id_opt;
        }
      }
      if (tab_id == -1) {
        tab_id = ResolveRealTabIdForBrowser(browser, handler->tab_manager_);
      }
      if (tab_id == -1 && app) {
        tab_id = app->GetCurrentTabId();
      }
      const bool is_dedicated_preview_tab =
          handler->tab_manager_ &&
          tab_id != -1 &&
          handler->tab_manager_->GetDocPreviewMode(tab_id) ==
              DocPreviewMode::kDedicated;
      if (is_dedicated_preview_tab) {
        handler->CloseTabAndNotify(tab_id);
      } else {
        handler->ClearDocPreviewStateForTab(tab_id);
        app->HideDocPreviewOverlay();
      }
      callback->Success("");
      return true;
    }

    if (msg.rfind("download-doc:", 0) == 0) {
      std::string download_url = msg.substr(13);
      int tab_id = -1;
      std::string local_path;
      std::string mime_type;
      if (download_url.rfind("browser://doc-preview/", 0) == 0 || download_url.empty()) {
        if (handler && handler->tab_manager_) {
          tab_id = handler->tab_manager_->GetId(browser);
          std::string mapped_url = handler->GetDocPreviewUrlForTab(tab_id);
          if (!mapped_url.empty()) {
            download_url = mapped_url;
          }
          local_path = handler->GetDocPreviewLocalFileForTab(tab_id);
          if (!local_path.empty()) {
            mime_type = otf::GuessDocumentMimeType(local_path);
          }
        }
      }
      if (!local_path.empty()) {
        std::error_code ec;
        const std::filesystem::path source_path(local_path);
        const std::string suggested_name =
            source_path.filename().empty()
                ? "download.txt"
                : SanitizeFilename(source_path.filename().string());
        const std::string target_path = otf::BuildDownloadPath(suggested_name);
        std::filesystem::copy_file(local_path, target_path,
                                   std::filesystem::copy_options::none, ec);
        if (ec) {
          callback->Failure(0, "Could not save document file");
          return true;
        }
        if (handler->store_) {
          const auto file_size = std::filesystem::file_size(local_path, ec);
          const int download_id = handler->store_->CreateDownload(
              download_url, local_path, target_path, suggested_name,
              mime_type.empty() ? "application/octet-stream" : mime_type,
              "completed");
          if (download_id > 0) {
            PersistedDownload download;
            download.id = download_id;
            download.url = download_url;
            download.original_url = local_path;
            download.target_path = target_path;
            download.filename = suggested_name;
            download.total_bytes = ec ? 0 : static_cast<int64_t>(file_size);
            download.received_bytes = ec ? 0 : static_cast<int64_t>(file_size);
            download.status = "completed";
            download.mime_type = mime_type.empty() ? "application/octet-stream" : mime_type;
            handler->store_->UpdateDownload(download);
            handler->NotifyDownloadsChanged();
            handler->NotifyDownloadBadge();
          }
        }
        callback->Success("");
        return true;
      }
      // Fetched doc held in memory: write the bytes we already have to the
      // download target instead of re-fetching from the network.
      if (handler) {
        const std::string content_url =
            handler->GetDocPreviewContentUrlForTab(tab_id);
        static const std::string kContentPrefix =
            "browser://doc-preview/content/";
        if (content_url.rfind(kContentPrefix, 0) == 0) {
          const std::string token = content_url.substr(kContentPrefix.size());
          std::vector<uint8_t> bytes;
          std::string mem_mime;
          if (otf::GetDocContentBytes(token, &bytes, &mem_mime)) {
            const std::string raw_name =
                token.substr(token.find_last_of('/') + 1);
            const std::string suggested_name =
                SanitizeFilename(raw_name.empty() ? "download.txt" : raw_name);
            const std::string target_path =
                otf::BuildDownloadPath(suggested_name);
            if (!otf::WriteFileBinary(target_path, bytes.data(),
                                      bytes.size())) {
              callback->Failure(0, "Could not save document file");
              return true;
            }
            const std::string save_mime =
                mem_mime.empty() ? "application/octet-stream" : mem_mime;
            if (handler->store_) {
              const int download_id = handler->store_->CreateDownload(
                  download_url, target_path, target_path, suggested_name,
                  save_mime, "completed");
              if (download_id > 0) {
                PersistedDownload download;
                download.id = download_id;
                download.url = download_url;
                download.original_url = target_path;
                download.target_path = target_path;
                download.filename = suggested_name;
                download.total_bytes = static_cast<int64_t>(bytes.size());
                download.received_bytes = static_cast<int64_t>(bytes.size());
                download.status = "completed";
                download.mime_type = save_mime;
                handler->store_->UpdateDownload(download);
                handler->NotifyDownloadsChanged();
                handler->NotifyDownloadBadge();
              }
            }
            callback->Success("");
            return true;
          }
        }
      }
      if (download_url.rfind("file://", 0) == 0) {
        callback->Failure(1, "file scheme is disabled");
        return true;
      }
      browser->GetHost()->StartDownload(download_url);
      callback->Success("");
      return true;
    }

    if (msg == "hide-imagepreview" || msg.rfind("hide-imagepreview:", 0) == 0) {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideImagePreviewOverlay();
        int tab_id = -1;
        if (msg.rfind("hide-imagepreview:", 0) == 0) {
          const auto tab_id_opt =
              ParseIntStrict(std::string_view(msg).substr(std::strlen("hide-imagepreview:")));
          if (tab_id_opt) {
            tab_id = *tab_id_opt;
          }
        }
        if (tab_id == -1) {
          tab_id = app->GetCurrentTabId();
        }
        if (handler) {
          handler->ClearInlineImagePreviewForTab(tab_id);
        }
      }
      callback->Success("");
      return true;
    }

    if (msg == "close-imagepreview" || msg.rfind("close-imagepreview:", 0) == 0) {
      OtfApp* app = OtfApp::GetInstance();
      if (!app || !handler) {
        callback->Success("");
        return true;
      }
      int tab_id = -1;
      if (msg.rfind("close-imagepreview:", 0) == 0) {
        const auto tab_id_opt =
            ParseIntStrict(std::string_view(msg).substr(std::strlen("close-imagepreview:")));
        if (tab_id_opt) {
          tab_id = *tab_id_opt;
        }
      }
      if (tab_id == -1) {
        tab_id = ResolveRealTabIdForBrowser(browser, handler->tab_manager_);
      }
      if (tab_id == -1 && app) {
        tab_id = app->GetCurrentTabId();
      }
      const bool is_dedicated_preview_tab =
          handler->tab_manager_ &&
          tab_id != -1 &&
          handler->tab_manager_->GetImagePreviewMode(tab_id) ==
              ImagePreviewMode::kDedicated;
      if (is_dedicated_preview_tab) {
        handler->CloseTabAndNotify(tab_id);
      } else {
        handler->ClearInlineImagePreviewForTab(tab_id);
        app->HideImagePreviewOverlay();
      }
      callback->Success("");
      return true;
    }

    if (msg.rfind("download-image:", 0) == 0) {
      std::string download_url = msg.substr(15);
      int explicit_tab_id = -1;
      const size_t tab_colon = msg.find(':', 15);
      if (tab_colon != std::string::npos) {
        const auto tab_opt =
            ParseIntStrict(std::string_view(msg).substr(15, tab_colon - 15));
        if (tab_opt && *tab_opt >= 0) {
          explicit_tab_id = *tab_opt;
          download_url = msg.substr(tab_colon + 1);
        }
      }
      int tab_id = -1;
      std::string local_path;
      if (download_url.rfind("browser://image-preview/", 0) == 0 || download_url.empty()) {
        if (handler && handler->tab_manager_) {
          tab_id = ResolvePreviewTabId(browser, handler->tab_manager_, explicit_tab_id);
          std::string mapped_url = handler->GetImagePreviewUrlForTab(tab_id);
          if (!mapped_url.empty()) {
            download_url = mapped_url;
          }
          local_path = handler->GetImagePreviewLocalFileForTab(tab_id);
        }
      }
      if (!local_path.empty()) {
        std::error_code ec;
        const std::filesystem::path source_path(local_path);
        const std::string suggested_name =
            source_path.filename().empty()
                ? "download.tiff"
                : SanitizeFilename(source_path.filename().string());
        const std::string target_path = otf::BuildDownloadPath(suggested_name);
        std::filesystem::copy_file(local_path, target_path,
                                   std::filesystem::copy_options::none, ec);
        if (ec) {
          callback->Failure(0, "Could not save TIFF file");
          return true;
        }
        if (handler->store_) {
          const auto file_size = std::filesystem::file_size(local_path, ec);
          const int download_id = handler->store_->CreateDownload(
              download_url, local_path, target_path, suggested_name,
              "image/tiff", "completed");
          if (download_id > 0) {
            PersistedDownload download;
            download.id = download_id;
            download.url = download_url;
            download.original_url = local_path;
            download.target_path = target_path;
            download.filename = suggested_name;
            download.total_bytes = ec ? 0 : static_cast<int64_t>(file_size);
            download.received_bytes = ec ? 0 : static_cast<int64_t>(file_size);
            download.status = "completed";
            download.mime_type = "image/tiff";
            handler->store_->UpdateDownload(download);
            handler->NotifyDownloadsChanged();
            handler->NotifyDownloadBadge();
          }
        }
        callback->Success("");
        return true;
      }
      if (download_url.rfind("file://", 0) == 0) {
        callback->Failure(1, "file scheme is disabled");
        return true;
      }
      browser->GetHost()->StartDownload(download_url);
      callback->Success("");
      return true;
    }

    if (msg.rfind("get-image-size:", 0) == 0) {
      std::string img_url = msg.substr(15);
      int explicit_tab_id = -1;
      const size_t tab_colon = msg.find(':', 15);
      if (tab_colon != std::string::npos) {
        const auto tab_opt =
            ParseIntStrict(std::string_view(msg).substr(15, tab_colon - 15));
        if (tab_opt && *tab_opt >= 0) {
          explicit_tab_id = *tab_opt;
          img_url = msg.substr(tab_colon + 1);
        }
      }
      if (img_url.rfind("browser://image-preview/", 0) == 0 || img_url.empty()) {
        if (handler && handler->tab_manager_) {
          int tab_id = ResolvePreviewTabId(browser, handler->tab_manager_, explicit_tab_id);
          std::string mapped_url = handler->GetImagePreviewUrlForTab(tab_id);
          if (!mapped_url.empty()) {
            img_url = mapped_url;
          }
          const std::string local_path =
              handler->GetImagePreviewLocalFileForTab(tab_id);
          if (!local_path.empty()) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(local_path, ec);
            if (!ec) {
              callback->Success(std::to_string(size));
            } else {
              callback->Failure(0, "Could not read downloaded file size");
            }
            return true;
          }
        }
      }

      if (img_url.rfind("file://", 0) == 0) {
        callback->Failure(1, "file scheme is disabled");
        return true;
      }
      if (img_url.rfind("http://", 0) == 0 || img_url.rfind("https://", 0) == 0) {
        CefRefPtr<CefRequestContext> request_context =
            browser ? browser->GetHost()->GetRequestContext()
                    : CefRequestContext::GetGlobalContext();
        CefRefPtr<CefRequest> head_request = CefRequest::Create();
        head_request->SetURL(img_url);
        head_request->SetMethod("HEAD");
        CefRefPtr<OtfSizeRequestClient> client = new OtfSizeRequestClient(callback);
        CefURLRequest::Create(head_request, client, request_context);
        return true;
      }
      callback->Failure(0, "Unsupported scheme");
      return true;
    }

    if (msg.rfind("preview-image:", 0) == 0 ||
        msg.rfind("preview-image-thumb:", 0) == 0 ||
        msg.rfind("decode-tiff:", 0) == 0) {
      const bool legacy_decode_request = msg.rfind("decode-tiff:", 0) == 0;
      const bool thumbnail_request = msg.rfind("preview-image-thumb:", 0) == 0;
      const size_t prefix_len =
          legacy_decode_request ? std::strlen("decode-tiff:")
          : thumbnail_request ? std::strlen("preview-image-thumb:")
                              : std::strlen("preview-image:");
      uint64_t decode_nonce = 0;
      int page_index = 0;
      int explicit_tab_id = -1;
      std::string source_url;
      const size_t first_colon = msg.find(':', prefix_len);
      if (first_colon != std::string::npos) {
        const auto nonce_opt = ParseUint64Strict(
            std::string_view(msg).substr(prefix_len, first_colon - prefix_len));
        if (nonce_opt) {
          decode_nonce = *nonce_opt;
          const size_t page_start = first_colon + 1;
          const size_t second_colon = msg.find(':', page_start);
          if (second_colon != std::string::npos) {
            const auto page_opt = ParseIntStrict(
                std::string_view(msg).substr(page_start, second_colon - page_start));
            if (page_opt && *page_opt >= 0) {
              page_index = *page_opt;
              const size_t third_colon = msg.find(':', second_colon + 1);
              if (third_colon != std::string::npos) {
                const auto tab_opt = ParseIntStrict(
                    std::string_view(msg).substr(second_colon + 1,
                                                third_colon - second_colon - 1));
                if (tab_opt && *tab_opt >= 0) {
                  explicit_tab_id = *tab_opt;
                  source_url = msg.substr(third_colon + 1);
                } else {
                  source_url = msg.substr(second_colon + 1);
                }
              } else {
                source_url = msg.substr(second_colon + 1);
              }
            } else {
              source_url = msg.substr(page_start);
            }
          } else {
            source_url = msg.substr(page_start);
          }
        } else {
          const size_t second_colon = msg.find(':', first_colon + 1);
          if (second_colon != std::string::npos) {
            const auto page_opt = ParseIntStrict(
                std::string_view(msg).substr(first_colon + 1, second_colon - first_colon - 1));
            if (page_opt && *page_opt >= 0) {
              page_index = *page_opt;
              source_url = msg.substr(second_colon + 1);
            } else {
              source_url = msg.substr(first_colon + 1);
            }
          } else {
            source_url = msg.substr(first_colon + 1);
          }
        }
      } else {
        source_url = msg.substr(prefix_len);
      }

      // Resolve the active tab id (overlay browser is not in tab_manager;
      // fall back to the current tab so we still persist navigation state).
      int preview_tab_id = -1;
      if (explicit_tab_id >= 0) {
        preview_tab_id = explicit_tab_id;
      }
      if (handler) {
        if (preview_tab_id == -1 && handler->tab_manager_) {
          preview_tab_id = handler->tab_manager_->GetId(browser);
        }
        if (preview_tab_id == -1) {
          OtfApp* app = OtfApp::GetInstance();
          if (app) preview_tab_id = app->GetCurrentTabId();
        }
      }
      if (preview_tab_id == -1 && handler) {
        for (const auto& [tab_id, stored_url] : handler->tab_image_preview_urls_) {
          if (stored_url == source_url) {
            preview_tab_id = tab_id;
            break;
          }
        }
      }

      if (source_url.rfind("browser://image-preview/", 0) == 0 || source_url.empty()) {
        if (handler && preview_tab_id != -1) {
          std::string mapped_url = handler->GetImagePreviewUrlForTab(preview_tab_id);
          if (!mapped_url.empty()) {
            source_url = mapped_url;
          }
        }
      }

      if (handler && preview_tab_id != -1) {
        const std::string local_path =
            handler->GetImagePreviewLocalFileForTab(preview_tab_id);
        if (!local_path.empty()) {
          if (!otf::IsTiffUrl(source_url)) {
            callback->Failure(0, "Unsupported image scheme");
            return true;
          }
          std::string png_base64;
          int page_count = 1;
          std::string error_reason;
          if (DecodeLocalTiffPreview(local_path, page_index, &png_base64,
                                     &page_count, &error_reason)) {
            if (!thumbnail_request) {
              handler->SetImagePreviewPageForTab(preview_tab_id, page_index);
              handler->SetImagePreviewPageCountForTab(preview_tab_id, page_count);
            }
            const int64_t file_size = GetFileSizeBytes(local_path, &error_reason);
            if (file_size >= 0) {
              handler->SetImagePreviewFileSizeForTab(preview_tab_id, file_size);
              handler->SetImagePreviewFormatForTab(preview_tab_id, "TIFF");
            }
            const std::string token = BuildImageContentToken(
                preview_tab_id, "local-tiff", page_index, source_url + ".png");
            std::vector<uint8_t> png_bytes = DecodeDataUrlBytes(png_base64);
            if (png_bytes.empty()) {
              callback->Failure(0, "Failed to prepare decoded TIFF image");
              return true;
            }
            otf::RegisterImageContentBytes(token, std::move(png_bytes), "image/png");
            const std::string display_url =
                "browser://image-preview/content/" + token;
            if (!thumbnail_request) {
              handler->tab_image_preview_render_cache_[preview_tab_id] =
                  OtfHandler::ImagePreviewRenderCache{
                      local_path, display_url, page_index, page_count};
            }
            callback->Success(JsonObjectBuilder()
                                  .AddString("displayUrl", display_url)
                                  .AddInt("pageCount", page_count)
                                  .AddInt("currentPage", page_index)
                                  .AddInt("fileSizeBytes", file_size >= 0
                                                                ? static_cast<int>(std::min<int64_t>(file_size, std::numeric_limits<int>::max()))
                                                                : -1)
                                  .AddString("format", "TIFF")
                                  .Build());
          } else {
            callback->Failure(0, error_reason.empty()
                                   ? "Failed to decode downloaded TIFF file"
                                   : error_reason);
          }
          return true;
        }

        if (source_url.rfind("http://", 0) == 0 || source_url.rfind("https://", 0) == 0) {
          auto cache_it = handler->tab_image_preview_download_cache_.find(preview_tab_id);
          if (cache_it != handler->tab_image_preview_download_cache_.end() &&
              cache_it->second.source_url == source_url &&
              !cache_it->second.display_url.empty()) {
            callback->Success(handler->BuildImagePreviewLoadEvent(preview_tab_id, false));
            return true;
          }

          CefRefPtr<CefRequestContext> request_context =
              browser ? browser->GetHost()->GetRequestContext()
                      : CefRequestContext::GetGlobalContext();
          CefRefPtr<CefRequest> decode_request = CefRequest::Create();
          decode_request->SetURL(source_url);
          decode_request->SetMethod("GET");
          decode_request->SetFlags(UR_FLAG_ALLOW_STORED_CREDENTIALS | UR_FLAG_NO_RETRY_ON_5XX);
          CefRefPtr<OtfTiffDecodeClient> client =
              new OtfTiffDecodeClient(source_url, page_index, preview_tab_id,
                                      decode_nonce, thumbnail_request, callback);
          CefURLRequest::Create(decode_request, client, request_context);
          return true;
        }
      }

      if (source_url.rfind("file://", 0) == 0) {
        callback->Failure(1, "file scheme is disabled");
        return true;
      }
      callback->Failure(0, "Unsupported TIFF scheme");
      return true;
    }

    if (msg == "hide-bookmarkbar") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideBookmarkOverlay();
      }
      callback->Success("");
      return true;
    }

    if (msg == "get-workspaces") {
      if (!handler->store_) { callback->Success("[]"); return true; }
      if (handler->guest_session_active_) {
        callback->Success("[]");
        return true;
      }
      callback->Success(BuildWorkspacesJson(handler->store_->GetWorkspaces(),
                                            handler->active_workspace_id_));
      return true;
    }

    if (msg == "create-guest-session") {
      handler->StartGuestSession();
      callback->Success("");
      return true;
    }

    if (msg == "is-guest-session") {
      callback->Success(handler->guest_session_active_ ? "true" : "false");
      return true;
    }

    if (msg.rfind("create-workspace:", 0) == 0) {
      if (handler->guest_session_active_) {
        callback->Failure(1, "Workspaces are disabled in guest sessions");
        return true;
      }
      if (!handler->store_) { callback->Failure(1, "store unavailable"); return true; }
      std::string name = msg.substr(17);
      if (name.empty()) name = "Workspace";
      {
        const auto existing = handler->store_->GetWorkspaces();
        const bool dup = std::any_of(existing.begin(), existing.end(),
            [&name](const Workspace& w) { return w.name == name; });
        if (dup) { callback->Failure(1, "duplicate name"); return true; }
      }
      const int new_id = handler->store_->CreateWorkspace(name, "");
      if (new_id <= 0) { callback->Failure(1, "create failed"); return true; }
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "workspaces-updated")
                             .Build());
      callback->Success(std::to_string(new_id));
      return true;
    }

    if (msg.rfind("rename-workspace:", 0) == 0) {
      if (handler->guest_session_active_) {
        callback->Failure(1, "Workspaces are disabled in guest sessions");
        return true;
      }
      const std::string payload = msg.substr(17);
      const size_t colon = payload.find(':');
      if (colon == std::string::npos) { callback->Failure(1, "bad payload"); return true; }
      const auto id_opt = ParseIntStrict(std::string_view(payload).substr(0, colon));
      if (!id_opt) { callback->Failure(1, "bad id"); return true; }
      const std::string name = payload.substr(colon + 1);
      if (!handler->store_) { callback->Failure(1, "store unavailable"); return true; }
      {
        const auto existing = handler->store_->GetWorkspaces();
        const int rename_id = *id_opt;
        const bool dup = std::any_of(existing.begin(), existing.end(),
            [&name, rename_id](const Workspace& w) {
              return w.name == name && w.id != rename_id;
            });
        if (dup) { callback->Failure(1, "duplicate name"); return true; }
      }
      if (!handler->store_->RenameWorkspace(*id_opt, name)) {
        callback->Failure(1, "rename failed");
        return true;
      }
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "workspaces-updated")
                             .Build());
      callback->Success("");
      return true;
    }

    if (msg.rfind("delete-workspace:", 0) == 0) {
      if (handler->guest_session_active_) {
        callback->Failure(1, "Workspaces are disabled in guest sessions");
        return true;
      }
      const auto id_opt = ParseIntStrict(std::string_view(msg).substr(17));
      if (!id_opt) { callback->Failure(1, "bad id"); return true; }
      const int target = *id_opt;
      if (!handler->store_) { callback->Failure(1, "store unavailable"); return true; }
      const auto workspaces = handler->store_->GetWorkspaces();
      if (workspaces.size() <= 1) { callback->Failure(1, "cannot delete last"); return true; }

      // Close every tab that belongs to the doomed workspace. Done before
      // deleting the workspace row so the renderer sees tab-closed events
      // and prunes its state. If the active workspace is being deleted,
      // fall back to the first remaining workspace.
      if (handler->tab_manager_) {
        const auto tab_ids = handler->tab_manager_->GetTabIdsForWorkspace(target);
        for (int tid : tab_ids) {
          handler->CloseTabAndNotify(tid);
        }
      }

      if (handler->active_workspace_id_ == target) {
        int fallback = 1;
        for (const auto& w : workspaces) {
          if (w.id != target) { fallback = w.id; break; }
        }
        handler->active_workspace_id_ = fallback;
        handler->store_->SetActiveWorkspace(fallback);
      }

      handler->store_->DeleteWorkspace(target);
      handler->workspace_contexts_.erase(target);
      if (handler->tab_manager_) {
        handler->tab_manager_->ClearWorkspaceOriginZooms(target);
        handler->tab_manager_->ClearPrivateWorkspaceOriginZooms(target);
      }

      // Remove the on-disk request-context cache that was created for this
      // workspace (see GetWorkspaceRequestContext). Errors are silently
      // ignored — a leftover directory is a disk leak, not a correctness bug.
      {
        std::error_code ec;
        std::filesystem::remove_all(otf::GetWorkspaceCefCacheDir(target), ec);
      }

      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "workspaces-updated")
                             .Build());
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "workspace-changed")
                             .AddInt("id", handler->active_workspace_id_)
                             .Build());
      callback->Success("");
      return true;
    }

    if (msg.rfind("switch-workspace:", 0) == 0) {
      const auto id_opt = ParseIntStrict(std::string_view(msg).substr(17));
      if (!id_opt) { callback->Failure(1, "bad id"); return true; }
      const int target = *id_opt;
      if (target == handler->active_workspace_id_) { callback->Success(""); return true; }
      if (!handler->store_) { callback->Failure(1, "store unavailable"); return true; }
      if (target <= 0) {
        callback->Failure(1, "workspace not found");
        return true;
      }
      if (handler->guest_session_active_) {
        callback->Failure(1, "workspace switching is disabled in guest sessions");
        return true;
      }

      // Snapshot the outgoing workspace now, while GetCurrentTabId() still
      // points to a tab that belongs to it — that's the only moment was_active
      // is accurate for the workspace we're leaving.
      handler->PersistWorkspaceTabs(handler->active_workspace_id_);
      // Remember which tab was active so switching back restores it.
      if (OtfApp* _app = OtfApp::GetInstance()) {
        handler->workspace_last_active_tab_[handler->active_workspace_id_] =
            _app->GetCurrentTabId();
      }

      handler->active_workspace_id_ = target;
      handler->store_->SetActiveWorkspace(target);

      // Surface a tab from the new workspace. If live tabs exist in memory
      // switch to the last one that was active in this workspace (or the first
      // as a fallback). Otherwise restore from the persisted session; if
      // nothing was persisted either, open a fresh new-tab page.
      OtfApp* app = OtfApp::GetInstance();
      if (app && handler->tab_manager_) {
        auto tab_ids = handler->tab_manager_->GetTabIdsForWorkspace(target);
        if (!tab_ids.empty()) {
          int target_tab = tab_ids.front();
          const auto last_it = handler->workspace_last_active_tab_.find(target);
          if (last_it != handler->workspace_last_active_tab_.end()) {
            const int last = last_it->second;
            if (std::find(tab_ids.begin(), tab_ids.end(), last) != tab_ids.end()) {
              target_tab = last;
            }
          }
          app->SwitchTab(target_tab);
        } else {
          // No live tabs — check the persisted session for this workspace.
          const auto persisted = handler->store_->GetWorkspaceTabs(target);

          // Find the tab that was active when the workspace was last saved.
          auto active_it = std::find_if(persisted.begin(), persisted.end(),
              [](const WorkspaceTab& t) {
                return t.was_active && IsRestorableWorkspaceTab(t);
              });
          if (active_it == persisted.end()) {
            active_it = std::find_if(persisted.begin(), persisted.end(),
                [](const WorkspaceTab& t) {
                  return IsRestorableWorkspaceTab(t);
                });
          }

          if (active_it != persisted.end()) {
            // Create all persisted tabs and collect (db_position → tab_id) so
            // we can sort tab_order_ back into the original DB order afterwards.
            std::map<int, int> db_pos_to_tab_id;
            int active_tab_id = -1;
            for (auto it = persisted.begin(); it != persisted.end(); ++it) {
              if (!IsRestorableWorkspaceTab(*it)) continue;
              const int id = app->CreateRestoredTab(*it);
              handler->NotifyNewTab(id, -1);
              db_pos_to_tab_id[it->position] = id;
              if (it == active_it) active_tab_id = id;
            }
            // Restore original tab order (active tab was appended first which
            // puts it at the wrong position relative to the other tabs).
            if (db_pos_to_tab_id.size() > 1) {
              std::vector<int> sorted_ids;
              sorted_ids.reserve(db_pos_to_tab_id.size());
              for (auto& [_, tid] : db_pos_to_tab_id) sorted_ids.push_back(tid);
              handler->tab_manager_->SetWorkspaceTabOrder(target, sorted_ids);
            }
            if (active_tab_id >= 0) app->SwitchTab(active_tab_id);
          } else {
            // Nothing persisted — open a blank new-tab page.
            const int new_id = app->CreateTab("browser://newtab");
            handler->NotifyNewTab(new_id, -1);
            app->SwitchTab(new_id);
          }
        }
      }

      handler->ApplySplitViewState(target);
      handler->PersistWorkspaceTabs(target);
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "workspace-changed")
                             .AddInt("id", target)
                             .Build());
      callback->Success("");
      return true;
    }

    if (msg == "get-downloads") {
      callback->Success(handler->guest_session_active_ ? "[]" : handler->GetDownloadsJson());
      return true;
    }

    if (msg == "get-settings") {
      callback->Success(handler->guest_session_active_ ? "{}" : otf::LoadSettingsJson());
      return true;
    }

    if (msg == "get-current-certificate") {
      OtfApp* app = OtfApp::GetInstance();
      const int tab_id = app ? app->GetCurrentTabId() : -1;
      CefRefPtr<CefBrowser> tab_browser =
          handler->tab_manager_ ? handler->tab_manager_->GetBrowser(tab_id) : nullptr;
      callback->Success(
          BuildCurrentCertificateJson(tab_browser, handler, tab_id, nullptr, nullptr));
      return true;
    }

    if (msg.find("get-certificate-by-tab-id:") == 0) {
      const std::string tab_id_str = msg.substr(26);
      char* parse_end = nullptr;
      errno = 0;
      long parsed_tab_id = std::strtol(tab_id_str.c_str(), &parse_end, 10);
      int tab_id = (errno == 0 && parse_end != tab_id_str.c_str() && *parse_end == '\0')
                       ? static_cast<int>(parsed_tab_id)
                       : -1;
      CefRefPtr<CefBrowser> tab_browser =
          handler->tab_manager_ ? handler->tab_manager_->GetBrowser(tab_id) : nullptr;
      callback->Success(
          BuildCurrentCertificateJson(tab_browser, handler, tab_id, nullptr, nullptr));
      return true;
    }

    if (msg.find("set-settings:") == 0) {
      if (handler->guest_session_active_) {
        callback->Failure(1, "Settings are disabled in guest sessions");
        return true;
      }
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

    if (msg.find("reset-browser-data:") == 0) {
      if (handler->guest_session_active_) {
        callback->Failure(1, "Resetting browser data is disabled in guest sessions");
        return true;
      }
      ResetRequest reset_request;
      if (!ParseResetRequest(msg.substr(19), &reset_request)) {
        callback->Failure(1, "Invalid reset payload");
        return true;
      }

      CefRefPtr<ResetCompletionState> state =
          new ResetCompletionState(callback);

      const std::string current_settings = otf::LoadSettingsJson();
      const std::string reset_settings = BuildResetSettingsJson(current_settings);
      std::string normalized_settings;
      if (otf::SaveSettingsJson(reset_settings, &normalized_settings)) {
        state->MarkCompleted("settings");
        handler->SendEvent(JsonObjectBuilder()
                               .AddString("key", "settings-changed")
                               .AddRaw("settings", normalized_settings)
                               .Build());
      } else {
        state->MarkFailed("settings");
      }

      if (reset_request.history) {
        if (handler->store_ && handler->store_->ClearHistory()) {
          state->MarkCompleted("history");
          if (handler->tab_manager_) {
            for (int tab_id : handler->tab_manager_->GetAllTabIds()) {
              const std::string url = handler->tab_manager_->GetUrl(tab_id);
              if (IsPersistableWebUrl(url) && !IsInternalUiUrl(url)) {
                handler->tab_manager_->SetHistorySuppressedUrl(tab_id, url);
              }
            }
          }
        } else {
          state->MarkFailed("history");
        }
      }

      if (reset_request.bookmarks) {
        if (handler->store_ && handler->store_->ClearBookmarks()) {
          state->MarkCompleted("bookmarks");
          if (handler->tab_manager_) {
            for (int tab_id : handler->tab_manager_->GetAllTabIds()) {
              handler->NotifyBookmarkStateForTab(tab_id);
            }
          } else {
            handler->SendEvent(JsonObjectBuilder()
                                   .AddString("key", "bookmarks-changed")
                                   .AddBool("bookmarked", false)
                                   .Build());
          }
        } else {
          state->MarkFailed("bookmarks");
        }
      }

      if (reset_request.downloads) {
        if (handler->store_ && handler->store_->ClearDownloads()) {
          for (const auto& entry : handler->download_callbacks_) {
            if (entry.second) {
              entry.second->Cancel();
            }
          }
          handler->downloads_.clear();
          handler->download_callbacks_.clear();
          handler->runtime_download_ids_.clear();
          handler->NotifyDownloadsChanged();
          handler->NotifyDownloadBadge();
          state->MarkCompleted("downloads");
        } else {
          state->MarkFailed("downloads");
        }
      }

      if (reset_request.cookies) {
        CefRefPtr<CefCookieManager> cookie_manager =
            CefCookieManager::GetGlobalManager(nullptr);
        if (cookie_manager) {
          state->AddPending("cookies");
          cookie_manager->DeleteCookies("", "", new ResetAsyncCallback(state, "cookies"));
        } else {
          state->MarkFailed("cookies");
        }
      }

      CefRefPtr<CefRequestContext> request_context =
          CefRequestContext::GetGlobalContext();
      if (reset_request.cache && request_context) {
#if CEF_API_ADDED(14400)
        state->AddPending("cache");
        request_context->ClearHttpCache(new ResetAsyncCallback(state, "cache"));
#else
        state->MarkUnsupported("cache");
#endif
      } else if (reset_request.cache) {
        state->MarkFailed("cache");
      }

      if (reset_request.ssl && request_context) {
        state->AddPending("ssl-exceptions");
        request_context->ClearCertificateExceptions(
            new ResetAsyncCallback(state, "ssl-exceptions"));
        state->AddPending("http-auth");
        request_context->ClearHttpAuthCredentials(
            new ResetAsyncCallback(state, "http-auth"));
        state->AddPending("connections");
        request_context->CloseAllConnections(
            new ResetAsyncCallback(state, "connections"));
      } else if (reset_request.ssl) {
        state->MarkFailed("ssl");
      }

      if (reset_request.service_workers) {
        state->MarkUnsupported("serviceWorkers");
      }
      if (reset_request.permissions) {
        if (handler->store_) {
          handler->store_->ClearAllSitePermissions();
          state->MarkCompleted("permissions");
        } else {
          state->MarkFailed("permissions");
        }
      }
      if (reset_request.storage) {
        state->MarkUnsupported("storage");
      }
      if (reset_request.passwords) {
        state->MarkUnsupported("passwords");
      }

      state->MaybeFinalize();
      return true;
    }

    // --- Folder picker ---
    if (msg == "select-folder") {
      class FolderPickerCallback : public CefRunFileDialogCallback {
       public:
        explicit FolderPickerCallback(CefRefPtr<Callback> cb)
            : callback_(cb) {}
        void OnFileDialogDismissed(
            const std::vector<CefString>& file_paths) override {
          if (!file_paths.empty()) {
            callback_->Success(file_paths[0].ToString());
          } else {
            callback_->Failure(1, "cancelled");
          }
        }
       private:
        CefRefPtr<Callback> callback_;
        IMPLEMENT_REFCOUNTING(FolderPickerCallback);
      };
      browser->GetHost()->RunFileDialog(
          FILE_DIALOG_OPEN_FOLDER, "Select Directory", "",
          std::vector<CefString>(),
          new FolderPickerCallback(callback));
      return true;
    }

    // --- Storage paths ---
    if (msg == "get-storage-paths") {
      JsonObjectBuilder b;
      // Active (runtime) paths
      b.AddString("activeDataDir",
                   otf::PathToUtf8(otf::GetActiveDataDir()));
      b.AddString("activeCacheDir",
                   otf::PathToUtf8(otf::GetActiveCacheDir()));
      b.AddString("activeDownloadsDir",
                   otf::PathToUtf8(otf::GetActiveDownloadsDir()));
      // Configured (settings.json) paths
      const auto configured_cache = otf::GetConfiguredCacheDir();
      const auto configured_downloads = otf::GetConfiguredDownloadsDir();
      if (!configured_cache.empty()) {
        b.AddString("configuredCacheDir",
                     otf::PathToUtf8(configured_cache));
      }
      if (!configured_downloads.empty()) {
        b.AddString("configuredDownloadsDir",
                     otf::PathToUtf8(configured_downloads));
      }
      // Pending changes (restart required)
      const std::string pending_raw = otf::LoadPendingPathsJson();
      if (pending_raw != "{}") {
        CefRefPtr<CefValue> pending_parsed = CefParseJSON(pending_raw, JSON_PARSER_ALLOW_TRAILING_COMMAS);
        if (pending_parsed && pending_parsed->GetType() == VTYPE_DICTIONARY) {
          CefRefPtr<CefDictionaryValue> pdict = pending_parsed->GetDictionary();
          if (pdict->HasKey("cacheDir"))
            b.AddString("pendingCacheDir", pdict->GetString("cacheDir").ToString());
          if (pdict->HasKey("downloadDir"))
            b.AddString("pendingDownloadsDir", pdict->GetString("downloadDir").ToString());
        }
      }
      // Default (fallback) paths
      b.AddString("defaultCacheDir",
                   otf::PathToUtf8(otf::GetAppCacheDir()));
      b.AddString("defaultDownloadsDir",
                   otf::PathToUtf8(otf::GetDownloadsDir()));
      // Folder sizes in bytes
      const auto active_cache = otf::GetActiveCacheDir();
      const auto active_downloads = otf::GetActiveDownloadsDir();
      b.AddRaw("cacheSize", std::to_string(otf::GetDirectorySize(active_cache)));
      b.AddRaw("downloadsSize", std::to_string(otf::GetDirectorySize(active_downloads)));
      callback->Success(b.Build());
      return true;
    }

    if (msg.rfind("set-storage-path:", 0) == 0) {
      if (handler->guest_session_active_) {
        callback->Failure(1, "Storage paths cannot be changed in guest sessions");
        return true;
      }
      const std::string payload = msg.substr(17);
      CefRefPtr<CefValue> parsed = CefParseJSON(payload, JSON_PARSER_ALLOW_TRAILING_COMMAS);
      std::string purpose, path;
      if (parsed && parsed->GetType() == VTYPE_DICTIONARY) {
        CefRefPtr<CefDictionaryValue> d = parsed->GetDictionary();
        if (d->HasKey("purpose")) purpose = d->GetString("purpose").ToString();
        if (d->HasKey("path")) path = d->GetString("path").ToString();
      }
      if (purpose.empty() || path.empty()) {
        callback->Failure(1, "Invalid payload format");
        return true;
      }

      // Validate
      std::string error = otf::ValidateStoragePath(path, purpose);
      if (!error.empty()) {
        callback->Failure(1, error);
        return true;
      }

      // Save to settings.json via CefParseJSON manipulation
      std::string settings_raw = otf::LoadSettingsJson();
      CefRefPtr<CefValue> settings_root =
          CefParseJSON(settings_raw, JSON_PARSER_ALLOW_TRAILING_COMMAS);
      if (!settings_root || settings_root->GetType() != VTYPE_DICTIONARY) {
        callback->Failure(1, "Failed to read settings");
        return true;
      }
      CefRefPtr<CefDictionaryValue> settings_dict = settings_root->GetDictionary();
      const std::string key = (purpose == "cache") ? "cacheDir" : "downloadDir";
      settings_dict->SetString(key, path);

      CefRefPtr<CefValue> out_root = CefValue::Create();
      out_root->SetDictionary(settings_dict);
      CefString out_json = CefWriteJSON(out_root, JSON_WRITER_DEFAULT);
      if (out_json.empty()) {
        callback->Failure(1, "Failed to serialize settings");
        return true;
      }
      std::string normalized;
      if (!otf::NormalizeSettingsJson(out_json.ToString(), &normalized)) {
        callback->Failure(1, "Failed to normalize settings");
        return true;
      }
      if (!otf::SaveSettingsJson(normalized, nullptr)) {
        callback->Failure(1, "Failed to save settings");
        return true;
      }

      // Write pending_paths.json
      std::string pending = otf::LoadPendingPathsJson();
      if (pending == "{}") {
        pending = "{\"" + key + "\":\"" + otf::JsonEscape(path) + "\"}";
      } else {
        // Update existing pending JSON
        CefRefPtr<CefValue> pending_root =
            CefParseJSON(pending, JSON_PARSER_ALLOW_TRAILING_COMMAS);
        if (pending_root && pending_root->GetType() == VTYPE_DICTIONARY) {
          CefRefPtr<CefDictionaryValue> pending_dict = pending_root->GetDictionary();
          pending_dict->SetString(key, path);
          CefRefPtr<CefValue> pending_out = CefValue::Create();
          pending_out->SetDictionary(pending_dict);
          CefString pending_json = CefWriteJSON(pending_out, JSON_WRITER_DEFAULT);
          if (!pending_json.empty()) {
            pending = pending_json.ToString();
          }
        }
      }
      otf::SavePendingPathsJson(pending);

      // Notify settings changed
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "settings-changed")
                             .AddRaw("settings", normalized)
                             .Build());
      callback->Success("");
      return true;
    }

    // --- Clear browsing data ---
    if (msg == "get-site-usage-list") {
      std::vector<std::string> history_origins;
      if (handler->store_) {
        history_origins = handler->store_->GetDistinctOrigins();
      }
      CefRefPtr<CefCookieManager> mgr = CefCookieManager::GetGlobalManager(nullptr);
      if (mgr) {
        class CookieSizeVisitor : public CefCookieVisitor {
         public:
          CookieSizeVisitor(OtfHandler* handler,
                            CefRefPtr<CefBrowser> ui_browser,
                            CefRefPtr<Callback> cb,
                            std::vector<std::string> extra)
              : handler_(handler),
                ui_browser_(ui_browser),
                callback_(cb),
                extra_origins_(std::move(extra)) {}
          // CEF calls Visit on the UI thread and drops its last reference when
          // iteration ends — including the zero-cookie case where Visit never
          // runs — so the destructor is the reliable completion signal. It
          // hands off to the CDP stage that fills in per-origin quota usage.
          ~CookieSizeVisitor() {
            QuerySiteUsageOverCdp(handler_, ui_browser_, callback_,
                                  std::move(extra_origins_),
                                  std::move(cookie_sizes_),
                                  std::move(cookie_counts_));
          }
          bool Visit(const CefCookie& cookie, int count, int total,
                     bool& delete_cookie) override {
            delete_cookie = false;
            std::string domain = CefString(&cookie.domain).ToString();
            if (!domain.empty() && domain[0] == '.') domain = domain.substr(1);
            if (domain.empty()) return true;
            std::string origin =
                cookie.secure ? "https://" + domain : "http://" + domain;
            // Approximate cookie size: name + value + domain + path + overhead
            uint64_t size = CefString(&cookie.name).length()
                          + CefString(&cookie.value).length()
                          + CefString(&cookie.domain).length()
                          + CefString(&cookie.path).length() + 50;
            cookie_sizes_[origin] += size;
            cookie_counts_[origin] += 1;
            return true;
          }
         private:
          OtfHandler* handler_;
          CefRefPtr<CefBrowser> ui_browser_;
          CefRefPtr<Callback> callback_;
          std::vector<std::string> extra_origins_;
          std::map<std::string, uint64_t> cookie_sizes_;
          std::map<std::string, uint64_t> cookie_counts_;
          IMPLEMENT_REFCOUNTING(CookieSizeVisitor);
        };
        mgr->VisitAllCookies(new CookieSizeVisitor(
            handler, browser, callback, std::move(history_origins)));
      } else {
        QuerySiteUsageOverCdp(handler, browser, callback,
                              std::move(history_origins), {}, {});
      }
      return true;
    }

    if (msg == "get-storage-totals") {
      callback->Success(otf::BuildStorageTotalsJson());
      return true;
    }

    if (msg == "get-tracked-cookies") {
      // Inspect the active tab's cookie store — its request context is
      // workspace-specific. Without a live tab there is no browsing context
      // to inspect (the UI shell's store holds no user data), so fail loudly
      // instead of answering from the wrong store.
      CefRefPtr<CefBrowser> context_browser;
      if (OtfApp* app = OtfApp::GetInstance()) {
        const int current_id = app->GetCurrentTabId();
        if (handler->tab_manager_ && current_id >= 0) {
          context_browser = handler->tab_manager_->GetBrowser(current_id);
        }
      }
      if (!context_browser) {
        callback->Failure(1, "no active tab");
        return true;
      }
      CefRefPtr<CefCookieManager> mgr =
          context_browser->GetHost()->GetRequestContext()->GetCookieManager(nullptr);
      if (!mgr) {
        callback->Success("[]");
        return true;
      }
      class CookieInspectVisitor : public CefCookieVisitor {
       public:
        explicit CookieInspectVisitor(CefRefPtr<Callback> cb)
            : callback_(cb) {}
        // CEF calls Visit on the UI thread and drops its last reference when
        // iteration ends — including the zero-cookie case where Visit never
        // runs — so the destructor is the reliable completion signal.
        ~CookieInspectVisitor() { callback_->Success("[" + json_ + "]"); }
        bool Visit(const CefCookie& cookie, int count, int total,
                   bool& delete_cookie) override {
          delete_cookie = false;
          if (!json_.empty()) json_ += ",";
          json_ += JsonObjectBuilder()
                       .AddString("name", CefString(&cookie.name).ToString())
                       .AddString("domain", CefString(&cookie.domain).ToString())
                       .AddString("path", CookiePathForTracking(cookie))
                       .AddBool("secure", cookie.secure != 0)
                       .AddBool("httpOnly", cookie.httponly != 0)
                       .AddRaw("createdAt",
                               std::to_string(CefBaseTimeToUnixSeconds(cookie.creation)))
                       .AddRaw("lastAccessAt",
                               std::to_string(CefBaseTimeToUnixSeconds(cookie.last_access)))
                       .Build();
          return true;
        }
       private:
        CefRefPtr<Callback> callback_;
        std::string json_;
        IMPLEMENT_REFCOUNTING(CookieInspectVisitor);
      };
      mgr->VisitAllCookies(new CookieInspectVisitor(callback));
      return true;
    }

    if (msg.rfind("clear-browsing-data:", 0) == 0) {
      const std::string payload = msg.substr(20);
      std::vector<std::string> categories;
      std::string time_range = "all";
      CefRefPtr<CefValue> parsed = CefParseJSON(payload, JSON_PARSER_ALLOW_TRAILING_COMMAS);
      if (parsed && parsed->GetType() == VTYPE_DICTIONARY) {
        CefRefPtr<CefDictionaryValue> d = parsed->GetDictionary();
        if (d->HasKey("categories")) {
          CefRefPtr<CefListValue> list = d->GetList("categories");
          if (list) {
            for (size_t i = 0; i < list->GetSize(); ++i)
              categories.push_back(list->GetString(i).ToString());
          }
        }
        if (d->HasKey("timeRange")) {
          time_range = d->GetString("timeRange").ToString();
        }
      }
      auto has = [&](const std::string& cat) {
        return std::find(categories.begin(), categories.end(), cat) != categories.end();
      };

      // Compute cutoff timestamp (seconds since epoch) for time-filtered clears.
      int64_t cutoff = 0;
      if (time_range != "all") {
        const int64_t now = std::time(nullptr);
        if (time_range == "hour") cutoff = now - 3600;
        else if (time_range == "day") cutoff = now - 86400;
        else if (time_range == "week") cutoff = now - 604800;
        else if (time_range == "month") cutoff = now - 2592000;
      }

      // Resolve origins from history for time-filtered clears.
      std::vector<std::string> filtered_origins;
      if (cutoff > 0 && handler->store_) {
        filtered_origins = handler->store_->GetDistinctOriginsSince(
            cutoff, handler->active_workspace_id_);
      }

      if (has("cookies")) {
        CefRefPtr<CefRequestContext> req_ctx = browser ? browser->GetHost()->GetRequestContext() : nullptr;
        CefRefPtr<CefCookieManager> mgr = req_ctx ? req_ctx->GetCookieManager(nullptr) : nullptr;
        if (mgr) {
          if (cutoff > 0) {
            class TimeRangeCookieDeleter : public CefCookieVisitor {
             public:
              explicit TimeRangeCookieDeleter(int64_t cutoff) : cutoff_(cutoff) {}
              bool Visit(const CefCookie& cookie, int count, int total,
                         bool& delete_cookie) override {
                int64_t created_at = CefBaseTimeToUnixSeconds(cookie.creation);
                if (created_at <= 0) {
                  created_at = CefBaseTimeToUnixSeconds(cookie.last_access);
                }
                delete_cookie = created_at >= cutoff_;
                return true;
              }
             private:
              int64_t cutoff_;
              IMPLEMENT_REFCOUNTING(TimeRangeCookieDeleter);
            };
            mgr->VisitAllCookies(new TimeRangeCookieDeleter(cutoff));
          } else {
            mgr->DeleteCookies("", "", nullptr);
          }
        }
      }
      if (has("cache")) {
        CefRefPtr<CefRequestContext> req_ctx = browser ? browser->GetHost()->GetRequestContext() : nullptr;
        if (req_ctx) {
          if (!filtered_origins.empty()) {
            for (const auto& origin : filtered_origins) {
              CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
              params->SetString("origin", origin);
              params->SetString("storageTypes", "cache_storage");
              browser->GetHost()->ExecuteDevToolsMethod(0, "Storage.clearDataForOrigin", params);
            }
          } else if (cutoff == 0) {
            req_ctx->ClearHttpCache(nullptr);
          }
        }
      }
      if (has("history") && handler->store_) {
        if (cutoff > 0) {
          handler->store_->ClearHistorySince(cutoff, handler->active_workspace_id_);
        } else {
          handler->store_->ClearHistory(handler->active_workspace_id_);
        }
        if (handler->tab_manager_) {
          for (int tab_id : handler->tab_manager_->GetAllTabIds()) {
            const std::string url = handler->tab_manager_->GetUrl(tab_id);
            if (IsPersistableWebUrl(url) && !IsInternalUiUrl(url)) {
              handler->tab_manager_->SetHistorySuppressedUrl(tab_id, url);
            }
          }
        }
        handler->SendEvent(JsonObjectBuilder()
                               .AddString("key", "history-changed")
                               .Build());
      }
      if (has("downloads") && handler->store_) {
        if (cutoff > 0) {
          handler->store_->ClearDownloadsSince(cutoff);
        } else {
          handler->store_->ClearDownloads();
          for (const auto& entry : handler->download_callbacks_) {
            if (entry.second) entry.second->Cancel();
          }
          handler->downloads_.clear();
          handler->download_callbacks_.clear();
          handler->runtime_download_ids_.clear();
        }
        handler->NotifyDownloadsChanged();
        handler->NotifyDownloadBadge();
      }
      if (has("siteData")) {
        CefRefPtr<CefRequestContext> req_ctx_ssl = browser ? browser->GetHost()->GetRequestContext() : nullptr;
        if (req_ctx_ssl) {
          req_ctx_ssl->ClearHttpAuthCredentials(nullptr);
          req_ctx_ssl->ClearCertificateExceptions(nullptr);
        }
        if (handler->store_) {
          handler->store_->ClearAllSitePermissions();
        }
        CefRefPtr<CefRequestContext> req_ctx = browser ? browser->GetHost()->GetRequestContext() : nullptr;
        CefRefPtr<CefCookieManager> mgr = req_ctx ? req_ctx->GetCookieManager(nullptr) : nullptr;
        if (mgr && browser) {
          if (!filtered_origins.empty()) {
            for (const auto& origin : filtered_origins) {
              CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
              params->SetString("origin", origin);
              params->SetString("storageTypes",
                  "appcache,file_systems,indexeddb,local_storage,"
                  "shader_cache,websql,service_workers,cache_storage");
              browser->GetHost()->ExecuteDevToolsMethod(
                  0, "Storage.clearDataForOrigin", params);
            }
          } else if (cutoff == 0) {
            class StorageClearVisitor : public CefCookieVisitor {
             public:
              StorageClearVisitor(CefRefPtr<CefBrowser> b)
                  : browser_(b) {}
              bool Visit(const CefCookie& cookie, int count, int total,
                         bool& delete_cookie) override {
                delete_cookie = false;
                std::string domain = CefString(&cookie.domain).ToString();
                if (!domain.empty() && domain[0] == '.') domain = domain.substr(1);
                if (domain.empty()) return true;
                std::string origin =
                    cookie.secure ? "https://" + domain : "http://" + domain;
                if (origins_.insert(origin).second) {
                  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
                  params->SetString("origin", origin);
                  params->SetString("storageTypes",
                      "appcache,file_systems,indexeddb,local_storage,"
                      "shader_cache,websql,service_workers,cache_storage");
                  browser_->GetHost()->ExecuteDevToolsMethod(
                      0, "Storage.clearDataForOrigin", params);
                }
                return true;
              }
             private:
              CefRefPtr<CefBrowser> browser_;
              std::set<std::string> origins_;
              IMPLEMENT_REFCOUNTING(StorageClearVisitor);
            };
            mgr->VisitAllCookies(new StorageClearVisitor(browser));
          }
        }
      }

      callback->Success("");
      return true;
    }

    if (msg == "clear-all-browsing-data") {
      CefRefPtr<CefCookieManager> mgr = CefCookieManager::GetGlobalManager(nullptr);
      if (mgr) mgr->DeleteCookies("", "", nullptr);

      CefRefPtr<CefRequestContext> ctx = CefRequestContext::GetGlobalContext();
      ctx->ClearHttpCache(nullptr);
      ctx->ClearCertificateExceptions(nullptr);
      ctx->ClearHttpAuthCredentials(nullptr);

      handler->store_->ClearAllSitePermissions();

      callback->Success("");
      return true;
    }

    if (msg == "restart-browser") {
      if (!RestartBrowserProcess()) {
        callback->Failure(1, "Unable to restart browser");
        return true;
      }
      callback->Success("");
      handler->CloseAllBrowsers(false);
      return true;
    }

    if (msg.find("resolve-input-url:") == 0) {
      size_t cursor = 18;
      bool ok = false;
      const std::string input = ParseLengthPrefixedField(msg, &cursor, &ok);
      if (!ok || cursor != msg.size()) {
        callback->Failure(1, "Invalid resolve payload");
        return true;
      }
      // Resolve with whatever search engine we have (possibly empty). The
      // helper returns a search URL only in branches that genuinely needed
      // one; URL-shaped input (browser://, http(s)://, localhost:PORT, an
      // IP, or an obvious host like example.com/path) resolves without
      // touching the engine string. We then differentiate after the call.
      const std::optional<std::string> search_engine_id =
          otf::GetCurrentSearchEngineId();
      const std::string engine_id = search_engine_id.value_or("");
      std::string resolved_url;
      std::string dns_host;
      const bool resolved_without_dns = ResolveUserInputUrlWithoutDns(
          input, engine_id, &resolved_url, &dns_host);

      // If the input looks like a URL we can navigate to right now, do it —
      // no need for a search engine. This keeps a fresh install (no engine
      // chosen yet) usable for direct URLs like 'localhost:3000' instead of
      // bouncing the user to the settings page on every attempt.
      const bool looks_like_url =
          resolved_without_dns &&
          (resolved_url.rfind("http://", 0) == 0 ||
           resolved_url.rfind("https://", 0) == 0 ||
           resolved_url.rfind("browser://", 0) == 0 ||
           resolved_url.rfind("//", 0) == 0 ||
           HasExplicitScheme(resolved_url));
      if (looks_like_url) {
        callback->Success(resolved_url);
        return true;
      }

      // Past this point we either need to build a search URL or do DNS
      // disambiguation — both require a chosen search engine. Send the
      // user to settings if none is configured.
      if (!search_engine_id.has_value()) {
        callback->Success("browser://settings");
        return true;
      }
      if (resolved_without_dns) {
        // Search URL came back from the resolver (whitespace / ambiguous).
        callback->Success(resolved_url);
        return true;
      }
      CefRefPtr<CefRequestContext> request_context =
          browser ? browser->GetHost()->GetRequestContext()
                  : CefRequestContext::GetGlobalContext();
      if (!request_context) {
        callback->Success(otf::BuildSearchUrl(*search_engine_id,
                                              TrimWhitespaceCopy(input)));
        return true;
      }
      request_context->ResolveHost(
          "https://" + dns_host,
          new UserInputResolveCallback(callback, input, *search_engine_id));
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
      if (IsDangerousSchemeUrl(url)) {
        callback->Failure(1, "dangerous scheme");
        return true;
      }
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
      if (colon == std::string::npos) {
        callback->Failure(1, "invalid id"); return true;
      }
      const auto tab_id_opt = ParseIntStrict(std::string_view(msg).substr(9, colon - 9));
      if (!tab_id_opt) { callback->Failure(1, "invalid id"); return true; }
      const int tab_id = *tab_id_opt;
      std::string url = msg.substr(colon + 1);
      if (IsDangerousSchemeUrl(url)) {
        callback->Failure(1, "dangerous scheme");
        return true;
      }
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) {
        if (url.find("browser://") == 0) {
          handler->tab_manager_->SetSchemeUrl(tab_id, url);
        }
        b->GetMainFrame()->LoadURL(url);
        // Move input focus to the page content after an address-bar
        // navigation, so keyboard/scroll work immediately without a manual
        // click — matching the other navigation/tab-switch paths.
        if (OtfApp* app = OtfApp::GetInstance()) {
          app->FocusCurrentTabContent();
        }
      }
      callback->Success("");
    } else if (msg == "new-tab:") {
      OtfApp* app = OtfApp::GetInstance();
      if (!app) { callback->Failure(1, "App not ready"); return true; }
      int id = app->CreateTab("browser://newtab");
      handler->NotifyNewTab(id, -1);
      app->SwitchTab(id);
      handler->PersistWorkspaceForTab(id);
      callback->Success(std::to_string(id));
    } else if (msg.find("new-tab:") == 0) {
      std::string url = msg.substr(8);
      OtfApp* app = OtfApp::GetInstance();
      if (!app) { callback->Failure(1, "App not ready"); return true; }
      int id = app->CreateTab(url);
      handler->NotifyNewTab(id, -1);
      app->SwitchTab(id);
      handler->PersistWorkspaceForTab(id);
      callback->Success(std::to_string(id));
    } else if (msg == "new-private-tab:") {
      OtfApp* app = OtfApp::GetInstance();
      if (!app) { callback->Failure(1, "App not ready"); return true; }
      int id = app->CreateTab("browser://newtab", -1, /*is_private=*/true);
      handler->NotifyNewTab(id, -1);
      app->SwitchTab(id);
      // Private tabs are never persisted to the workspace session.
      callback->Success(std::to_string(id));
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
    } else if (msg == "toggle-fullscreen") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->ToggleFullscreen();
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
    } else if (msg == "toggle-certificate") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        if (app->certificate_overlay_ && app->certificate_overlay_->IsVisible()) {
          app->HideCertificateOverlay();
        } else {
          app->ShowCertificateOverlay();
        }
      }
      callback->Success("");
    } else if (msg == "hide-certificate") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideCertificateOverlay();
      }
      callback->Success("");
    } else if (msg.rfind("show-popup:", 0) == 0) {
      const std::string name = msg.substr(11);
      OtfApp* app = OtfApp::GetInstance();
      otf::PopupOverlay* popup = app ? app->GetPopup(name) : nullptr;
      if (popup) {
        if (name == "blockedpopup") {
          int* pid = &handler->popup_ask_pending_id_;
          std::string* purl = &handler->popup_ask_pending_url_;
          std::string* porigin = &handler->popup_ask_pending_origin_;
          popup->SetRestoreProducer([pid, purl, porigin]() {
            return JsonObjectBuilder()
                .AddInt("id", *pid)
                .AddString("url", *purl)
                .AddString("origin", *porigin)
                .Build();
          });
        }
        popup->Show();
      }
      callback->Success(popup ? "ok" : "no-such-popup");
      return true;
    } else if (msg.rfind("hide-popup:", 0) == 0) {
      const std::string name = msg.substr(11);
      OtfApp* app = OtfApp::GetInstance();
      otf::PopupOverlay* popup = app ? app->GetPopup(name) : nullptr;
      if (popup) popup->Hide();
      callback->Success(popup ? "ok" : "no-such-popup");
      return true;
    } else if (msg.rfind("toggle-popup:", 0) == 0) {
      const std::string name = msg.substr(13);
      OtfApp* app = OtfApp::GetInstance();
      otf::PopupOverlay* popup = app ? app->GetPopup(name) : nullptr;
      if (popup) popup->Toggle();
      callback->Success(popup ? "ok" : "no-such-popup");
      return true;
    } else if (msg.rfind("popup-restore:", 0) == 0) {
      const std::string name = msg.substr(14);
      OtfApp* app = OtfApp::GetInstance();
      otf::PopupOverlay* popup = app ? app->GetPopup(name) : nullptr;
      if (popup) popup->SetRestoreSubscriber(callback);
      return true;
    } else if (msg.rfind("show-clear-site-data:", 0) == 0) {
      const std::string origin = ParseSiteDataOrigin(msg.substr(21));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      handler->pending_cleardata_origin_ = origin;
      OtfApp* app = OtfApp::GetInstance();
      otf::PopupOverlay* popup = app ? app->GetPopup("cleardata") : nullptr;
      if (popup) {
        // Wire the restore producer so PopupOverlay::Show() publishes the
        // current origin to the renderer. Producer reads the pending field
        // by pointer so the popup always reflects the latest show request.
        std::string* pending = &handler->pending_cleardata_origin_;
        popup->SetRestoreProducer([pending]() {
          return JsonObjectBuilder().AddString("origin", *pending).Build();
        });
        popup->Show();
      }
      callback->Success("");
      return true;
    } else if (msg.rfind("show-qr:", 0) == 0) {
      const std::string raw_url = msg.substr(8);
      handler->pending_qr_url_ = otf::StripTrackingParamsFromUrl(raw_url);
      OtfApp* app = OtfApp::GetInstance();
      otf::PopupOverlay* popup = app ? app->GetPopup("qr") : nullptr;
      if (popup) {
        std::string* pending = &handler->pending_qr_url_;
        popup->SetRestoreProducer([pending]() {
          return JsonObjectBuilder().AddString("url", *pending).Build();
        });
        popup->Show();
      }
      callback->Success("");
      return true;
    } else if (msg.rfind("open-site-data-page:", 0) == 0) {
      const std::string origin = ParseSiteDataOrigin(msg.substr(20));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        otf::PopupOverlay* popup = app->GetPopup("cleardata");
        if (popup) popup->Hide();
        // Encode the origin so colons/slashes survive the query string.
        std::string encoded;
        for (char c : origin) {
          if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
              c == '.' || c == '_' || c == '~') {
            encoded += c;
          } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X",
                          static_cast<unsigned char>(c));
            encoded += buf;
          }
        }
        // Open the inspector in the same context as the tab being inspected,
        // so a private tab's site-data page sees its own ephemeral session
        // rather than the global profile.
        const int active_id = app->GetCurrentTabId();
        const bool from_private =
            active_id >= 0 && handler->tab_manager_ &&
            handler->tab_manager_->IsPrivate(active_id);
        int id = app->CreateTab("browser://sitedata?origin=" + encoded, -1,
                                from_private);
        handler->NotifyNewTab(id, handler->tab_manager_->GetId(browser));
        app->SwitchTab(id);
      }
      callback->Success("");
      return true;
    } else if (msg.rfind("get-storage-for-site:", 0) == 0) {
      const std::string origin = ParseSiteDataOrigin(msg.substr(21));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      if (!handler->devtools_bridge_) {
        callback->Failure(1, "devtools bridge not attached");
        return true;
      }
      // Storage usage is browser-context-scoped, so run it against a browser
      // in the inspected tab's context (private tabs report their own
      // ephemeral storage instead of the global profile).
      handler->devtools_bridge_->Attach(handler->ResolveSiteDataBrowser(browser));
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("origin", origin);
      // Storage.getUsageAndQuota returns {usage, quota, usageBreakdown:[]}
      // — pass the result JSON straight through; the React side formats
      // the numbers.
      const int message_id = handler->devtools_bridge_->Execute(
          "Storage.getUsageAndQuota", params,
          [callback](bool ok, const std::string& result_json) {
            if (ok) callback->Success(result_json);
            else callback->Failure(1, result_json);
          });
      if (message_id == 0) callback->Failure(1, "Storage usage request failed");
      return true;
    } else if (msg.rfind("get-cookies-for-site:", 0) == 0) {
      const std::string origin = ParseSiteDataOrigin(msg.substr(21));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      // Visitor accumulates rows into a JSON array. CEF never calls Visit
      // when zero cookies match and drops its last reference when iteration
      // ends, so the destructor is the reliable completion signal — it
      // answers "[]" for the zero-cookie case with no fallback timer.
      class CookieListVisitor : public CefCookieVisitor {
       public:
        CookieListVisitor(CefRefPtr<Callback> cb) : callback_(cb) {}
        ~CookieListVisitor() { callback_->Success("[" + rows_ + "]"); }
        bool Visit(const CefCookie& cookie, int count, int total,
                   bool& delete_cookie) override {
          if (!rows_.empty()) rows_ += ",";
          rows_ += JsonObjectBuilder()
                       .AddString("name", CefString(&cookie.name).ToString())
                       .AddString("value", CefString(&cookie.value).ToString())
                       .AddString("domain", CefString(&cookie.domain).ToString())
                       .AddString("path", CefString(&cookie.path).ToString())
                       .AddBool("secure", cookie.secure != 0)
                       .AddBool("httpOnly", cookie.httponly != 0)
                       .Build();
          return true;
        }
       private:
        CefRefPtr<Callback> callback_;
        std::string rows_;
        IMPLEMENT_REFCOUNTING(CookieListVisitor);
      };
      CefRefPtr<CefBrowser> ctx_browser = handler->ResolveSiteDataBrowser(browser);
      CefRefPtr<CefCookieManager> mgr =
          ctx_browser ? ctx_browser->GetHost()->GetRequestContext()->GetCookieManager(nullptr)
                      : CefCookieManager::GetGlobalManager(nullptr);
      if (mgr) {
        mgr->VisitUrlCookies(origin, true, new CookieListVisitor(callback));
      } else {
        callback->Success("[]");
      }
      return true;
    } else if (msg.rfind("clear-cookies-for-site:", 0) == 0) {
      const std::string origin = ParseSiteDataOrigin(msg.substr(23));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      // CEF's DeleteCookies(url, "") only removes host cookies (not domain
      // cookies). Most sites set Domain=.example.com, which means a naive
      // call leaves them untouched. The reliable path: visit all cookies
      // matching the URL and set delete_cookie=true on each.
      class CookiePurgeVisitor : public CefCookieVisitor {
       public:
        CookiePurgeVisitor(CefRefPtr<Callback> cb) : callback_(cb) {}
        bool Visit(const CefCookie& cookie, int count, int total,
                   bool& delete_cookie) override {
          delete_cookie = true;
          ++deleted_;
          if (count + 1 >= total) Resolve();
          return true;
        }
        void Resolve() {
          if (resolved_) return;
          resolved_ = true;
          callback_->Success(std::to_string(deleted_));
        }
        void ResolveWatchdog() {
          if (resolved_) return;
          resolved_ = true;
          if (deleted_ == 0) {
            callback_->Success("0");
          } else {
            callback_->Failure(1, "Cookie clear timed out");
          }
        }
       private:
        CefRefPtr<Callback> callback_;
        int deleted_ = 0;
        bool resolved_ = false;
        IMPLEMENT_REFCOUNTING(CookiePurgeVisitor);
      };
      class FallbackTask : public CefTask {
       public:
        FallbackTask(CefRefPtr<CookiePurgeVisitor> v) : visitor_(v) {}
        void Execute() override { visitor_->ResolveWatchdog(); }
       private:
        CefRefPtr<CookiePurgeVisitor> visitor_;
        IMPLEMENT_REFCOUNTING(FallbackTask);
      };
      CefRefPtr<CookiePurgeVisitor> visitor = new CookiePurgeVisitor(callback);
      CefRefPtr<CefBrowser> ctx_browser = handler->ResolveSiteDataBrowser(browser);
      CefRefPtr<CefCookieManager> mgr =
          ctx_browser ? ctx_browser->GetHost()->GetRequestContext()->GetCookieManager(nullptr)
                      : CefCookieManager::GetGlobalManager(nullptr);
      if (mgr) {
        mgr->VisitUrlCookies(origin, true, visitor);
      } else {
        visitor->Resolve();
      }
      // Same 250ms fallback as the cookie-list visitor — covers the
      // zero-cookies case where Visit is never called.
      CefPostDelayedTask(TID_UI, new FallbackTask(visitor), 2000);
      return true;
    } else if (msg.rfind("clear-storage-for-site:", 0) == 0) {
      const std::string origin = ParseSiteDataOrigin(msg.substr(23));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      CefRefPtr<CefBrowser> ctx_browser = handler->ResolveSiteDataBrowser(browser);
      if (!ctx_browser || !handler->devtools_bridge_) {
        callback->Failure(1, "devtools bridge not attached");
        return true;
      }
      handler->devtools_bridge_->Attach(ctx_browser);
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("origin", origin);
      params->SetString(
          "storageTypes",
          "appcache,file_systems,indexeddb,local_storage,"
          "shader_cache,websql,service_workers,cache_storage");
      const int message_id = handler->devtools_bridge_->Execute(
          "Storage.clearDataForOrigin", params,
          [callback](bool ok, const std::string& result_json) {
            if (ok) callback->Success("ok");
            else callback->Failure(1, result_json);
          });
      if (message_id == 0) callback->Failure(1, "Storage clear request failed");
      return true;
    } else if (msg.rfind("clear-permissions-for-site:", 0) == 0) {
      const std::string origin = ParseSiteDataOrigin(msg.substr(27));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      if (handler->store_) {
        if (!handler->store_->ClearSitePermissions(origin)) {
          callback->Failure(1, "Failed to clear stored site permissions");
          return true;
        }
      }
      CefRefPtr<CefBrowser> ctx_browser = handler->ResolveSiteDataBrowser(browser);
      if (!ctx_browser || !handler->devtools_bridge_) {
        callback->Success("ok");
        return true;
      }
      handler->devtools_bridge_->Attach(ctx_browser);
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("origin", origin);
      const int message_id = handler->devtools_bridge_->Execute(
          "Browser.resetPermissions", params,
          [callback](bool ok, const std::string& result_json) {
            if (ok) callback->Success("ok");
            else callback->Failure(1, result_json);
          });
      if (message_id == 0) callback->Failure(1, "Permission reset request failed");
      return true;
    } else if (msg.rfind("get-cross-origin-resources:", 0) == 0) {
      const std::string origin = ParseSiteDataOrigin(msg.substr(27));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      std::lock_guard<std::mutex> lock(handler->cross_origin_mutex_);
      auto it = handler->cross_origin_resources_.find(origin);
      if (it == handler->cross_origin_resources_.end()) {
        callback->Success("[]");
      } else {
        std::string json = "[";
        bool first = true;
        for (const auto& res : it->second) {
          if (!first) json += ",";
          first = false;
          json += "\"" + otf::JsonEscape(res) + "\"";
        }
        json += "]";
        callback->Success(json);
      }
      return true;
    } else if (msg.rfind("get-permissions-for-site:", 0) == 0) {
      const std::string origin = ParseSiteDataOrigin(msg.substr(25));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      if (handler->store_ && !handler->guest_session_active_) {
        std::string json = handler->store_->GetSitePermissionsJson(origin);
        callback->Success(json);
      } else {
        callback->Success("{}");
      }
      return true;
    } else if (msg.rfind("set-permission-for-site:", 0) == 0) {
      if (handler->guest_session_active_) {
        callback->Failure(1, "Site permissions are disabled in guest sessions");
        return true;
      }
      const std::string payload = msg.substr(24);
      size_t last_colon = payload.rfind(':');
      if (last_colon != std::string::npos && last_colon > 0) {
        size_t sec_colon = payload.rfind(':', last_colon - 1);
        if (sec_colon != std::string::npos) {
          std::string origin = ParseSiteDataOrigin(payload.substr(0, sec_colon));
          std::string permission = payload.substr(sec_colon + 1, last_colon - sec_colon - 1);
          std::string setting = payload.substr(last_colon + 1);
          if (origin.empty() ||
              !IsValidSitePermissionSetting(permission, setting)) {
            callback->Failure(1, "invalid permission setting");
            return true;
          }

          if (handler->store_ &&
              !handler->store_->SetSitePermission(origin, permission, setting)) {
            callback->Failure(1, "Failed to save site permission");
            return true;
          }
          callback->Success("ok");
          return true;
        }
      }
      callback->Failure(1, "invalid payload");
      return true;
    } else if (msg.rfind("clear-all-for-site:", 0) == 0) {
      if (handler->guest_session_active_) {
        callback->Success("ok");
        return true;
      }
      const std::string origin = ParseSiteDataOrigin(msg.substr(19));
      if (origin.empty()) {
        callback->Failure(1, "invalid site origin");
        return true;
      }
      if (handler->store_) {
        if (!handler->store_->ClearSitePermissions(origin)) {
          callback->Failure(1, "Failed to clear stored site permissions");
          return true;
        }
      }
      class ClearAllState : public CefBaseRefCounted {
       public:
        explicit ClearAllState(CefRefPtr<Callback> cb) : callback_(cb) {}
        void AddPending() { ++pending_; }
        void Done(bool ok, const std::string& error = "") {
          if (resolved_) return;
          if (!ok) errors_.push_back(error.empty() ? "clear failed" : error);
          if (pending_ > 0) --pending_;
          if (pending_ == 0) Resolve();
        }
        void Resolve() {
          if (resolved_) return;
          if (pending_ > 0) return;
          resolved_ = true;
          if (errors_.empty()) {
            callback_->Success("ok");
            return;
          }
          std::string joined;
          for (const auto& error : errors_) {
            if (!joined.empty()) joined += "; ";
            joined += error;
          }
          callback_->Failure(1, joined);
        }
       private:
        CefRefPtr<Callback> callback_;
        int pending_ = 0;
        bool resolved_ = false;
        std::vector<std::string> errors_;
        IMPLEMENT_REFCOUNTING(ClearAllState);
      };
      CefRefPtr<ClearAllState> state = new ClearAllState(callback);

      // Clear storage + permissions through the bridge so this request resolves
      // only after Chromium reports completion.
      CefRefPtr<CefBrowser> cdp_browser = handler->ResolveSiteDataBrowser(browser);
      if (cdp_browser && handler->devtools_bridge_) {
        handler->devtools_bridge_->Attach(cdp_browser);
        CefRefPtr<CefDictionaryValue> storage_params =
            CefDictionaryValue::Create();
        storage_params->SetString("origin", origin);
        storage_params->SetString("storageTypes", "all");
        state->AddPending();
        const int storage_id = handler->devtools_bridge_->Execute(
            "Storage.clearDataForOrigin", storage_params,
            [state](bool ok, const std::string& result_json) {
              state->Done(ok, ok ? "" : result_json);
            });
        if (storage_id == 0) state->Done(false, "Storage clear request failed");

        CefRefPtr<CefDictionaryValue> perm_params =
            CefDictionaryValue::Create();
        perm_params->SetString("origin", origin);
        state->AddPending();
        const int permissions_id = handler->devtools_bridge_->Execute(
            "Browser.resetPermissions", perm_params,
            [state](bool ok, const std::string& result_json) {
              state->Done(ok, ok ? "" : result_json);
            });
        if (permissions_id == 0) {
          state->Done(false, "Permission reset request failed");
        }
      }
      class CookiePurgeVisitor : public CefCookieVisitor {
       public:
        CookiePurgeVisitor(CefRefPtr<ClearAllState> state) : state_(state) {}
        bool Visit(const CefCookie& cookie, int count, int total,
                   bool& delete_cookie) override {
          delete_cookie = true;
          ++deleted_;
          if (count + 1 >= total) Resolve();
          return true;
        }
        void Resolve() {
          if (resolved_) return;
          resolved_ = true;
          state_->Done(true);
        }
        void ResolveWatchdog() {
          if (resolved_) return;
          resolved_ = true;
          if (deleted_ == 0) {
            state_->Done(true);
          } else {
            state_->Done(false, "Cookie clear timed out");
          }
        }
       private:
        CefRefPtr<ClearAllState> state_;
        int deleted_ = 0;
        bool resolved_ = false;
        IMPLEMENT_REFCOUNTING(CookiePurgeVisitor);
      };
      class FallbackTask : public CefTask {
       public:
        FallbackTask(CefRefPtr<CookiePurgeVisitor> v) : visitor_(v) {}
        void Execute() override { visitor_->ResolveWatchdog(); }
       private:
        CefRefPtr<CookiePurgeVisitor> visitor_;
        IMPLEMENT_REFCOUNTING(FallbackTask);
      };
      state->AddPending();
      CefRefPtr<CookiePurgeVisitor> visitor = new CookiePurgeVisitor(state);
      CefRefPtr<CefBrowser> ctx_browser = handler->ResolveSiteDataBrowser(browser);
      CefRefPtr<CefCookieManager> mgr =
          ctx_browser ? ctx_browser->GetHost()->GetRequestContext()->GetCookieManager(nullptr)
                      : CefCookieManager::GetGlobalManager(nullptr);
      if (mgr) {
        mgr->VisitUrlCookies(origin, true, visitor);
      } else {
        visitor->Resolve();
      }
      CefPostDelayedTask(TID_UI, new FallbackTask(visitor), 2000);
      state->Resolve();
      return true;
    } else if (msg == "open-downloads-page") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideDownloadsOverlay();
        int id = app->CreateTab("browser://downloads");
        handler->NotifyNewTab(id, handler->tab_manager_->GetId(browser));
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
      const auto download_id = ParseUint32Strict(std::string_view(msg).substr(16));
      if (!download_id) { callback->Failure(1, "invalid id"); return true; }
      auto it = handler->download_callbacks_.find(*download_id);
      if (it != handler->download_callbacks_.end()) {
        it->second->Cancel();
      }
      callback->Success("");
    } else if (msg.find("pause-download:") == 0) {
      const auto download_id = ParseUint32Strict(std::string_view(msg).substr(15));
      if (!download_id) { callback->Failure(1, "invalid id"); return true; }
      auto it = handler->download_callbacks_.find(*download_id);
      if (it != handler->download_callbacks_.end()) {
        it->second->Pause();
      }
      callback->Success("");
    } else if (msg.find("resume-download:") == 0) {
      const auto download_id = ParseUint32Strict(std::string_view(msg).substr(16));
      if (!download_id) { callback->Failure(1, "invalid id"); return true; }
      auto it = handler->download_callbacks_.find(*download_id);
      if (it != handler->download_callbacks_.end()) {
        it->second->Resume();
      }
      callback->Success("");
    } else if (msg.find("open-download:") == 0) {
      if (handler->guest_session_active_) {
        callback->Success("");
        return true;
      }
      const auto download_id = ParseUint32Strict(std::string_view(msg).substr(14));
      if (!download_id) { callback->Failure(1, "invalid id"); return true; }
      auto it = handler->downloads_.find(*download_id);
      if (it != handler->downloads_.end() && !it->second.full_path.empty()) {
        std::string path = it->second.full_path;
        if (otf::IsSupportedImageUrl(path)) {
          OtfApp* app = OtfApp::GetInstance();
          if (app) {
            const std::string name = SanitizeFilename(
                DownloadDisplayName(it->second.suggested_name,
                                    it->second.full_path, it->second.url));
            const std::string public_url =
                "browser://image-preview/download/" +
                std::to_string(*download_id) + "/" + name;
            int new_id = app->CreateTab(public_url);
            handler->SetImagePreviewLocalFileForTab(new_id, public_url, path);
            if (handler->tab_manager_) {
              handler->tab_manager_->SetUrl(new_id, public_url);
              handler->tab_manager_->SetTitle(new_id, name);
              handler->tab_manager_->SetSchemeUrl(new_id, "browser://imagepreview");
              handler->tab_manager_->SetImagePreviewMode(
                  new_id, ImagePreviewMode::kDedicated);
            }
            handler->NotifyNewTab(new_id, -1);
            app->SwitchTab(new_id);
            CefPostTask(TID_UI, new DeferredImagePreviewPushTask(new_id));
            CefPostDelayedTask(TID_UI, new DeferredImagePreviewPushTask(new_id), 100);
            CefPostDelayedTask(TID_UI, new DeferredImagePreviewPushTask(new_id), 300);
          }
        } else if (otf::IsSupportedDocumentUrl(path)) {
          OtfApp* app = OtfApp::GetInstance();
          if (app) {
            const std::string name = SanitizeFilename(
                DownloadDisplayName(it->second.suggested_name,
                                    it->second.full_path, it->second.url));
            const std::string content_token =
                "download/" + std::to_string(*download_id) + "/" + name;
            const std::string content_url =
                "browser://doc-preview/content/" + content_token;
            const std::string public_url =
                "browser://doc-preview/download/" +
                std::to_string(*download_id) + "/" + name;
            const std::string mime_type = otf::GuessDocumentMimeType(path);
            otf::RegisterDocContent(content_token, path);
            int new_id = app->CreateTab(public_url);
            handler->SetDocPreviewLocalFileForTab(new_id, public_url, path);
            handler->SetDocPreviewContentUrlForTab(new_id, content_url);
            if (handler->tab_manager_) {
              handler->tab_manager_->SetUrl(new_id, public_url);
              handler->tab_manager_->SetTitle(new_id, name);
              handler->tab_manager_->SetSchemeUrl(new_id, "browser://docpreview");
              handler->tab_manager_->SetDocPreviewMode(
                  new_id, DocPreviewMode::kDedicated);
            }
            handler->NotifyNewTab(new_id, -1);
            app->SwitchTab(new_id);
          }
        } else {
          OpenPathWithSystemApp(path);
        }
      }
      callback->Success("");
    } else if (msg.find("show-download-in-folder:") == 0) {
      if (handler->guest_session_active_) {
        callback->Success("");
        return true;
      }
      const auto download_id = ParseUint32Strict(std::string_view(msg).substr(24));
      if (!download_id) { callback->Failure(1, "invalid id"); return true; }
      auto it = handler->downloads_.find(*download_id);
      if (it != handler->downloads_.end() && !it->second.full_path.empty()) {
        RevealPathInFolder(it->second.full_path);
      }
      callback->Success("");
    } else if (msg.find("retry-download:") == 0) {
      if (handler->guest_session_active_) {
        callback->Success("");
        return true;
      }
      const auto download_id = ParseUint32Strict(std::string_view(msg).substr(15));
      if (!download_id) { callback->Failure(1, "invalid id"); return true; }
      auto it = handler->downloads_.find(*download_id);
      if (it != handler->downloads_.end()) {
        const std::string retry_url = it->second.original_url.empty()
                                         ? it->second.url
                                         : it->second.original_url;
        if (!retry_url.empty() && browser) {
          browser->GetHost()->StartDownload(retry_url);
        }
      }
      callback->Success("");
    } else if (msg.find("copy-download-link:") == 0) {
      const auto download_id = ParseUint32Strict(std::string_view(msg).substr(19));
      if (!download_id) { callback->Failure(1, "invalid id"); return true; }
      auto it = handler->downloads_.find(*download_id);
      if (it != handler->downloads_.end()) {
        const std::string link = it->second.original_url.empty()
                                    ? it->second.url
                                    : it->second.original_url;
        WriteToClipboard(link);
        if (OtfApp* app = OtfApp::GetInstance()) {
          app->ShowToast("copy", "Download link copied");
        }
      }
      callback->Success("");
    } else if (msg == "clear-finished-downloads") {
      if (handler->guest_session_active_) {
        callback->Success("");
        return true;
      }
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
      if (c1 == std::string::npos) {
        callback->Failure(1, "invalid id"); return true;
      }
      const auto tab_id_opt = ParseIntStrict(std::string_view(msg).substr(5, c1 - 5));
      if (!tab_id_opt) { callback->Failure(1, "invalid id"); return true; }
      std::string text = msg.substr(c1 + 1);
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(*tab_id_opt);
      if (b) b->GetHost()->Find(text, true, false, false);  // findNext=false: initial search
      callback->Success("");
    } else if (msg.find("stop-find:") == 0) {
      const auto tab_id_opt = ParseIntStrict(std::string_view(msg).substr(10));
      if (!tab_id_opt) { callback->Failure(1, "invalid id"); return true; }
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(*tab_id_opt);
      if (b) b->GetHost()->StopFinding(true);
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
        app->FocusCurrentTabContent();
      }
      callback->Success("");
    } else if (msg == "subscribe-console") {
      handler->console_subscription_ = callback;
      // Send all buffered entries for the current tab on subscribe.
      OtfApp* app = OtfApp::GetInstance();
      if (app && handler->tab_manager_) {
        const int tab_id = app->GetCurrentTabId();
        const auto& logs = handler->tab_manager_->GetConsoleLogs(tab_id);
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
        for (const auto& e : logs) {
          std::string event =
              "{\"key\":\"console-entry\",\"tabId\":" + std::to_string(tab_id) +
              ",\"level\":" + std::to_string(e.level) +
              ",\"message\":\"" + esc(e.message) + "\"" +
              ",\"source\":\"" + esc(e.source) + "\"" +
              ",\"line\":" + std::to_string(e.line) +
              ",\"ts\":" + std::to_string(e.timestamp_ms) + "}";
          callback->Success(event);
        }
      }
      // Persistent — don't call Success again here; entries stream in via OnConsoleMessage.
      return true;
    } else if (msg == "start-snip") {
      OtfApp* app = OtfApp::GetInstance();
      if (!app || !handler->tab_manager_ || !handler->devtools_bridge_) {
        callback->Failure(1, "not ready");
        return true;
      }
      int cur = app->GetCurrentTabId();
      CefRefPtr<CefBrowser> target = handler->tab_manager_->GetBrowser(cur);
      if (!target) {
        callback->Failure(1, "no active tab");
        return true;
      }
      handler->devtools_bridge_->Attach(target);
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("format", "png");
      handler->devtools_bridge_->Execute(
          "Page.captureScreenshot", params,
          [handler](bool ok, const std::string& result_json) {
            if (!ok) return;
            OtfApp* app = OtfApp::GetInstance();
            if (!app || !handler->snip_preview_browser_) return;
            app->HideAppMenuOverlay();
            app->ShowSnipPreviewOverlay();
            const std::string js = "window.__otfSetSnipImage(" + result_json + ");";
            handler->snip_preview_browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
          });
      callback->Success("");
      return true;
    } else if (msg.rfind("capture-viewport:", 0) == 0) {
      const auto tab_id_opt = ParseIntStrict(
          std::string_view(msg).substr(std::strlen("capture-viewport:")));
      if (!tab_id_opt || !handler->tab_manager_) {
        callback->Failure(1, "invalid tab id");
        return true;
      }
      int tab_id = *tab_id_opt;
      CefRefPtr<CefBrowser> target = handler->tab_manager_->GetBrowser(tab_id);
      if (!target) {
        callback->Failure(1, "no browser for tab");
        return true;
      }
      if (!handler->devtools_bridge_) {
        callback->Failure(1, "devtools bridge not attached");
        return true;
      }
      handler->devtools_bridge_->Attach(target);
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("format", "png");
      handler->devtools_bridge_->Execute(
          "Page.captureScreenshot", params,
          [callback](bool ok, const std::string& result_json) {
            if (!ok) {
              callback->Failure(1, result_json);
              return;
            }
            callback->Success(result_json);
          });
      return true;
    } else if (msg == "hide-snip-preview") {
      OtfApp* app = OtfApp::GetInstance();
      if (app) {
        app->HideSnipPreviewOverlay();
        app->FocusCurrentTabContent();
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

std::string BuildSplitViewStateJson(const OtfHandler::SplitViewState& state) {
  JsonObjectBuilder builder;
  builder
      .AddBool("enabled", state.enabled)
      .AddInt("leftTabId", state.left_tab_id)
      .AddInt("rightTabId", state.right_tab_id)
      .AddInt("activeTabId", state.active_tab_id);
  if (!state.left_url.empty()) builder.AddString("leftUrl", state.left_url);
  if (!state.right_url.empty()) builder.AddString("rightUrl", state.right_url);
  if (!state.active_url.empty()) builder.AddString("activeUrl", state.active_url);
  return builder.Build();
}

bool ParseSplitViewStateJson(const std::string& json,
                             OtfHandler::SplitViewState* state) {
  if (!state || json.empty()) return false;
  CefRefPtr<CefValue> value = CefParseJSON(json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return false;
  CefRefPtr<CefDictionaryValue> dict = value->GetDictionary();
  if (!dict) return false;
  state->enabled = dict->HasKey("enabled") && dict->GetBool("enabled");
  state->left_tab_id = dict->HasKey("leftTabId") ? dict->GetInt("leftTabId") : -1;
  state->right_tab_id = dict->HasKey("rightTabId") ? dict->GetInt("rightTabId") : -1;
  state->active_tab_id = dict->HasKey("activeTabId") ? dict->GetInt("activeTabId") : -1;
  state->left_url = dict->HasKey("leftUrl") ? dict->GetString("leftUrl").ToString() : "";
  state->right_url = dict->HasKey("rightUrl") ? dict->GetString("rightUrl").ToString() : "";
  state->active_url = dict->HasKey("activeUrl") ? dict->GetString("activeUrl").ToString() : "";
  return true;
}

}  // namespace

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

OtfHandler::SplitViewState OtfHandler::GetSplitViewState(int workspace_id) const {
  auto it = workspace_split_states_.find(workspace_id);
  if (it != workspace_split_states_.end()) {
    return it->second;
  }
  SplitViewState state;
  if (store_ && workspace_id > 0) {
    const std::string json = store_->GetWorkspaceStateValue(workspace_id, "split");
    if (!json.empty()) {
      ParseSplitViewStateJson(json, &state);
    }
  }
  return state;
}

std::string OtfHandler::BuildSplitViewStateJson(int workspace_id) const {
  return ::otf::BuildSplitViewStateJson(GetSplitViewState(workspace_id));
}

bool OtfHandler::SetSplitViewTabs(int workspace_id,
                                  int left_tab_id,
                                  int right_tab_id,
                                  int active_tab_id) {
  if (workspace_id <= 0 || left_tab_id < 0 || right_tab_id < 0 ||
      left_tab_id == right_tab_id) {
    return ClearSplitViewState(workspace_id);
  }

  SplitViewState state;
  state.enabled = true;
  state.left_tab_id = left_tab_id;
  state.right_tab_id = right_tab_id;
  state.active_tab_id =
      (active_tab_id == right_tab_id) ? right_tab_id : left_tab_id;
  if (tab_manager_) {
    state.left_url = IsSplitPlaceholderTab(tab_manager_, left_tab_id)
                         ? ""
                         : tab_manager_->GetUrl(left_tab_id);
    state.right_url = IsSplitPlaceholderTab(tab_manager_, right_tab_id)
                          ? ""
                          : tab_manager_->GetUrl(right_tab_id);
    state.active_url = state.active_tab_id == right_tab_id
                           ? state.right_url
                           : state.left_url;
  }
  workspace_split_states_[workspace_id] = state;
  PersistWorkspaceSplitState(workspace_id);
  return true;
}

bool OtfHandler::ClearSplitViewState(int workspace_id) {
  if (workspace_id <= 0) return false;
  SplitViewState state;
  workspace_split_states_[workspace_id] = state;
  PersistWorkspaceSplitState(workspace_id);
  return true;
}

bool OtfHandler::ApplySplitViewState(int workspace_id) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !tab_manager_) return false;
  SplitViewState state = GetSplitViewState(workspace_id);
  if (!state.enabled) {
    app->ClearSplitView();
    return false;
  }
  const auto ids = tab_manager_->GetTabIdsForWorkspace(workspace_id);
  auto is_alive = [&ids](int tab_id) {
    return std::find(ids.begin(), ids.end(), tab_id) != ids.end();
  };
  auto is_matching_tab = [this, &is_alive](int tab_id, const std::string& url) {
    if (!is_alive(tab_id)) return false;
    return url.empty() || tab_manager_->GetUrl(tab_id) == url;
  };
  auto find_by_url = [this, &ids](const std::string& url) {
    if (url.empty()) return -1;
    for (int id : ids) {
      if (tab_manager_->GetUrl(id) == url) return id;
    }
    return -1;
  };

  if (!is_matching_tab(state.left_tab_id, state.left_url)) {
    state.left_tab_id = find_by_url(state.left_url);
  }
  if (!is_matching_tab(state.right_tab_id, state.right_url)) {
    state.right_tab_id = find_by_url(state.right_url);
  }
  if (!is_matching_tab(state.active_tab_id, state.active_url)) {
    if (!state.active_url.empty() && state.active_url == state.right_url) {
      state.active_tab_id = state.right_tab_id;
    } else {
      state.active_tab_id = state.left_tab_id;
    }
  }

  const bool left_alive = is_alive(state.left_tab_id);
  const bool right_alive = is_alive(state.right_tab_id);
  if (!left_alive || !right_alive) {
    ClearSplitViewState(workspace_id);
    app->ClearSplitView();
    NotifySplitStateChanged(workspace_id);
    return false;
  }
  workspace_split_states_[workspace_id] = state;
  PersistWorkspaceSplitState(workspace_id);
  app->OpenSplitView(state.left_tab_id, state.right_tab_id, state.active_tab_id);
  NotifySplitStateChanged(workspace_id);
  return true;
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

void OtfHandler::PersistWorkspaceSplitState(int workspace_id) {
  if (!store_ || workspace_id <= 0) return;
  const SplitViewState state = GetSplitViewState(workspace_id);
  if (!state.enabled) {
    store_->SetWorkspaceStateValue(workspace_id, "split", "");
    return;
  }
  if (!tab_manager_ ||
      IsSplitPlaceholderTab(tab_manager_, state.left_tab_id) ||
      IsSplitPlaceholderTab(tab_manager_, state.right_tab_id) ||
      !otf::IsPersistableWebUrl(state.left_url) ||
      !otf::IsPersistableWebUrl(state.right_url)) {
    store_->SetWorkspaceStateValue(workspace_id, "split", "");
    return;
  }
  store_->SetWorkspaceStateValue(workspace_id, "split",
                                 ::otf::BuildSplitViewStateJson(state));
}

void OtfHandler::NotifySplitStateChanged(int workspace_id) {
  if (workspace_id <= 0) return;
  SendEvent(JsonObjectBuilder()
                .AddString("key", "split-state-changed")
                .AddInt("workspaceId", workspace_id)
                .AddRaw("state", BuildSplitViewStateJson(workspace_id))
                .Build());
}

void OtfHandler::SyncSplitStateFromApp() {
  OtfApp* app = OtfApp::GetInstance();
  if (!app) return;
  if (!app->HasSplitView()) {
    if (active_workspace_id_ > 0) {
      ClearSplitViewState(active_workspace_id_);
      NotifySplitStateChanged(active_workspace_id_);
    }
    return;
  }
  SetSplitViewTabs(active_workspace_id_, app->GetSplitLeftTabId(),
                   app->GetSplitRightTabId(), app->GetCurrentTabId());
  NotifySplitStateChanged(active_workspace_id_);
}

bool OtfHandler::IsSplitTab(int tab_id) const {
  if (!tab_manager_ || tab_id < 0) return false;
  const int workspace_id = tab_manager_->GetWorkspaceId(tab_id);
  if (workspace_id <= 0) return false;
  const SplitViewState state = GetSplitViewState(workspace_id);
  return tab_id == state.left_tab_id || tab_id == state.right_tab_id;
}

bool OtfHandler::IsSplitActive() const {
  if (!tab_manager_) return false;
  return GetSplitViewState(active_workspace_id_).enabled;
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

// --- Helper: best-guess filename from a download URL. ---
static std::string ExtractDownloadName(const std::string& url) {
  // Strip query string / fragment.
  std::string::size_type q = url.find_first_of("?#");
  std::string path = q != std::string::npos ? url.substr(0, q) : url;
  // Last non-empty path segment.
  std::string::size_type s = path.rfind('/');
  if (s == std::string::npos) return {};
  std::string name = path.substr(s + 1);
  if (name.empty()) return {};
  // Basic URL-decode: %20 → space etc.
  std::string decoded;
  decoded.reserve(name.size());
  for (std::string::size_type i = 0; i < name.size(); ++i) {
    if (name[i] == '%' && i + 2 < name.size()) {
      char hi = name[i + 1];
      char lo = name[i + 2];
      int h = (hi >= '0' && hi <= '9') ? (hi - '0')
            : (hi >= 'A' && hi <= 'F') ? (hi - 'A' + 10)
            : (hi >= 'a' && hi <= 'f') ? (hi - 'a' + 10) : -1;
      int l = (lo >= '0' && lo <= '9') ? (lo - '0')
            : (lo >= 'A' && lo <= 'F') ? (lo - 'A' + 10)
            : (lo >= 'a' && lo <= 'f') ? (lo - 'a' + 10) : -1;
      if (h >= 0 && l >= 0) { decoded += static_cast<char>((h << 4) | l); i += 2; }
      else { decoded += name[i]; }
    } else {
      decoded += name[i];
    }
  }
  return decoded;
}

bool OtfHandler::CanDownload(CefRefPtr<CefBrowser> browser,
                             const CefString& url,
                             const CefString& request_method) {
  CEF_REQUIRE_UI_THREAD();
  (void)request_method;

  if (!store_) return true;

  // Downloads initiated from internal UI views (QR code, etc.) are internal
  // actions and must not be subject to page download permissions.
  CefRefPtr<CefBrowserView> _can_dl_view = CefBrowserView::GetForBrowser(browser);
  if (_can_dl_view && IsNonTabBrowserViewId(_can_dl_view->GetID())) {
    return true;
  }

  CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
  std::string page_origin =
      main_frame ? ExtractOrigin(main_frame->GetURL().ToString()) : "";
  if (page_origin.empty()) return true;

  // Check transient allow-once (set by permissions.download.allow).
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

    if (OtfApp* a = OtfApp::GetInstance()) {
      if (auto* ov = a->GetPopup("downloadrequest")) {
        ov->SetRestoreProducer([this]() {
          return JsonObjectBuilder()
              .AddString("url", download_ask_pending_url_)
              .AddString("origin", download_ask_pending_origin_)
              .AddString("name", download_ask_pending_name_)
              .Build();
        });
        ov->Show();
      }
    }
    return false;
  }

  return true;  // "allow"
}

std::string OtfHandler::GetDownloadsJson() const {
  return BuildDownloadsJson(downloads_);
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

bool OtfHandler::ApplyTabZoomAction(int tab_id, const std::string& action) {
  if (!tab_manager_) return false;
  CefRefPtr<CefBrowser> b = tab_manager_->GetBrowser(tab_id);
  if (!b) return false;

  double next_zoom = b->GetHost()->GetZoomLevel();
  if (action == "in") {
    next_zoom = otf::ZoomIn(next_zoom);
  } else if (action == "out") {
    next_zoom = otf::ZoomOut(next_zoom);
  } else if (action == "reset") {
    next_zoom = otf::ZoomReset();
  } else {
    return false;
  }

  b->GetHost()->SetZoomLevel(next_zoom);
  const int pct = ToRoundedZoomPercent(next_zoom);
  tab_manager_->SetZoomPercent(tab_id, pct);
  SaveWorkspaceOriginZoom(tab_manager_, store_.get(), tab_id, pct);
  SendEvent(BuildTabPropertyEvent(tab_id, "zoomPercent", std::to_string(pct)));
  if (zoombar_subscription_) {
    zoombar_subscription_->Success(BuildZoomUpdateEvent(tab_id, pct));
  }
  return true;
}

bool OtfHandler::SplitCurrentTab(std::string* error) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !tab_manager_) {
    if (error) *error = "App not ready";
    return false;
  }
  const int active_tab_id = app->GetCurrentTabId();
  if (active_tab_id < 0) {
    if (error) *error = "No active tab";
    return false;
  }
  auto existing_state = GetSplitViewState(active_workspace_id_);
  if (existing_state.enabled) {
    ApplySplitViewState(active_workspace_id_);
    return true;
  }
  const int secondary_tab_id =
      app->CreateTab("browser://split-placeholder", active_tab_id);
  NotifyNewTab(secondary_tab_id, active_tab_id);
  tab_manager_->SetTitle(secondary_tab_id, "Add a tab to split view");
  tab_manager_->SetSchemeUrl(secondary_tab_id, "browser://split-placeholder");
  app->OpenSplitView(active_tab_id, secondary_tab_id, active_tab_id);
  SetSplitViewTabs(active_workspace_id_, active_tab_id, secondary_tab_id,
                   active_tab_id);
  PersistWorkspaceForTab(active_tab_id);
  NotifySplitStateChanged(active_workspace_id_);
  return true;
}

bool OtfHandler::AddTabToSplit(int target_tab_id, std::string* error) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !tab_manager_) {
    if (error) *error = "App not ready";
    return false;
  }
  auto state = GetSplitViewState(active_workspace_id_);
  if (!state.enabled) {
    if (error) *error = "Split view inactive";
    return false;
  }
  const auto ids = tab_manager_->GetTabIdsForWorkspace(active_workspace_id_);
  if (std::find(ids.begin(), ids.end(), target_tab_id) == ids.end()) {
    if (error) *error = "Tab is not in the active workspace";
    return false;
  }
  if (target_tab_id == state.left_tab_id || target_tab_id == state.right_tab_id) {
    app->SwitchTab(target_tab_id);
    SyncSplitStateFromApp();
    return true;
  }
  const bool left_is_placeholder =
      IsSplitPlaceholderTab(tab_manager_, state.left_tab_id);
  const bool right_is_placeholder =
      IsSplitPlaceholderTab(tab_manager_, state.right_tab_id);
  const bool replace_right =
      right_is_placeholder ? true
                           : left_is_placeholder
                                 ? false
                                 : state.active_tab_id != state.right_tab_id;
  const int replaced_tab_id = replace_right ? state.right_tab_id : state.left_tab_id;
  const int next_left = replace_right ? state.left_tab_id : target_tab_id;
  const int next_right = replace_right ? target_tab_id : state.right_tab_id;
  app->OpenSplitView(next_left, next_right, target_tab_id);
  SetSplitViewTabs(active_workspace_id_, next_left, next_right, target_tab_id);
  app->ActivateSplitPane(target_tab_id, true);
  const bool should_close_placeholder =
      IsSplitPlaceholderTab(tab_manager_, replaced_tab_id);
  PersistWorkspaceForTab(target_tab_id);
  NotifySplitStateChanged(active_workspace_id_);
  if (should_close_placeholder) {
    CefPostDelayedTask(TID_UI, base::BindOnce([](int tab_id) {
      if (auto* handler = OtfHandler::GetInstance()) {
        handler->CloseTabAndNotify(tab_id);
      }
    }, replaced_tab_id), 100);
  }
  return true;
}

bool OtfHandler::CloseSplitView(std::string* error) {
  (void)error;
  OtfApp* app = OtfApp::GetInstance();
  const auto state = GetSplitViewState(active_workspace_id_);
  int placeholder_tab_id = -1;
  if (tab_manager_ && state.enabled) {
    if (IsSplitPlaceholderTab(tab_manager_, state.left_tab_id)) {
      placeholder_tab_id = state.left_tab_id;
    } else if (IsSplitPlaceholderTab(tab_manager_, state.right_tab_id)) {
      placeholder_tab_id = state.right_tab_id;
    }
  }
  if (app) {
    app->ClearSplitView();
  }
  ClearSplitViewState(active_workspace_id_);
  NotifySplitStateChanged(active_workspace_id_);
  if (placeholder_tab_id >= 0) {
    CloseTabAndNotify(placeholder_tab_id);
  }
  return true;
}

bool OtfHandler::SwapSplitView(std::string* error) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app) {
    if (error) *error = "App not ready";
    return false;
  }
  const auto state = GetSplitViewState(active_workspace_id_);
  if (!state.enabled) {
    if (error) *error = "Split view inactive";
    return false;
  }
  const int active_tab_id =
      app->GetCurrentTabId() == state.left_tab_id ? state.right_tab_id
                                                  : state.left_tab_id;
  app->OpenSplitView(state.right_tab_id, state.left_tab_id, active_tab_id);
  SetSplitViewTabs(active_workspace_id_, state.right_tab_id, state.left_tab_id,
                   active_tab_id);
  NotifySplitStateChanged(active_workspace_id_);
  return true;
}

bool OtfHandler::CloseSplitPane(const std::string& pane, std::string* error) {
  const auto state = GetSplitViewState(active_workspace_id_);
  if (!state.enabled) {
    if (error) *error = "Split view inactive";
    return false;
  }
  int tab_id = -1;
  if (pane == "left") {
    tab_id = state.left_tab_id;
  } else if (pane == "right") {
    tab_id = state.right_tab_id;
  } else {
    if (error) *error = "Invalid split pane";
    return false;
  }
  if (tab_manager_ && tab_manager_->IsPinned(tab_id)) {
    if (error) *error = "Pinned tabs cannot be closed";
    return false;
  }
  CloseTabAndNotify(tab_id);
  return true;
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

// ── Periodic per-tab memory logging ──

class MemoryLogTask : public CefTask {
 public:
  explicit MemoryLogTask(OtfHandler* handler) : handler_(handler) {}
  void Execute() override {
    if (handler_ && handler_->IsMemoryLoggingRunning()) {
      handler_->LogTabMemoryUsage();
      CefPostDelayedTask(TID_UI, new MemoryLogTask(handler_), 10000);
    }
  }
 private:
  OtfHandler* handler_;
  IMPLEMENT_REFCOUNTING(MemoryLogTask);
};

void OtfHandler::StartMemoryLogging() {
  CEF_REQUIRE_UI_THREAD();
  if (memory_log_running_ || !tab_manager_) return;
  memory_log_running_ = true;
  memory_task_manager_ = CefTaskManager::GetTaskManager();
  CefPostDelayedTask(TID_UI, new MemoryLogTask(this), 10000);
}

void OtfHandler::StopMemoryLogging() {
  CEF_REQUIRE_UI_THREAD();
  memory_log_running_ = false;
  memory_task_manager_ = nullptr;
}

void OtfHandler::LogTabMemoryUsage() {
  CEF_REQUIRE_UI_THREAD();
  if (!tab_manager_) return;

  auto tm = memory_task_manager_;
  if (!tm) return;

  const auto tab_ids = tab_manager_->GetAllTabIds();
  if (tab_ids.empty()) return;

  for (int tab_id : tab_ids) {
    CefRefPtr<CefBrowser> b = tab_manager_->GetBrowser(tab_id);
    if (!b) continue;
    int64_t task_id = tm->GetTaskIdForBrowserId(b->GetIdentifier());
    if (task_id < 0) continue;
    CefTaskInfo info;
    if (!tm->GetTaskInfo(task_id, info)) continue;

    int64_t ram = info.memory;
    SendEvent(BuildTabPropertyEvent(tab_id, "memoryBytes", ram));
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

bool OtfHandler::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefDownloadItem> download_item,
                                  const CefString& suggested_name,
                                  CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!download_item || !callback) {
    return false;
  }

  const std::string resolved_name = suggested_name.ToString().empty()
                                        ? download_item->GetSuggestedFileName().ToString()
                                        : suggested_name.ToString();
  const std::string target_path = BuildDownloadPath(resolved_name);
  const std::string download_url = download_item->GetOriginalUrl().ToString();
  const std::string origin = ExtractOrigin(download_url.empty()
                                               ? download_item->GetURL().ToString()
                                               : download_url);
  bool is_guest_download = false;
  if (tab_manager_) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view && !IsNonTabBrowserViewId(view->GetID())) {
      is_guest_download = IsGuestTab(view->GetID());
    }
  }

  // Consume transient allow-once entry set by permissions.download.allow.
  // StartDownload may bypass CanDownload where it's normally consumed, so
  // we must consume it here to prevent a stale entry from silently allowing
  // the next download from this origin.
  if (!origin.empty()) {
    allow_once_downloads_.erase(origin);
  }

  // Safety net: if the store says block (CanDownload might have been
  // bypassed), cancel the download before any data is saved.
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
  state.failure_reason = static_cast<int>(download_item->GetInterruptReason());
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
        CefPostTask(TID_UI, new DeferredImagePreviewPushTask(tab_id));
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
        // Reset any prior preview content for this tab so the overlay shows the
        // loading state immediately instead of the previously-viewed document
        // while a new (possibly large) doc downloads. Free the old in-memory
        // bytes too.
        const std::string prev_content = GetDocPreviewContentUrlForTab(tab_id);
        static const std::string kContentPrefix =
            "browser://doc-preview/content/";
        if (prev_content.rfind(kContentPrefix, 0) == 0) {
          otf::UnregisterDocContent(prev_content.substr(kContentPrefix.size()));
        }
        SetDocPreviewContentUrlForTab(tab_id, "");
        SetDocPreviewFileSizeForTab(tab_id, -1);
        SetDocPreviewFormatForTab(tab_id, "");
        tab_doc_preview_render_cache_.erase(tab_id);

        if (tab_manager_) {
          tab_manager_->SetSchemeUrl(tab_id, "");
          tab_manager_->SetDocPreviewMode(tab_id, DocPreviewMode::kInline);
        }
        this->SetDocPreviewUrlForTab(tab_id, doc_url);
        app->ShowDocPreviewOverlay();
        // Immediate content-less push → the overlay resets to its loading
        // indicator now, before the fetch completes (mirrors image preview).
        CefPostTask(TID_UI, new DeferredDocPreviewPushTask(tab_id));
        CefPostTask(TID_IO, new DeferredDocFetchTask(doc_url, tab_id));
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

  // Return false for all unhandled commands — CEF will execute built-in commands
  // like IDC_CONTENT_CONTEXT_COPYLINKLOCATION natively (clipboard copy).
  return false;
}

namespace {

std::string CookiePathForTracking(const CefCookie& cookie) {
  std::string path = CefString(&cookie.path).ToString();
  return path.empty() ? "/" : path;
}

int64_t CefBaseTimeToUnixSeconds(cef_basetime_t base_time) {
  cef_time_t cef_time{};
  time_t out = 0;
  if (cef_time_from_basetime(base_time, &cef_time) &&
      cef_time_to_timet(&cef_time, &out)) {
    return static_cast<int64_t>(out);
  }
  return 0;
}

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
      double next_zoom = b->GetHost()->GetZoomLevel();
      if (action == Shortcut::kZoomIn) {
        next_zoom = otf::ZoomIn(next_zoom);
      } else if (action == Shortcut::kZoomOut) {
        next_zoom = otf::ZoomOut(next_zoom);
      } else {
        next_zoom = otf::ZoomReset();
      }
      b->GetHost()->SetZoomLevel(next_zoom);
      const int pct = ToRoundedZoomPercent(next_zoom);
      tab_manager_->SetZoomPercent(cur, pct);
      SaveWorkspaceOriginZoom(tab_manager_, store_.get(), cur, pct);
      SendEvent(BuildTabPropertyEvent(
          cur, "zoomPercent", std::to_string(pct)));
      if (zoombar_subscription_) {
        zoombar_subscription_->Success(
            BuildZoomUpdateEvent(cur, pct));
      }
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

void OtfHandler::SetImagePreviewUrlForTab(int tab_id, const std::string& url) {
  otf::UnregisterImageContentForTab(tab_id);
  tab_image_preview_urls_[tab_id] = url;
  tab_image_preview_local_files_.erase(tab_id);
  tab_image_preview_file_sizes_.erase(tab_id);
  tab_image_preview_formats_.erase(tab_id);
  tab_image_preview_render_cache_.erase(tab_id);
  tab_image_preview_download_cache_.erase(tab_id);
  tab_image_preview_decode_nonces_.erase(tab_id);
  if (tab_manager_) {
    tab_manager_->SetImagePreviewDimensions(tab_id, 0, 0);
    tab_manager_->SetImagePreviewInfoVisible(tab_id, true);
  }
  // Setting (or clearing) the URL resets navigation state — a different
  // image was loaded, page index from the previous TIFF is meaningless.
  tab_image_preview_pages_[tab_id] = 0;
  tab_image_preview_page_counts_[tab_id] = 1;
  if (!url.empty()) {
    CefPostTask(TID_UI, new DeferredImagePreviewPushTask(tab_id));
  }
}

void OtfHandler::ClearInlineImagePreviewForTab(int tab_id) {
  if (tab_id < 0) {
    return;
  }
  if (tab_manager_) {
    tab_manager_->SetImagePreviewMode(tab_id, ImagePreviewMode::kNone);
    if (tab_manager_->GetUrl(tab_id) != "browser://imagepreview") {
      tab_manager_->SetSchemeUrl(tab_id, "");
    }
  }
  SetImagePreviewUrlForTab(tab_id, "");
}

void OtfHandler::SetImagePreviewLocalFileForTab(
    int tab_id,
    const std::string& public_url,
    const std::string& file_path) {
  otf::UnregisterImageContentForTab(tab_id);
  tab_image_preview_urls_[tab_id] = public_url;
  tab_image_preview_local_files_[tab_id] = file_path;
  tab_image_preview_file_sizes_.erase(tab_id);
  tab_image_preview_formats_.erase(tab_id);
  tab_image_preview_render_cache_.erase(tab_id);
  tab_image_preview_download_cache_.erase(tab_id);
  tab_image_preview_decode_nonces_.erase(tab_id);
  if (tab_manager_) {
    tab_manager_->SetImagePreviewDimensions(tab_id, 0, 0);
    tab_manager_->SetImagePreviewInfoVisible(tab_id, true);
  }
  tab_image_preview_pages_[tab_id] = 0;
  tab_image_preview_page_counts_[tab_id] = 1;
  if (!public_url.empty() && !file_path.empty()) {
    CefPostTask(TID_UI, new DeferredImagePreviewPushTask(tab_id));
  }
}

void OtfHandler::SetImagePreviewFileSizeForTab(int tab_id, int64_t file_size_bytes) {
  if (file_size_bytes < 0) {
    tab_image_preview_file_sizes_.erase(tab_id);
  } else {
    tab_image_preview_file_sizes_[tab_id] = file_size_bytes;
  }
}

int64_t OtfHandler::GetImagePreviewFileSizeForTab(int tab_id) const {
  auto it = tab_image_preview_file_sizes_.find(tab_id);
  return it != tab_image_preview_file_sizes_.end() ? it->second : -1;
}

void OtfHandler::SetImagePreviewFormatForTab(int tab_id, const std::string& format) {
  if (format.empty()) {
    tab_image_preview_formats_.erase(tab_id);
  } else {
    tab_image_preview_formats_[tab_id] = format;
  }
}

std::string OtfHandler::GetImagePreviewFormatForTab(int tab_id) const {
  auto it = tab_image_preview_formats_.find(tab_id);
  return it != tab_image_preview_formats_.end() ? it->second : "";
}

void OtfHandler::SetDocPreviewUrlForTab(int tab_id, const std::string& url) {
  tab_doc_preview_urls_[tab_id] = url;
  tab_doc_preview_local_files_.erase(tab_id);
  tab_doc_preview_file_sizes_.erase(tab_id);
  tab_doc_preview_formats_.erase(tab_id);
  tab_doc_preview_render_cache_.erase(tab_id);
}

void OtfHandler::SetDocPreviewLocalFileForTab(
    int tab_id,
    const std::string& public_url,
    const std::string& file_path) {
  tab_doc_preview_urls_[tab_id] = public_url;
  tab_doc_preview_local_files_[tab_id] = file_path;
  tab_doc_preview_file_sizes_.erase(tab_id);
  tab_doc_preview_formats_.erase(tab_id);
  tab_doc_preview_render_cache_.erase(tab_id);
}

std::string OtfHandler::GetDocPreviewUrlForTab(int tab_id) const {
  auto it = tab_doc_preview_urls_.find(tab_id);
  return it != tab_doc_preview_urls_.end() ? it->second : "";
}

std::string OtfHandler::GetDocPreviewLocalFileForTab(int tab_id) const {
  auto it = tab_doc_preview_local_files_.find(tab_id);
  return it != tab_doc_preview_local_files_.end() ? it->second : "";
}

void OtfHandler::SetDocPreviewContentUrlForTab(int tab_id, const std::string& content_url) {
  tab_doc_preview_content_urls_[tab_id] = content_url;
}

std::string OtfHandler::GetDocPreviewContentUrlForTab(int tab_id) const {
  auto it = tab_doc_preview_content_urls_.find(tab_id);
  return it != tab_doc_preview_content_urls_.end() ? it->second : "";
}

void OtfHandler::SetDocPreviewFileSizeForTab(int tab_id, int64_t file_size_bytes) {
  if (file_size_bytes < 0) {
    tab_doc_preview_file_sizes_.erase(tab_id);
  } else {
    tab_doc_preview_file_sizes_[tab_id] = file_size_bytes;
  }
}

int64_t OtfHandler::GetDocPreviewFileSizeForTab(int tab_id) const {
  auto it = tab_doc_preview_file_sizes_.find(tab_id);
  return it != tab_doc_preview_file_sizes_.end() ? it->second : -1;
}

void OtfHandler::SetDocPreviewFormatForTab(int tab_id, const std::string& format) {
  if (format.empty()) {
    tab_doc_preview_formats_.erase(tab_id);
  } else {
    tab_doc_preview_formats_[tab_id] = format;
  }
}

std::string OtfHandler::GetDocPreviewFormatForTab(int tab_id) const {
  auto it = tab_doc_preview_formats_.find(tab_id);
  return it != tab_doc_preview_formats_.end() ? it->second : "";
}

void OtfHandler::ClearDocPreviewStateForTab(int tab_id) {
  if (tab_id < 0) {
    return;
  }
  if (tab_manager_) {
    tab_manager_->SetDocPreviewMode(tab_id, DocPreviewMode::kNone);
    if (tab_manager_->GetUrl(tab_id) != "browser://docpreview") {
      tab_manager_->SetSchemeUrl(tab_id, "");
    }
  }
  tab_doc_preview_subscriptions_.erase(tab_id);
  // Free any in-memory fetched content registered for this tab.
  const std::string content_url = GetDocPreviewContentUrlForTab(tab_id);
  static const std::string kContentPrefix = "browser://doc-preview/content/";
  if (content_url.rfind(kContentPrefix, 0) == 0) {
    otf::UnregisterDocContent(content_url.substr(kContentPrefix.size()));
  }
  SetDocPreviewUrlForTab(tab_id, "");
  SetDocPreviewContentUrlForTab(tab_id, "");
  tab_doc_preview_render_cache_.erase(tab_id);
}

std::string OtfHandler::BuildDocPreviewLoadEvent(int tab_id) {
  std::string url = GetDocPreviewUrlForTab(tab_id);
  if (url.empty()) {
    return "";
  }

  const std::string local_path = GetDocPreviewLocalFileForTab(tab_id);
  const std::string content_url = GetDocPreviewContentUrlForTab(tab_id);
  std::string display_url;
  std::string mime_type;
  int64_t file_size_bytes = GetDocPreviewFileSizeForTab(tab_id);
  std::string format = GetDocPreviewFormatForTab(tab_id);

  if (!local_path.empty()) {
    mime_type = otf::GuessDocumentMimeType(local_path);
    auto cache_it = tab_doc_preview_render_cache_.find(tab_id);
    if (cache_it != tab_doc_preview_render_cache_.end() &&
        cache_it->second.file_path == local_path &&
        !cache_it->second.display_url.empty()) {
      display_url = cache_it->second.display_url;
    } else {
      if (mime_type == "application/pdf") {
        auto file_bytes = otf::ReadFileBinary(local_path);
        if (file_bytes) {
          std::string raw_bytes(file_bytes->begin(), file_bytes->end());
          display_url = GetDataURI(raw_bytes, "application/pdf");
          file_size_bytes = static_cast<int64_t>(raw_bytes.size());
          tab_doc_preview_render_cache_[tab_id] =
              DocPreviewRenderCache{local_path, display_url};
        }
      } else {
        auto file_bytes = otf::ReadFileBinary(local_path);
        if (file_bytes) {
          std::string content(file_bytes->begin(), file_bytes->end());
          display_url = "data:text/plain;base64," +
                        CefBase64Encode(content.data(), content.size()).ToString();
          file_size_bytes = static_cast<int64_t>(content.size());
          tab_doc_preview_render_cache_[tab_id] =
              DocPreviewRenderCache{local_path, display_url};
        }
      }
      if (format.empty()) {
        format = GuessPreviewFormat(local_path);
        if (format.empty()) {
          std::string ext = mime_type;
          SetDocPreviewFormatForTab(tab_id, ext.empty() ? "TXT" : ext);
        } else {
          SetDocPreviewFormatForTab(tab_id, format);
        }
      }
      if (file_size_bytes >= 0) {
        SetDocPreviewFileSizeForTab(tab_id, file_size_bytes);
      }
    }
  } else if (!content_url.empty()) {
    // Fetched doc served from memory (no on-disk file). Source the display URL
    // from the in-memory bytes. PDFs render by navigating the tab to
    // content_url, so they need no data: URI — only text gets one for Monaco.
    static const std::string kContentPrefix = "browser://doc-preview/content/";
    const std::string token =
        content_url.rfind(kContentPrefix, 0) == 0
            ? content_url.substr(kContentPrefix.size())
            : std::string();
    std::vector<uint8_t> bytes;
    std::string mem_mime;
    if (!token.empty() && otf::GetDocContentBytes(token, &bytes, &mem_mime)) {
      mime_type = mem_mime.empty() ? otf::GuessDocumentMimeType(url) : mem_mime;
      file_size_bytes = static_cast<int64_t>(bytes.size());
      auto cache_it = tab_doc_preview_render_cache_.find(tab_id);
      if (cache_it != tab_doc_preview_render_cache_.end() &&
          cache_it->second.file_path == content_url &&
          !cache_it->second.display_url.empty()) {
        display_url = cache_it->second.display_url;
      } else if (mime_type != "application/pdf") {
        std::string content(bytes.begin(), bytes.end());
        display_url = "data:text/plain;base64," +
                      CefBase64Encode(content.data(), content.size()).ToString();
        tab_doc_preview_render_cache_[tab_id] =
            DocPreviewRenderCache{content_url, display_url};
      }
      if (format.empty()) {
        format = GuessPreviewFormat(url);
        if (format.empty()) format = mime_type;
        SetDocPreviewFormatForTab(tab_id, format.empty() ? "TXT" : format);
      }
      if (file_size_bytes >= 0) {
        SetDocPreviewFileSizeForTab(tab_id, file_size_bytes);
      }
    }
  }

  if (format.empty()) {
    format = GuessPreviewFormat(url);
  }

  std::string result = JsonObjectBuilder()
      .AddString("key", "load-doc")
      .AddString("url", url)
      .AddString("displayUrl", display_url)
      .AddString("contentUrl", content_url)
      .AddString("mimeType", mime_type.empty() ? "text/plain" : mime_type)
      .AddInt("tabId", tab_id)
      .AddInt("fileSizeBytes", file_size_bytes >= 0
                                    ? static_cast<int>(std::min<int64_t>(file_size_bytes, std::numeric_limits<int>::max()))
                                    : -1)
      .AddString("format", format)
      .Build();
  return result;
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
      tab_image_preview_subscriptions_.erase(tab_id);
      tab_image_preview_urls_.erase(tab_id);
      tab_image_preview_local_files_.erase(tab_id);
      tab_image_preview_file_sizes_.erase(tab_id);
      tab_image_preview_formats_.erase(tab_id);
      tab_image_preview_render_cache_.erase(tab_id);
      tab_image_preview_download_cache_.erase(tab_id);
      tab_image_preview_pages_.erase(tab_id);
      tab_image_preview_page_counts_.erase(tab_id);
      tab_image_preview_decode_nonces_.erase(tab_id);
      otf::UnregisterImageContentForTab(tab_id);
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

std::string OtfHandler::GetImagePreviewUrlForTab(int tab_id) const {
  auto it = tab_image_preview_urls_.find(tab_id);
  if (it != tab_image_preview_urls_.end()) {
    return it->second;
  }
  return "";
}

std::string OtfHandler::GetImagePreviewLocalFileForTab(int tab_id) const {
  auto it = tab_image_preview_local_files_.find(tab_id);
  if (it != tab_image_preview_local_files_.end()) {
    return it->second;
  }
  return "";
}

uint64_t OtfHandler::BumpImagePreviewDecodeNonceForTab(int tab_id) {
  uint64_t& nonce = tab_image_preview_decode_nonces_[tab_id];
  if (nonce == 0) {
    nonce = 1;
  } else {
    ++nonce;
  }
  return nonce;
}

uint64_t OtfHandler::GetImagePreviewDecodeNonceForTab(int tab_id) const {
  auto it = tab_image_preview_decode_nonces_.find(tab_id);
  return it != tab_image_preview_decode_nonces_.end() ? it->second : 0;
}

void OtfHandler::NotifyImagePreviewDownloadProgress(int tab_id,
                                                    uint64_t decode_nonce,
                                                    int received_bytes,
                                                    int total_bytes) {
  if (tab_id == -1 || GetImagePreviewDecodeNonceForTab(tab_id) != decode_nonce) {
    return;
  }
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> sub;
  auto it = tab_image_preview_subscriptions_.find(tab_id);
  if (it != tab_image_preview_subscriptions_.end()) {
    sub = it->second;
  } else {
    sub = image_preview_subscription_;
  }
  if (!sub) {
    return;
  }

  int percent = -1;
  if (total_bytes > 0) {
    percent = static_cast<int>(std::clamp((received_bytes * 100) / total_bytes, 0, 100));
  }
  sub->Success(JsonObjectBuilder()
                   .AddString("key", "image-preview-download-progress")
                   .AddString("decodeNonce", std::to_string(decode_nonce))
                   .AddInt("receivedBytes", received_bytes)
                   .AddInt("totalBytes", total_bytes)
                   .AddInt("percent", percent)
                   .Build());
}

void OtfHandler::SetImagePreviewPageForTab(int tab_id, int page) {
  tab_image_preview_pages_[tab_id] = page < 0 ? 0 : page;
}

int OtfHandler::GetImagePreviewPageForTab(int tab_id) const {
  auto it = tab_image_preview_pages_.find(tab_id);
  return it != tab_image_preview_pages_.end() ? it->second : 0;
}

void OtfHandler::SetImagePreviewPageCountForTab(int tab_id, int count) {
  tab_image_preview_page_counts_[tab_id] = count < 1 ? 1 : count;
}

int OtfHandler::GetImagePreviewPageCountForTab(int tab_id) const {
  auto it = tab_image_preview_page_counts_.find(tab_id);
  return it != tab_image_preview_page_counts_.end() ? it->second : 1;
}

std::string OtfHandler::BuildImagePreviewLoadEvent(int tab_id, bool bump_decode_nonce) {
  std::string url = GetImagePreviewUrlForTab(tab_id);
  if (url.empty()) return "";

  int page = GetImagePreviewPageForTab(tab_id);
  const uint64_t decode_nonce = bump_decode_nonce ? BumpImagePreviewDecodeNonceForTab(tab_id)
                                                   : GetImagePreviewDecodeNonceForTab(tab_id);
  otf::ImagePreviewPayload payload;
  payload.display_url.clear();
  payload.page_count = 1;
  payload.natural_width = tab_manager_ ? tab_manager_->GetImagePreviewWidth(tab_id) : 0;
  payload.natural_height = tab_manager_ ? tab_manager_->GetImagePreviewHeight(tab_id) : 0;
  payload.show_info = tab_manager_ ? tab_manager_->IsImagePreviewInfoVisible(tab_id) : true;
  const std::string local_path = GetImagePreviewLocalFileForTab(tab_id);
  const bool is_remote_source = url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
  if (!local_path.empty()) {
    auto cache_it = tab_image_preview_render_cache_.find(tab_id);
    if (cache_it != tab_image_preview_render_cache_.end() &&
        cache_it->second.file_path == local_path &&
        cache_it->second.page == page &&
        !cache_it->second.display_url.empty()) {
      payload.display_url = cache_it->second.display_url;
      payload.page_count = cache_it->second.page_count;
    } else {
      if (otf::IsTiffUrl(local_path)) {
        std::string png_base64;
        int decoded_page_count = 1;
        std::string error_reason;
        if (DecodeLocalTiffPreview(local_path, page, &png_base64,
                                   &decoded_page_count, &error_reason)) {
          const std::string token = BuildImageContentToken(
              tab_id, "local-tiff", page, url + ".png");
          std::vector<uint8_t> png_bytes = DecodeDataUrlBytes(png_base64);
          if (!png_bytes.empty()) {
            otf::RegisterImageContentBytes(token, std::move(png_bytes), "image/png");
            payload.display_url = "browser://image-preview/content/" + token;
          } else {
            payload.display_url.clear();
          }
          payload.page_count = decoded_page_count;
          tab_image_preview_render_cache_[tab_id] =
              ImagePreviewRenderCache{local_path, payload.display_url, page, decoded_page_count};
          SetImagePreviewPageForTab(tab_id, page);
          SetImagePreviewPageCountForTab(tab_id, decoded_page_count);
        } else {
          payload.display_url.clear();
          payload.page_count = 1;
          return JsonObjectBuilder()
              .AddString("key", "load-image")
              .AddString("url", url)
              .AddString("displayUrl", "")
              .AddInt("pageCount", 1)
              .AddInt("currentPage", page)
              .AddInt("tabId", tab_id)
              .AddString("decodeNonce", std::to_string(decode_nonce))
              .AddInt("naturalWidth", payload.natural_width)
              .AddInt("naturalHeight", payload.natural_height)
              .AddBool("showInfo", payload.show_info)
              .AddString("error", error_reason.empty() ? "Failed to decode downloaded TIFF file" : error_reason)
              .Build();
        }
      } else if (otf::IsSupportedImageUrl(local_path)) {
        if (!std::filesystem::exists(local_path)) {
          return JsonObjectBuilder()
              .AddString("key", "load-image")
              .AddString("url", url)
              .AddString("displayUrl", "")
              .AddInt("pageCount", 1)
              .AddInt("currentPage", page)
              .AddInt("tabId", tab_id)
              .AddString("decodeNonce", std::to_string(decode_nonce))
              .AddInt("naturalWidth", payload.natural_width)
              .AddInt("naturalHeight", payload.natural_height)
              .AddBool("showInfo", payload.show_info)
              .AddString("error", "Failed to open downloaded image")
              .Build();
        }
        std::string mime_type = GuessImageMimeType(local_path);
        const std::string token =
            BuildImageContentToken(tab_id, "local", 0, local_path);
        otf::RegisterImageContent(token, local_path, mime_type);
        payload.display_url = "browser://image-preview/content/" + token;
        payload.page_count = 1;
        tab_image_preview_render_cache_[tab_id] =
            ImagePreviewRenderCache{local_path, payload.display_url, 0, 1};
        SetImagePreviewPageForTab(tab_id, 0);
        SetImagePreviewPageCountForTab(tab_id, 1);
        SetImagePreviewFormatForTab(tab_id, GuessPreviewFormat(local_path));
        std::string size_error;
        const int64_t file_size = GetFileSizeBytes(local_path, &size_error);
        if (file_size >= 0) {
          SetImagePreviewFileSizeForTab(tab_id, file_size);
        }
      } else {
        payload.display_url.clear();
        payload.page_count = 1;
        return JsonObjectBuilder()
            .AddString("key", "load-image")
            .AddString("url", url)
            .AddString("displayUrl", "")
            .AddInt("pageCount", 1)
            .AddInt("currentPage", page)
            .AddInt("tabId", tab_id)
            .AddString("decodeNonce", std::to_string(decode_nonce))
            .AddInt("naturalWidth", payload.natural_width)
            .AddInt("naturalHeight", payload.natural_height)
            .AddBool("showInfo", payload.show_info)
            .AddString("error", "Unsupported image format")
            .Build();
      }
    }
  } else if (is_remote_source) {
    auto cache_it = tab_image_preview_download_cache_.find(tab_id);
    if (cache_it != tab_image_preview_download_cache_.end() &&
        cache_it->second.source_url == url) {
      auto& cache = cache_it->second;
      payload.page_count = cache.page_count < 1 ? 1 : cache.page_count;
      if (cache.is_tiff) {
        if (cache.page == page && !cache.display_url.empty()) {
          payload.display_url = cache.display_url;
        } else if (!cache.raw_bytes.empty()) {
          std::string png_base64;
          int decoded_page_count = 1;
          if (otf::DecodeTiffBufferToPngBase64(cache.raw_bytes.data(),
                                               cache.raw_bytes.size(), page,
                                               png_base64, decoded_page_count)) {
            const std::string token = BuildImageContentToken(
                tab_id, "remote-tiff", page, url + ".png");
            std::vector<uint8_t> png_bytes = DecodeDataUrlBytes(png_base64);
            if (!png_bytes.empty()) {
              otf::RegisterImageContentBytes(token, std::move(png_bytes), "image/png");
              cache.display_url = "browser://image-preview/content/" + token;
            } else {
              cache.display_url.clear();
            }
            cache.page = page;
            cache.page_count = decoded_page_count;
            payload.display_url = cache.display_url;
            payload.page_count = decoded_page_count;
            tab_image_preview_render_cache_[tab_id] =
                ImagePreviewRenderCache{url, cache.display_url, page, decoded_page_count};
            SetImagePreviewPageForTab(tab_id, page);
            SetImagePreviewPageCountForTab(tab_id, decoded_page_count);
          } else {
            payload.display_url.clear();
          }
        } else {
          payload.display_url.clear();
        }
      } else {
        payload.display_url = cache.display_url;
      }
      if (cache.file_size_bytes >= 0) {
        SetImagePreviewFileSizeForTab(tab_id, cache.file_size_bytes);
      }
      if (!cache.mime_type.empty() && !cache.is_tiff) {
        SetImagePreviewFormatForTab(tab_id, GuessPreviewFormat(url));
      }
    } else {
      payload.display_url.clear();
      payload.page_count = 1;
    }
  } else if (is_remote_source) {
    payload.display_url = url;
  }

  // Refresh stored page_count from the decoder if the new payload page count is larger
  // (may have changed if the file was swapped on disk, or wasn't known yet on first load).
  // This avoids clobbering asynchronously-fetched remote TIFF page counts back to 1.
  int page_count = GetImagePreviewPageCountForTab(tab_id);
  if (payload.page_count > page_count) {
    page_count = payload.page_count;
    SetImagePreviewPageCountForTab(tab_id, page_count);
  }

  int64_t file_size_bytes = GetImagePreviewFileSizeForTab(tab_id);
  if (file_size_bytes < 0 && !local_path.empty()) {
    std::string size_error;
    file_size_bytes = GetFileSizeBytes(local_path, &size_error);
    if (file_size_bytes >= 0) {
      SetImagePreviewFileSizeForTab(tab_id, file_size_bytes);
      SetImagePreviewFormatForTab(
          tab_id, otf::IsTiffUrl(local_path) ? "TIFF" : GuessPreviewFormat(local_path));
    }
  }
  if (file_size_bytes < 0) {
    auto cache_it = tab_image_preview_download_cache_.find(tab_id);
    if (cache_it != tab_image_preview_download_cache_.end() &&
        cache_it->second.source_url == url) {
      file_size_bytes = cache_it->second.file_size_bytes;
    }
  }
  std::string format = GetImagePreviewFormatForTab(tab_id);
  if (format.empty()) {
    format = GuessPreviewFormat(url);
  }

  return JsonObjectBuilder()
      .AddString("key", "load-image")
      .AddString("url", url)
      .AddString("displayUrl", payload.display_url)
      .AddInt("pageCount", page_count)
      .AddInt("currentPage", page)
      .AddInt("tabId", tab_id)
      .AddString("decodeNonce", std::to_string(decode_nonce))
      .AddInt("fileSizeBytes", file_size_bytes >= 0
                                    ? static_cast<int>(std::min<int64_t>(file_size_bytes, std::numeric_limits<int>::max()))
                                    : -1)
      .AddString("format", format)
      .AddInt("naturalWidth", payload.natural_width)
      .AddInt("naturalHeight", payload.natural_height)
      .AddBool("showInfo", payload.show_info)
      .Build();
}

} // namespace otf
