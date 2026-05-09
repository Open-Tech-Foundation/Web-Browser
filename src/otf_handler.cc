#include "otf_handler.h"

#include <sstream>
#include <string>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace otf {

namespace {

OtfHandler* g_instance = nullptr;

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
    std::string url = request.ToString();
    OtfHandler* handler = OtfHandler::GetInstance();
    
    if (handler && handler->tab_manager_) {
      BrowserTab* active_tab = handler->tab_manager_->GetActiveTab();
      if (active_tab && active_tab->browser) {
        active_tab->browser->GetMainFrame()->LoadURL(url);
        callback->Success("OK");
        return true;
      }
    }
    callback->Failure(1, "No active tab found");
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
}

OtfHandler::~OtfHandler() {
  g_instance = nullptr;
}

// static
OtfHandler* OtfHandler::GetInstance() {
  return g_instance;
}

void OtfHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                               const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  // Future: Update Tab UI title
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
  if (browser_view && tab_manager_) {
    tab_manager_->SetBrowser(browser_view->GetID(), browser);
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

} // namespace otf
