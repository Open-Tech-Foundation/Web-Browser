#include "otf_shutdown_runtime.h"

#include <sstream>
#include <string>

#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/base/cef_bind.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_certificate_runtime.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {
namespace {

std::string GetDataURI(const std::string& data, const std::string& mime_type) {
  return "data:" + mime_type + ";base64," +
         CefBase64Encode(data.data(), data.size()).ToString();
}

void MaybeMarkCertificateLoadError(OtfHandler* handler,
                                   CefRefPtr<CefBrowserView> view,
                                   cef_errorcode_t error_code,
                                   const std::string& failed_url) {
  if (!view || !handler->tab_manager_ || !IsCertificateErrorCode(error_code)) {
    return;
  }

  const int tab_id = view->GetID();
  handler->tab_manager_->SetUrl(tab_id, failed_url);
  handler->tab_manager_->SetSslError(tab_id, true);
  handler->tab_manager_->SetSslErrorUrl(tab_id, failed_url);
  handler->SendEvent(JsonObjectBuilder()
                         .AddInt("id", tab_id)
                         .AddString("key", "url")
                         .AddString("value", failed_url)
                         .Build());
  handler->SendEvent(JsonObjectBuilder()
                         .AddInt("id", tab_id)
                         .AddString("key", "sslError")
                         .AddBool("value", true)
                         .Build());
}

}  // namespace

bool OtfHandler::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (browser_list_.size() == 1) {
    is_closing_ = true;
  }
  return false;
}

void OtfHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  for (BrowserList::iterator it = browser_list_.begin();
       it != browser_list_.end(); ++it) {
    if ((*it)->IsSame(browser)) {
      browser_list_.erase(it);
      break;
    }
  }

  CefRefPtr<CefBrowserView> browser_view = CefBrowserView::GetForBrowser(browser);
  if (browser_view && browser_view->GetID() == kCertificateBrowserViewId) {
    certificate_browser_ = nullptr;
    certificate_subscription_ = nullptr;
  }

  if (OtfApp* app = OtfApp::GetInstance(); app && browser_view) {
    const int tab_id = browser_view->GetID();
    app->RemoveDiscardingTabId(tab_id);
    if (app->GetLazyTab(tab_id, nullptr)) {
      app->DetachContentView(browser_view);
      if (tab_manager_) {
        tab_manager_->SetView(tab_id, nullptr);
        tab_manager_->SetBrowser(tab_id, nullptr);
      }
    }
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
                             ErrorCode error_code,
                             const CefString& error_text,
                             const CefString& failed_url) {
  CEF_REQUIRE_UI_THREAD();
  if (error_code == ERR_ABORTED) {
    return;
  }

  LOG(ERROR) << "[otf] load error: " << std::string(failed_url) << " — "
             << std::string(error_text) << " (" << error_code << ")"
             << (frame->IsMain() ? " [main frame]" : " [subframe]");
  otf::DiagLog("LOAD ERROR: " + std::string(failed_url) + " — " +
               std::string(error_text) + " (" + std::to_string(error_code) +
               ")" + (frame->IsMain() ? " [main frame]" : " [subframe]"));

  if (frame->IsMain()) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
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

    MaybeMarkCertificateLoadError(this, view, error_code,
                                  failed_url.ToString());
  }

  std::stringstream ss;
  ss << "<html><body bgcolor=\"white\">"
        "<h2>Failed to load URL "
     << std::string(failed_url) << " with error " << std::string(error_text)
     << " (" << error_code << ").</h2></body></html>";
  frame->LoadURL(GetDataURI(ss.str(), "text/html"));
}

void OtfHandler::CloseAllBrowsers(bool force_close) {
  if (!CefCurrentlyOn(TID_UI)) {
    CefPostTask(
        TID_UI,
        base::BindOnce(
            [](bool force_close_on_ui) {
              if (auto* handler = OtfHandler::GetInstance()) {
                handler->CloseAllBrowsers(force_close_on_ui);
              }
            },
            force_close));
    return;
  }

  if (browser_list_.empty()) {
    return;
  }

  for (BrowserList::iterator it = browser_list_.begin();
       it != browser_list_.end(); ++it) {
    (*it)->GetHost()->CloseBrowser(force_close);
  }
}

}  // namespace otf
