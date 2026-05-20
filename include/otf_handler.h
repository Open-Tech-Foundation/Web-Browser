#ifndef OTF_BROWSER_HANDLER_H_
#define OTF_BROWSER_HANDLER_H_

#include <map>
#include <list>
#include <memory>
#include <cstdint>
#include "include/cef_client.h"
#include "include/cef_download_handler.h"
#include "include/wrapper/cef_message_router.h"
#include "otf_browser_shell.h"
#include "otf_devtools_bridge.h"
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
  struct ImagePreviewRenderCache {
    std::string file_path;
    std::string display_url;
    int page = 0;
    int page_count = 1;
  };

  struct ImagePreviewDownloadCache {
    std::string source_url;
    std::string mime_type;
    std::string raw_bytes;
    std::string display_url;
    int64_t file_size_bytes = -1;
    int page = 0;
    int page_count = 1;
    bool is_tiff = false;
  };

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
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> image_preview_subscription_;
  // The cleardata popup ships its origin to the renderer via a restore
  // producer attached to the PopupOverlay. The producer reads this field,
  // which is set by the show-clear-site-data:<origin> handler.
  std::string pending_cleardata_origin_;
  std::string pending_qr_url_;

  // Async DevTools-protocol callback router. Attached to the UI shell
  // browser in OnAfterCreated. Used by get-storage-for-site to call
  // Storage.getUsageAndQuota and route the response back to the
  // originating cefQuery callback.
  CefRefPtr<DevToolsBridge> devtools_bridge_;
  std::map<int, CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback>> tab_image_preview_subscriptions_;

  std::map<int, std::string> tab_image_preview_urls_;
  // Trusted local files selected from persisted download records. The
  // renderer only receives browser://image-preview/... tokens; it never gets
  // a file:// URL or raw filesystem path.
  std::map<int, std::string> tab_image_preview_local_files_;
  // Image info shown in the preview sidebar. Stored in C++ so a tab switch
  // restores the same metadata without recomputing it from the source URL.
  std::map<int, int64_t> tab_image_preview_file_sizes_;
  std::map<int, std::string> tab_image_preview_formats_;
  std::map<int, ImagePreviewRenderCache> tab_image_preview_render_cache_;
  std::map<int, ImagePreviewDownloadCache> tab_image_preview_download_cache_;
  // Per-tab TIFF navigation state. Persisted in C++ so tab switches and
  // re-subscribes restore the page the user was viewing.
  std::map<int, int> tab_image_preview_pages_;
  std::map<int, int> tab_image_preview_page_counts_;
  std::map<int, uint64_t> tab_image_preview_decode_nonces_;
  void SetImagePreviewUrlForTab(int tab_id, const std::string& url);
  void SetImagePreviewLocalFileForTab(int tab_id,
                                      const std::string& public_url,
                                      const std::string& file_path);
  void ClearInlineImagePreviewForTab(int tab_id);
  std::string GetImagePreviewUrlForTab(int tab_id) const;
  std::string GetImagePreviewLocalFileForTab(int tab_id) const;
  void SetImagePreviewFileSizeForTab(int tab_id, int64_t file_size_bytes);
  int64_t GetImagePreviewFileSizeForTab(int tab_id) const;
  void SetImagePreviewFormatForTab(int tab_id, const std::string& format);
  std::string GetImagePreviewFormatForTab(int tab_id) const;
  void CloseTabAndNotify(int tab_id);
  uint64_t BumpImagePreviewDecodeNonceForTab(int tab_id);
  uint64_t GetImagePreviewDecodeNonceForTab(int tab_id) const;
  void NotifyImagePreviewDownloadProgress(int tab_id,
                                          uint64_t decode_nonce,
                                          int received_bytes,
                                          int total_bytes);
  void SetImagePreviewPageForTab(int tab_id, int page);
  int GetImagePreviewPageForTab(int tab_id) const;
  void SetImagePreviewPageCountForTab(int tab_id, int count);
  int GetImagePreviewPageCountForTab(int tab_id) const;
  // Build a JSON `load-image` event for a tab using stored url/page state.
  // Also refreshes the stored page_count from the decode result. Returns ""
  // if the tab has no preview url.
  std::string BuildImagePreviewLoadEvent(int tab_id, bool bump_decode_nonce = true);

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

  // Workspace state. active_workspace_id_ is the workspace whose tabs are
  // currently surfaced in the UI; new tabs join it. workspace_contexts_ is
  // populated lazily — the default workspace (id 1) uses the global
  // request context (nullptr sentinel), while subsequent workspaces get a
  // per-workspace CefRequestContext with its own cache_path.
  int active_workspace_id_ = 1;
  std::map<int, CefRefPtr<CefRequestContext>> workspace_contexts_;
  // Remembers the last active tab per workspace so switching back to a
  // workspace with live in-memory tabs restores the correct tab, not
  // just the first one that was created.
  std::map<int, int> workspace_last_active_tab_;
  // Set to true when startup_behavior is "newtab" so that the auto-opened
  // startup newtab tab cannot overwrite the saved workspace session in the DB.
  // Clears itself in PersistWorkspaceTabs once any live tab has a real URL.
  bool startup_session_guard_ = false;
  // Returns the request context for the active workspace. Returns nullptr
  // for the default workspace, which is the signal the BrowserView API
  // expects to mean "use the global context".
  CefRefPtr<CefRequestContext> GetActiveWorkspaceRequestContext();
  CefRefPtr<CefRequestContext> GetWorkspaceRequestContext(int workspace_id);
  // Snapshot the live state of every tab in a workspace and replace the
  // persisted workspace_tabs rows for it. Called from per-tab change hooks
  // (address/title/favicon) and lifecycle events (new/close/switch) so a
  // crash or quit never loses more than the most recent change.
  void PersistWorkspaceTabs(int workspace_id);
  void PersistWorkspaceForTab(int tab_id);

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
