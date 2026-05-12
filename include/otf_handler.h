#ifndef OTF_BROWSER_HANDLER_H_
#define OTF_BROWSER_HANDLER_H_

#include <list>
#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"
#include "otf_browser_shell.h"

namespace otf {

class OtfHandler : public CefClient,
                   public CefDisplayHandler,
                   public CefLifeSpanHandler,
                   public CefLoadHandler,
                   public CefContextMenuHandler,
                   public CefRequestHandler,
                   public CefKeyboardHandler,
                   public CefFindHandler {
 public:
  explicit OtfHandler(bool use_alloy_style);
  ~OtfHandler() override;

  static OtfHandler* GetInstance();

  // CefClient methods:
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override {
    return this;
  }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override {
    return this;
  }
  CefRefPtr<CefFindHandler> GetFindHandler() override {
    return this;
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // CefDisplayHandler methods:
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                          const std::vector<CefString>& icon_urls) override;

  // CefLifeSpanHandler methods:
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefLoadHandler methods:
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool isLoading,
                            bool canGoBack,
                            bool canGoForward) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int httpStatusCode) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode,
                   const CefString& errorText,
                   const CefString& failedUrl) override;

  // CefContextMenuHandler methods:
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id,
                            EventFlags event_flags) override;

  // CefRequestHandler methods:
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       CefRefPtr<CefRequest> request,
                       bool user_gesture,
                       bool is_redirect) override;

  // CefKeyboardHandler methods:
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;

  // CefFindHandler methods:
  void OnFindResult(CefRefPtr<CefBrowser> browser,
                    int identifier,
                    int count,
                    const CefRect& selectionRect,
                    int activeMatchOrdinal,
                    bool finalUpdate) override;

  void CloseAllBrowsers(bool force_close);
  bool IsClosing() const { return is_closing_; }

  TabManager* tab_manager_;
  CefRefPtr<CefBrowser> ui_browser_;
  CefRefPtr<CefBrowser> findbar_browser_;
  std::string last_closed_url_;

  void SendEvent(const std::string& event_json);

  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> subscription_callback_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> findbar_subscription_;

  // Per-tab find state owned by tab_manager_ (text + case)
  // Pending find text for async result correlation
  std::string pending_find_text_;
  int         pending_find_tab_ = -1;
  int         restore_find_target_ordinal_ = 0;
  bool        restore_find_in_progress_ = false;

 private:
  const bool use_alloy_style_;
  CefRefPtr<CefMessageRouterBrowserSide> message_router_;
  
  typedef std::list<CefRefPtr<CefBrowser>> BrowserList;
  BrowserList browser_list_;
  bool is_closing_;

  IMPLEMENT_REFCOUNTING(OtfHandler);
};

} // namespace otf

#endif // OTF_BROWSER_HANDLER_H_
