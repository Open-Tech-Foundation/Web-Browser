#ifndef OTF_BROWSER_HANDLER_H_
#define OTF_BROWSER_HANDLER_H_

#include <map>
#include <list>
#include <memory>
#include "include/cef_client.h"
#include "include/cef_download_handler.h"
#include "include/wrapper/cef_message_router.h"
#include "otf_browser_shell.h"
#include "otf_store.h"

namespace otf {

class OtfHandler : public CefClient,
                   public CefDisplayHandler,
                   public CefLifeSpanHandler,
                   public CefLoadHandler,
                   public CefDownloadHandler,
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
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override {
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
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
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
                     bool* no_javascript_access) override;
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

  // CefDownloadHandler methods:
  bool CanDownload(CefRefPtr<CefBrowser> browser,
                   const CefString& url,
                   const CefString& request_method) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;

  // CefContextMenuHandler methods:
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool RunContextMenu(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefContextMenuParams> params,
                      CefRefPtr<CefMenuModel> model,
                      CefRefPtr<CefRunContextMenuCallback> callback) override;
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
  bool OnCertificateError(CefRefPtr<CefBrowser> browser,
                          ErrorCode cert_error,
                          const CefString& request_url,
                          CefRefPtr<CefSSLInfo> ssl_info,
                          CefRefPtr<CefCallback> callback) override;

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
  std::string GetDownloadsJson() const;
  void NotifyDownloadsChanged();
  void NotifyDownloadBadge();
  void NotifyBookmarkStateForTab(int tab_id);
  void NotifyNewTab(int new_tab_id, int parent_tab_id = -1);

  TabManager* tab_manager_;
  CefRefPtr<CefBrowser> ui_browser_;
  CefRefPtr<CefBrowser> findbar_browser_;
  CefRefPtr<CefBrowser> certificate_browser_;
  std::string last_closed_url_;

  void SendEvent(const std::string& event_json);

  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> subscription_callback_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> findbar_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> zoombar_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> downloads_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> certificate_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> bookmark_subscription_;

  // Per-tab find state owned by tab_manager_ (text + case)
  // Pending find text for async result correlation
  std::string pending_find_text_;
  int         pending_find_tab_ = -1;
  int         restore_find_target_ordinal_ = 0;
  bool        restore_find_in_progress_ = false;

  struct DownloadState {
    int id = 0;
    uint32_t runtime_id = 0;
    std::string url;
    std::string original_url;
    std::string suggested_name;
    std::string mime_type;
    std::string full_path;
    std::string status;
    int percent = -1;
    int64_t received_bytes = 0;
    int64_t total_bytes = 0;
    int64_t speed_bytes_per_sec = 0;
    int64_t ended_at = 0;
    bool is_in_progress = false;
    bool is_complete = false;
    bool is_canceled = false;
    bool is_interrupted = false;
    bool is_paused = false;
    bool can_cancel = false;
    bool can_pause = false;
    bool can_resume = false;
    bool can_open = false;
    bool can_show_in_folder = false;
  };

  std::map<int, DownloadState> downloads_;
  std::map<int, CefRefPtr<CefDownloadItemCallback>> download_callbacks_;
  std::map<uint32_t, int> runtime_download_ids_;
  std::unique_ptr<OtfStore> store_;

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
