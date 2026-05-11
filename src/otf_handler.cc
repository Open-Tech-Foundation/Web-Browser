#include "otf_handler.h"
#include "otf_app.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <fstream>
#include "otf_utils.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
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

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;      break;
    }
  }
  return out;
}

std::string GetDevUiUrl() {
  return CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");
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

    if (msg == "subscribe-events") {
      handler->subscription_callback_ = callback;
      return true;
    }

    if (msg == "get-tabs") {
      std::stringstream ss;
      ss << "[";
      std::vector<int> ids = handler->tab_manager_->GetAllTabIds();
      for (size_t i = 0; i < ids.size(); ++i) {
        std::string url = handler->tab_manager_->GetUrl(ids[i]);
        std::string title = handler->tab_manager_->GetTitle(ids[i]);
        ss << "{\"id\":" << ids[i]
           << ",\"url\":\"" << JsonEscape(url) << "\""
           << ",\"title\":\"" << JsonEscape(title) << "\"}";
        if (i < ids.size() - 1) ss << ",";
      }
      ss << "]";
      callback->Success(ss.str());
      return true;
    }

    if (msg == "get-settings") {
      std::string path = otf::GetSettingsFilePath();
      std::ifstream f(path);
      if (f.is_open()) {
        std::stringstream buffer;
        buffer << f.rdbuf();
        callback->Success(buffer.str());
      } else {
        callback->Success("{\"searchEngine\":\"\"}");
      }
      return true;
    }

    if (msg.find("set-settings:") == 0) {
      std::string json = msg.substr(13);
      std::string path = otf::GetSettingsFilePath();
      std::ofstream f(path);
      if (f.is_open()) {
        f << json;
        callback->Success("");
        handler->SendEvent("{\"key\":\"settings-changed\",\"value\":\"" + JsonEscape(json) + "\"}");
      } else {
        callback->Failure(1, "Failed to save settings");
      }
      return true;
    }

    if (msg.find("navigate:") == 0) {
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
      callback->Success(std::to_string(id));
    } else if (msg.find("new-tab:") == 0) {
      std::string url = msg.substr(8);
      OtfApp* app = OtfApp::GetInstance();
      if (!app) { callback->Failure(1, "App not ready"); return true; }
      int id = app->CreateTab(url);
      callback->Success(std::to_string(id));
    } else if (msg.find("close-tab:") == 0) {
      int tab_id = std::stoi(msg.substr(10));
      OtfApp* app = OtfApp::GetInstance();
      if (app) app->CloseTab(tab_id);
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
    if (tab_manager_) tab_manager_->SetTitle(view->GetID(), title.ToString());
    std::stringstream ss;
    ss << "{\"id\":" << view->GetID() << ",\"key\":\"title\",\"value\":\"" << JsonEscape(title.ToString()) << "\"}";
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
      std::string url_str = url.ToString();
      std::string scheme_url = tab_manager_->GetSchemeUrl(view->GetID());

      // Always store the URL so get-tabs can retrieve it
      if (tab_manager_) {
        tab_manager_->SetUrl(view->GetID(), url_str);
      }

      if (!scheme_url.empty() && url_str != scheme_url) {
        return;
      }

      if (url_str.find("browser://") == 0) {
        return;
      }

      std::string dev_ui_url = GetDevUiUrl();
      if (!dev_ui_url.empty()) {
        std::string prefix = dev_ui_url + "/";
        if (url_str.find(prefix) == 0) {
          return;
        }
      }

      std::stringstream ss;
      ss << "{\"id\":" << view->GetID() << ",\"key\":\"url\",\"value\":\"" << JsonEscape(url_str) << "\"}";
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
    ss << "{\"id\":" << view->GetID() << ",\"key\":\"favicon\",\"value\":\"" << JsonEscape(icon_urls[0].ToString()) << "\"}";
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

void OtfHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            int httpStatusCode) {
  if (!frame->IsMain()) return;
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (!view || view->GetID() == kUiBrowserViewId) return;

  std::stringstream ss;
  ss << "{\"id\":" << view->GetID() << ",\"key\":\"load-end\",\"value\":\"true\"}";
  SendEvent(ss.str());
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
    } else if (tab_manager_) {
      int tab_id = browser_view->GetID();
      tab_manager_->SetBrowser(tab_id, browser);
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
    tab_manager_->SetSchemeUrl(new_id, url);

    std::stringstream ss;
    ss << "{\"id\":0,\"key\":\"new-tab\",\"value\":\"{\\\"id\\\":" << new_id
       << ",\\\"url\\\":\\\"" << JsonEscape(url) << "\\\",\\\"title\\\":\\\"New Tab\\\"}\"}";
    SendEvent(ss.str());

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
  std::string url = request->GetURL().ToString();

  // Block chrome schemes and other unwanted schemes
  if (url.rfind("chrome://", 0) == 0 ||
      url.rfind("chrome-devtools://", 0) == 0 ||
      url.rfind("chrome-extension://", 0) == 0 ||
      url.rfind("chrome-search://", 0) == 0 ||
      url.rfind("chrome-untrusted://", 0) == 0 ||
      url.rfind("devtools://", 0) == 0) {
    return true;
  }

  std::string dev_ui_url = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");

  if (!dev_ui_url.empty() && url.rfind("browser://", 0) == 0) {
    std::string path = url.substr(10);
    std::string transformed = dev_ui_url + "/" + path + ".html";
    request->SetURL(transformed);
  }

  // Block javascript: and data: URLs
  if (url.rfind("javascript:", 0) == 0 ||
      (url.rfind("data:", 0) == 0 &&
       url.find("/ui/") == std::string::npos)) {
    return true;
  }

  return false;
}

} // namespace otf
