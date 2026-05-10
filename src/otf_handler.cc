#include "otf_handler.h"
#include "otf_app.h"

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
const int MENU_ID_OPEN_IN_NEW_TAB = 10001;

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

    if (msg == "subscribe-events") {
      handler->subscription_callback_ = callback;
      return true;
    }

    if (msg == "get-tabs") {
      std::stringstream ss;
      ss << "[";
      std::vector<int> ids = handler->tab_manager_->GetAllTabIds();
      for (size_t i = 0; i < ids.size(); ++i) {
        CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(ids[i]);
        ss << "{\"id\":" << ids[i] 
           << ",\"url\":\"" << (b ? b->GetMainFrame()->GetURL().ToString() : "") << "\""
           << ",\"title\":\"" << (b ? handler->tab_manager_->GetView(ids[i])->GetBrowser()->GetMainFrame()->GetURL().ToString() : "New Tab") << "\"}";
        if (i < ids.size() - 1) ss << ",";
      }
      ss << "]";
      callback->Success(ss.str());
      return true;
    }

    if (msg.find("navigate:") == 0) {
      size_t colon = msg.find(':', 9);
      int tab_id = std::stoi(msg.substr(9, colon - 9));
      std::string url = msg.substr(colon + 1);
      CefRefPtr<CefBrowser> b = handler->tab_manager_->GetBrowser(tab_id);
      if (b) b->GetMainFrame()->LoadURL(url);
      callback->Success("");
    } else if (msg.find("new-tab:") == 0) {
      std::string url = msg.substr(8);
      int id = OtfApp::GetInstance()->CreateTab(url);
      callback->Success(std::to_string(id));
    } else if (msg.find("close-tab:") == 0) {
      int tab_id = std::stoi(msg.substr(10));
      OtfApp::GetInstance()->CloseTab(tab_id);
      callback->Success("");
    } else if (msg.find("switch-tab:") == 0) {
      int tab_id = std::stoi(msg.substr(11));
      OtfApp::GetInstance()->SwitchTab(tab_id);
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
}

OtfHandler::~OtfHandler() {
  g_instance = nullptr;
}

// static
OtfHandler* OtfHandler::GetInstance() {
  return g_instance;
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
    std::stringstream ss;
    ss << "{\"id\":" << view->GetID() << ",\"key\":\"title\",\"value\":\"" << title.ToString() << "\"}";
    SendEvent(ss.str());
  }
}

void OtfHandler::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (frame->IsMain()) {
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view) {
      std::stringstream ss;
      ss << "{\"id\":" << view->GetID() << ",\"key\":\"url\",\"value\":\"" << url.ToString() << "\"}";
      SendEvent(ss.str());
    }
  }
}

void OtfHandler::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                    const std::vector<CefString>& icon_urls) {
  CEF_REQUIRE_UI_THREAD();
  if (icon_urls.empty()) return;

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view) {
    std::stringstream ss;
    ss << "{\"id\":" << view->GetID() << ",\"key\":\"favicon\",\"value\":\"" << icon_urls[0].ToString() << "\"}";
    SendEvent(ss.str());
  }
}

void OtfHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                      bool isLoading,
                                      bool canGoBack,
                                      bool canGoForward) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view) {
    std::stringstream ss;
    ss << "{\"id\":" << view->GetID() << ",\"key\":\"loading\",\"value\":\"" << (isLoading ? "true" : "false") << "\"}";
    SendEvent(ss.str());
    
    // Also sync navigation state
    std::stringstream ss_back;
    ss_back << "{\"id\":" << view->GetID() << ",\"key\":\"canGoBack\",\"value\":\"" << (canGoBack ? "true" : "false") << "\"}";
    SendEvent(ss_back.str());

    std::stringstream ss_forward;
    ss_forward << "{\"id\":" << view->GetID() << ",\"key\":\"canGoForward\",\"value\":\"" << (canGoForward ? "true" : "false") << "\"}";
    SendEvent(ss_forward.str());
  }
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
    if (browser_view->GetID() == 100) {
      ui_browser_ = browser;
    } else if (tab_manager_) {
      tab_manager_->SetBrowser(browser_view->GetID(), browser);
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
  if (!params->GetLinkUrl().empty()) {
    model->InsertItemAt(0, MENU_ID_OPEN_IN_NEW_TAB, "Open in new tab");
    model->InsertSeparatorAt(1);
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
    int new_id = OtfApp::GetInstance()->CreateTab(url);
    
    // Notify UI Shell about the new tab
    std::stringstream ss;
    ss << "{\"id\":0,\"key\":\"new-tab\",\"value\":\"{\\\"id\\\":" << new_id 
       << ",\\\"url\\\":\\\"" << url << "\\\",\\\"title\\\":\\\"New Tab\\\"}\"}";
    SendEvent(ss.str());
    
    return true;
  }
  return false;
}

} // namespace otf
