#include "otf_page_runtime.h"

#include <chrono>
#include <string>
#include <vector>

#include "include/base/cef_logging.h"
#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_bookmark_runtime.h"
#include "otf_browser_shell.h"
#include "otf_handler.h"
#include "otf_utils.h"
#include "otf_zoom_runtime.h"

namespace otf {
namespace {

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
  return tab_manager ? tab_manager->GetId(browser) : -1;
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

std::string GetDevUiUrl() {
  return CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");
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

void ClearDedicatedPreviewModeIfNeeded(OtfHandler* handler,
                                       int tab_id,
                                       const std::string& url) {
  if (!handler->tab_manager_) {
    return;
  }

  const bool is_image_preview_url =
      url == "browser://imagepreview" ||
      url.rfind("browser://image-preview/", 0) == 0 ||
      url.find("/imagepreview.html") != std::string::npos;
  const ImagePreviewMode image_mode =
      handler->tab_manager_->GetImagePreviewMode(tab_id);
  if (image_mode == ImagePreviewMode::kDedicated && !is_image_preview_url) {
    handler->tab_manager_->SetSchemeUrl(tab_id, "");
    handler->tab_manager_->SetImagePreviewMode(tab_id, ImagePreviewMode::kNone);
    handler->SetImagePreviewUrlForTab(tab_id, "");
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->HideImagePreviewOverlay();
    }
  }
  if (image_mode != ImagePreviewMode::kDedicated || !is_image_preview_url) {
    handler->tab_manager_->SetUrl(tab_id, url);
  }

  const bool is_doc_preview_url =
      url == "browser://docpreview" ||
      url.rfind("browser://doc-preview/", 0) == 0 ||
      url.find("/docpreview.html") != std::string::npos;
  const DocPreviewMode doc_mode = handler->tab_manager_->GetDocPreviewMode(tab_id);
  if (doc_mode == DocPreviewMode::kDedicated && !is_doc_preview_url) {
    handler->tab_manager_->SetSchemeUrl(tab_id, "");
    handler->tab_manager_->SetDocPreviewMode(tab_id, DocPreviewMode::kNone);
    handler->SetDocPreviewUrlForTab(tab_id, "");
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->HideDocPreviewOverlay();
    }
  }
  if (doc_mode != DocPreviewMode::kDedicated || !is_doc_preview_url) {
    handler->tab_manager_->SetUrl(tab_id, url);
  }
}

void MaybeClearSuppressedHistoryUrl(TabManager* tab_manager,
                                    int tab_id,
                                    const std::string& url) {
  if (!tab_manager) {
    return;
  }
  const std::string suppressed_url = tab_manager->GetHistorySuppressedUrl(tab_id);
  if (!suppressed_url.empty() && url != suppressed_url) {
    tab_manager->SetHistorySuppressedUrl(tab_id, "");
  }
}

void MaybeClearSslErrorState(OtfHandler* handler,
                             int tab_id,
                             const std::string& url) {
  if (!handler->tab_manager_) {
    return;
  }
  if (handler->tab_manager_->HasSslError(tab_id) &&
      !IsSecurityErrorDocumentUrl(url) &&
      !IsSameSecurityUrl(url, handler->tab_manager_->GetSslErrorUrl(tab_id))) {
    handler->tab_manager_->SetSslError(tab_id, false);
    handler->SendEvent(JsonObjectBuilder()
                           .AddInt("id", tab_id)
                           .AddString("key", "sslError")
                           .AddBool("value", false)
                           .Build());
  }
}

bool IsDevUiNavigation(const std::string& url) {
  const std::string dev_ui_url = GetDevUiUrl();
  if (dev_ui_url.empty()) {
    return false;
  }
  return url.rfind(dev_ui_url + "/", 0) == 0;
}

void MaybeResetFaviconForOriginChange(OtfHandler* handler,
                                      int tab_id,
                                      const std::string& url) {
  if (!handler->tab_manager_) {
    return;
  }
  const std::string old_url = handler->tab_manager_->GetUrl(tab_id);
  const std::string old_origin = ExtractOrigin(old_url);
  const std::string new_origin = ExtractOrigin(url);
  if (old_origin != new_origin) {
    handler->tab_manager_->SetFaviconUrl(tab_id, "");
    handler->SendEvent(BuildTabPropertyEvent(tab_id, "favicon", ""));
  }
}

void MaybeSendBookmarkSync(OtfHandler* handler, int tab_id, const std::string& url) {
  if (!handler->store_ || !IsPersistableWebUrl(url)) {
    return;
  }
  handler->SendEvent(BuildBookmarkSyncEvent(
      tab_id, url,
      !handler->IsGuestTab(tab_id) &&
          handler->store_->IsBookmarked(NormalizeBookmarkUrl(url))));
}

std::string EscapeJsString(std::string value) {
  for (size_t i = 0; (i = value.find('\\', i)) != std::string::npos; i += 2) {
    value.replace(i, 1, "\\\\");
  }
  for (size_t i = 0; (i = value.find('\'', i)) != std::string::npos; i += 2) {
    value.replace(i, 1, "\\'");
  }
  return value;
}

std::string EscapeJsonString(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 4);
  for (unsigned char c : value) {
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

void UpdateSslErrorStateForLoadedPage(OtfHandler* handler,
                                      CefRefPtr<CefBrowser> browser,
                                      int tab_id,
                                      const std::string& url) {
  CefRefPtr<CefNavigationEntry> entry =
      browser->GetHost() ? browser->GetHost()->GetVisibleNavigationEntry()
                         : nullptr;
  CefRefPtr<CefSSLStatus> ssl_status = entry ? entry->GetSSLStatus() : nullptr;
  const bool has_cert_error =
      ssl_status && CefIsCertStatusError(ssl_status->GetCertStatus());
  if (has_cert_error) {
    handler->tab_manager_->SetSslError(tab_id, true);
    handler->tab_manager_->SetSslErrorUrl(tab_id, url);
    handler->SendEvent(JsonObjectBuilder()
                           .AddInt("id", tab_id)
                           .AddString("key", "sslError")
                           .AddBool("value", true)
                           .Build());
  } else if (handler->tab_manager_->HasSslError(tab_id) &&
             url != handler->tab_manager_->GetSslErrorUrl(tab_id) &&
             !IsSecurityErrorDocumentUrl(url)) {
    handler->tab_manager_->SetSslError(tab_id, false);
    handler->SendEvent(JsonObjectBuilder()
                           .AddInt("id", tab_id)
                           .AddString("key", "sslError")
                           .AddBool("value", false)
                           .Build());
  }
}

void MaybeRecordHistoryVisit(OtfHandler* handler,
                             int tab_id,
                             const std::string& url) {
  const std::string current = handler->tab_manager_->GetUrl(tab_id);
  const std::string suppressed_url =
      handler->tab_manager_->GetHistorySuppressedUrl(tab_id);
  const int workspace_id = handler->tab_manager_->GetWorkspaceId(tab_id);
  if (otf::IsHistoryEnabled() && !handler->tab_manager_->IsPrivate(tab_id) &&
      !handler->IsGuestTab(tab_id) && IsPersistableWebUrl(url) &&
      !IsInternalUiUrl(url) &&
      (current.empty() || current.rfind("browser://", 0) != 0) &&
      (suppressed_url.empty() || suppressed_url != url)) {
    handler->store_->RecordVisit(url, handler->tab_manager_->GetTitle(tab_id),
                                 "link", workspace_id);
  }
}

void SyncZoomStateForLoadedPage(OtfHandler* handler,
                                CefRefPtr<CefBrowser> browser,
                                int tab_id,
                                const std::string& url) {
  int zoom_percent = 100;
  if (ApplyPrivateTabZoom(browser, handler->tab_manager_, tab_id, &zoom_percent) ||
      ApplyWorkspaceOriginZoom(browser, handler->tab_manager_, tab_id,
                               &zoom_percent)) {
    handler->SendEvent(
        BuildTabPropertyEvent(tab_id, "zoomPercent", std::to_string(zoom_percent)));
    if (handler->zoombar_subscription_) {
      handler->zoombar_subscription_->Success(
          BuildZoomUpdateEvent(tab_id, zoom_percent));
    }
    return;
  }

  if (!handler->tab_manager_->IsPrivate(tab_id) && !IsPersistableZoomUrl(url)) {
    browser->GetHost()->SetZoomLevel(otf::ZoomReset());
    handler->tab_manager_->SetZoomPercent(tab_id, 100);
    handler->SendEvent(BuildTabPropertyEvent(tab_id, "zoomPercent", "100"));
    if (handler->zoombar_subscription_) {
      handler->zoombar_subscription_->Success(BuildZoomUpdateEvent(tab_id, 100));
    }
  }
}

}  // namespace

void OtfHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                               const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (!view || IsNonTabBrowserViewId(view->GetID())) {
    return;
  }

  const int tab_id = view->GetID();
  if (tab_manager_) {
    tab_manager_->SetTitle(tab_id, title.ToString());
  }
  const std::string url = tab_manager_ ? tab_manager_->GetUrl(tab_id) : "";
  if (store_ && otf::IsHistoryEnabled() && IsPersistableWebUrl(url) &&
      !IsInternalUiUrl(url) && !IsGuestTab(tab_id)) {
    const int workspace_id =
        tab_manager_ ? tab_manager_->GetWorkspaceId(tab_id) : active_workspace_id_;
    store_->UpdateHistoryTitle(url, title.ToString(), workspace_id);
  }
  SendEvent(BuildTabPropertyEvent(tab_id, "title", title.ToString()));
  PersistWorkspaceForTab(tab_id);
  if (OtfApp* app = OtfApp::GetInstance()) {
    app->UpdateWindowTitle(tab_id);
  }
}

void OtfHandler::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain()) {
    return;
  }

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (!view || IsNonTabBrowserViewId(view->GetID())) {
    return;
  }

  const int tab_id = view->GetID();
  const std::string url_str = url.ToString();
  if (url_str.rfind("browser://insecure-blocked", 0) == 0 ||
      otf::IsLocalFilesystemPathLike(url_str)) {
    return;
  }

  ClearDedicatedPreviewModeIfNeeded(this, tab_id, url_str);
  if (url_str.rfind("browser://", 0) == 0) {
    return;
  }

  MaybeClearSuppressedHistoryUrl(tab_manager_, tab_id, url_str);
  MaybeClearSslErrorState(this, tab_id, url_str);
  if (IsDevUiNavigation(url_str)) {
    return;
  }

  SendEvent(BuildTabPropertyEvent(tab_id, "url", url_str));
  MaybeResetFaviconForOriginChange(this, tab_id, url_str);
  MaybeSendBookmarkSync(this, tab_id, url_str);
  PersistWorkspaceForTab(tab_id);
}

void OtfHandler::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                    const std::vector<CefString>& icon_urls) {
  CEF_REQUIRE_UI_THREAD();

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (!view || IsNonTabBrowserViewId(view->GetID())) {
    return;
  }

  const int tab_id = view->GetID();
  std::string favicon_url;
  if (!icon_urls.empty()) {
    favicon_url = icon_urls[0].ToString();
  }
  if (favicon_url.empty()) {
    if (tab_manager_) {
      tab_manager_->SetFaviconUrl(tab_id, "");
    }
    SendEvent(BuildTabPropertyEvent(tab_id, "favicon", ""));
    PersistWorkspaceForTab(tab_id);
    return;
  }

  const std::string page_url =
      tab_manager_ ? NormalizeBookmarkUrl(tab_manager_->GetUrl(tab_id)) : "";
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
  if (!link_preview_browser_) {
    return;
  }

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (!view || IsNonTabBrowserViewId(view->GetID())) {
    return;
  }

  const std::string url = value.ToString();
  OtfApp* app = OtfApp::GetInstance();
  if (url.empty()) {
    if (app) {
      app->SetLinkPreviewVisible(false);
    }
    return;
  }

  if (app) {
    app->SetLinkPreviewVisible(true);
  }
  const std::string js =
      "window.__otfSetLinkPreview('" + EscapeJsString(url) + "');";
  link_preview_browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
}

bool OtfHandler::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                  cef_log_severity_t level,
                                  const CefString& message,
                                  const CefString& source,
                                  int line) {
  CEF_REQUIRE_UI_THREAD();

  LOG(INFO) << "[otf][console:" << level << "] " << message.ToString()
            << " (" << source.ToString() << ":" << line << ")";
  otf::DiagLog("console[" + std::to_string(level) + "]: " + message.ToString() +
               " (" + source.ToString() + ":" + std::to_string(line) + ")");

  if (!tab_manager_) {
    return false;
  }

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && IsNonTabBrowserViewId(view->GetID())) {
    return false;
  }

  const int tab_id = ResolveRealTabIdForBrowser(browser, tab_manager_);
  if (tab_id < 0) {
    return false;
  }

  const std::string msg = message.ToString();
  if (msg.find("ResizeObserver loop") != std::string::npos) {
    return false;
  }

  const int64_t now_ms = static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  ConsoleEntry entry{
      static_cast<int>(level),
      msg,
      source.ToString(),
      line,
      now_ms,
  };
  tab_manager_->AddConsoleEntry(tab_id, entry);

  if (console_subscription_) {
    const std::string event =
        "{\"key\":\"console-entry\",\"tabId\":" + std::to_string(tab_id) +
        ",\"level\":" + std::to_string(static_cast<int>(level)) +
        ",\"message\":\"" + EscapeJsonString(entry.message) + "\"" +
        ",\"source\":\"" + EscapeJsonString(entry.source) + "\"" +
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
  if (!frame->IsMain()) {
    return;
  }

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && view->GetID() == kUiBrowserViewId) {
    LOG(INFO) << "[otf] UI shell OnLoadEnd: httpStatus=" << httpStatusCode
              << " url=" << frame->GetURL().ToString();
    otf::DiagLog("UI shell OnLoadEnd: httpStatus=" +
                 std::to_string(httpStatusCode) + " url=" +
                 frame->GetURL().ToString());
  }
  if (!view || IsNonTabBrowserViewId(view->GetID())) {
    return;
  }

  const int tab_id = view->GetID();
  http_upgraded_urls_.erase(tab_id);

  if (httpStatusCode >= 200 && httpStatusCode < 400 && store_ && tab_manager_) {
    const std::string url = frame->GetURL().ToString();
    UpdateSslErrorStateForLoadedPage(this, browser, tab_id, url);
    MaybeRecordHistoryVisit(this, tab_id, url);
    SyncZoomStateForLoadedPage(this, browser, tab_id, url);
    MaybeSendBookmarkSync(this, tab_id, url);
  }

  SendEvent(BuildTabPropertyEvent(tab_id, "load-end", true));
}

}  // namespace otf
