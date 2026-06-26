#include "otf_browse_runtime.h"

#include <string>
#include <utility>

#include "include/cef_command_line.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_utils.h"

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

bool IsSecurityErrorDocumentUrl(const std::string& url) {
  return url.rfind("browser://insecure-blocked", 0) == 0 ||
         url.find("/insecure-blocked.html") != std::string::npos ||
         url.rfind("chrome-error://", 0) == 0 ||
         url.rfind("data:", 0) == 0;
}

class DeferredTabRedirectTask : public CefTask {
 public:
  DeferredTabRedirectTask(const std::string& url, int old_tab_id)
      : url_(url), old_tab_id_(old_tab_id) {}

  void Execute() override {
    OtfApp* app = OtfApp::GetInstance();
    OtfHandler* handler = OtfHandler::GetInstance();
    if (!app || !handler) {
      return;
    }
    const bool was_private =
        handler->tab_manager_ && handler->tab_manager_->IsPrivate(old_tab_id_);
    const int new_id = app->CreateTab(url_, -1, was_private);
    if (new_id < 0) {
      return;
    }
    handler->NotifyNewTab(new_id, -1);
    app->SwitchTab(new_id);
    handler->CloseTabAndNotify(old_tab_id_);
  }

 private:
  std::string url_;
  int old_tab_id_;
  IMPLEMENT_REFCOUNTING(DeferredTabRedirectTask);
};

class DeferredFrameLoadTask : public CefTask {
 public:
  DeferredFrameLoadTask(CefRefPtr<CefFrame> frame, std::string url)
      : frame_(frame), url_(std::move(url)) {}

  void Execute() override {
    if (frame_) frame_->LoadURL(url_);
  }

 private:
  CefRefPtr<CefFrame> frame_;
  std::string url_;
  IMPLEMENT_REFCOUNTING(DeferredFrameLoadTask);
};

void ClearDedicatedPreviewModeIfNeeded(OtfHandler* handler,
                                       int tab_id,
                                       const std::string& next_url) {
  if (!handler->tab_manager_) {
    return;
  }

  const bool is_image_preview_url =
      next_url == "browser://imagepreview" ||
      next_url.rfind("browser://image-preview/", 0) == 0 ||
      next_url.find("/imagepreview.html") != std::string::npos;
  if (handler->tab_manager_->GetImagePreviewMode(tab_id) ==
          ImagePreviewMode::kDedicated &&
      !is_image_preview_url) {
    handler->tab_manager_->SetSchemeUrl(tab_id, "");
    handler->tab_manager_->SetImagePreviewMode(tab_id, ImagePreviewMode::kNone);
    handler->SetImagePreviewUrlForTab(tab_id, "");
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->HideImagePreviewOverlay();
    }
  }

  const bool is_doc_preview_url =
      next_url == "browser://docpreview" ||
      next_url.rfind("browser://doc-preview/", 0) == 0 ||
      next_url.find("/docpreview.html") != std::string::npos;
  if (handler->tab_manager_->GetDocPreviewMode(tab_id) ==
          DocPreviewMode::kDedicated &&
      !is_doc_preview_url) {
    handler->tab_manager_->SetSchemeUrl(tab_id, "");
    handler->tab_manager_->SetDocPreviewMode(tab_id, DocPreviewMode::kNone);
    handler->SetDocPreviewUrlForTab(tab_id, "");
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->HideDocPreviewOverlay();
    }
  }
}

void MaybeClearSslErrorState(OtfHandler* handler,
                             int tab_id,
                             const std::string& next_url) {
  if (!handler->tab_manager_) {
    return;
  }
  const std::string current_url = handler->tab_manager_->GetUrl(tab_id);
  const std::string ssl_error_url = handler->tab_manager_->GetSslErrorUrl(tab_id);
  const bool is_real_navigation =
      IsPersistableWebUrl(next_url) ||
      (next_url.rfind("browser://", 0) == 0 &&
       !IsSecurityErrorDocumentUrl(next_url));
  if (handler->tab_manager_->HasSslError(tab_id) &&
      current_url != next_url &&
      next_url != ssl_error_url &&
      is_real_navigation) {
    handler->tab_manager_->SetSslError(tab_id, false);
    handler->SendEvent(JsonObjectBuilder()
                           .AddInt("id", tab_id)
                           .AddString("key", "sslError")
                           .AddBool("value", false)
                           .Build());
  }
}

bool MaybeRedirectForJavascriptPermission(OtfHandler* handler,
                                          CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          int tab_id,
                                          const std::string& next_url) {
  const std::string origin = ExtractOrigin(next_url);
  if (origin.empty() || !handler->store_ || handler->IsGuestTab(tab_id)) {
    return false;
  }

  const bool dest_blocked =
      handler->store_->GetSitePermission(origin, "javascript") == "block";
  const bool tab_js_disabled = handler->IsTabJsDisabled(tab_id);
  if (dest_blocked == tab_js_disabled) {
    return false;
  }

  handler->NotifyMessageRouterBeforeBrowse(browser, frame);
  CefPostTask(TID_UI, new DeferredTabRedirectTask(next_url, tab_id));
  return true;
}

bool IsBlockedTopLevelScheme(const std::string& url) {
  return url.rfind("chrome://", 0) == 0 ||
         url.rfind("chrome-devtools://", 0) == 0 ||
         url.rfind("chrome-extension://", 0) == 0 ||
         url.rfind("chrome-search://", 0) == 0 ||
         url.rfind("chrome-untrusted://", 0) == 0 ||
         url.rfind("devtools://", 0) == 0 ||
         url.rfind("javascript:", 0) == 0 ||
         url.rfind("about:srcdoc", 0) == 0;
}

}  // namespace

bool OtfHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefRequest> request,
                                bool user_gesture,
                                bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();
  (void)user_gesture;
  (void)is_redirect;

  const std::string url = request->GetURL().ToString();
  if (url.rfind("javascript:", 0) == 0 || url.rfind("vbscript:", 0) == 0) {
    return true;
  }

  if (frame->IsMain()) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view && tab_manager_ && !IsNonTabBrowserViewId(view->GetID())) {
      const int tab_id = view->GetID();
      if (MaybeRedirectForJavascriptPermission(this, browser, frame, tab_id, url)) {
        return true;
      }
      MaybeClearSslErrorState(this, tab_id, url);
      ClearDedicatedPreviewModeIfNeeded(this, tab_id, url);
    }

    if (!pending_new_tab_urls_.empty()) {
      auto it = pending_new_tab_urls_.find(url);
      if (it != pending_new_tab_urls_.end()) {
        pending_new_tab_urls_.erase(it);
        return true;
      }
    }
  }

  NotifyMessageRouterBeforeBrowse(browser, frame);

  const bool is_main_frame = !frame || frame->IsMain();
  if ((url.rfind("chrome-extension://", 0) == 0 ||
       url.rfind("chrome-untrusted://", 0) == 0) &&
      !is_main_frame) {
    return false;
  }

  if (IsBlockedTopLevelScheme(url)) {
    return true;
  }

  if (is_main_frame &&
      (url.rfind("data:", 0) == 0 || url.rfind("blob:", 0) == 0)) {
    return true;
  }

  if (otf::IsLocalFilesystemPathLike(url) || url.rfind("file://", 0) == 0) {
    return true;
  }

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (is_main_frame) {
    const std::string stripped_url = otf::StripTrackingParamsFromUrl(url);
    if (stripped_url != url) {
      CefPostTask(TID_UI, new DeferredFrameLoadTask(frame, stripped_url));
      return true;
    }
  }

  if (is_main_frame && url.rfind("http://", 0) == 0 && !IsAllowedHttpUrl(url)) {
    const std::string https_url = "https://" + url.substr(7);
    if (view && tab_manager_) {
      const int tab_id = view->GetID();
      http_upgraded_urls_[tab_id] = url;
      tab_manager_->SetSslError(tab_id, false);
    }
    request->SetURL(https_url);
    return false;
  }

  const std::string dev_ui_url =
      CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");
  if (!dev_ui_url.empty() && IsAllowedBrowserPageUrl(url)) {
    request->SetURL(GetBrowserPageDevUrl(dev_ui_url, url));
  }

  return false;
}

}  // namespace otf
