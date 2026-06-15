#ifndef OTF_BROWSER_HANDLER_H_
#define OTF_BROWSER_HANDLER_H_

#include <deque>
#include <map>
#include <set>
#include <list>
#include <memory>
#include <mutex>
#include <cstdint>
#include "include/cef_client.h"
#include "include/cef_download_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_task_manager.h"
#include "include/wrapper/cef_message_router.h"
#include "otf_browser_shell.h"
#include "otf_devtools_bridge.h"
#include "otf_native_rpc.h"
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
                   public CefFindHandler,
                   public CefFocusHandler {
 public:
  struct SplitViewState {
    bool enabled = false;
    int left_tab_id = -1;
    int right_tab_id = -1;
    int active_tab_id = -1;
    std::string left_url;
    std::string right_url;
    std::string active_url;
  };

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

  struct DocPreviewRenderCache {
    std::string file_path;
    std::string display_url;
  };

  struct ClosedTabInfo {
    std::string url;
    std::string title;
    std::string favicon;
    int workspace_id = 0;
    bool is_image_preview = false;
    bool is_doc_preview = false;
    std::string preview_local_path;
    int preview_page = 0;
  };

  explicit OtfHandler(bool use_alloy_style);
  ~OtfHandler() override;

  static OtfHandler* GetInstance();
  OtfStore* GetStore() { return store_.get(); }

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
  CefRefPtr<CefFocusHandler> GetFocusHandler() override {
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
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                              bool fullscreen) override;
  void OnStatusMessage(CefRefPtr<CefBrowser> browser,
                       const CefString& value) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                        cef_log_severity_t level,
                        const CefString& message,
                        const CefString& source,
                        int line) override;

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
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         const CefString& target_url,
                         cef_window_open_disposition_t target_disposition,
                         bool user_gesture) override;
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

  // CefFocusHandler methods:
  void OnGotFocus(CefRefPtr<CefBrowser> browser) override;

  void CloseAllBrowsers(bool force_close);
  bool IsClosing() const { return is_closing_; }

  std::string GetDownloadsJson() const;
  bool OpenDownloadsPageFromOverlay(CefRefPtr<CefBrowser> browser,
                                    std::string* error);
  bool ApplyDownloadAction(uint32_t download_id,
                           const std::string& action,
                           CefRefPtr<CefBrowser> browser,
                           std::string* error);
  std::string BuildTabsJson() const;
  bool ApplyTabZoomAction(int tab_id, const std::string& action);
  bool SplitCurrentTab(std::string* error);
  bool AddTabToSplit(int target_tab_id, std::string* error);
  bool CloseSplitView(std::string* error);
  bool SwapSplitView(std::string* error);
  bool CloseSplitPane(const std::string& pane, std::string* error);
  void NotifyDownloadsChanged();
  void NotifyDownloadBadge();
  void NotifyBookmarkStateForTab(int tab_id);
  void NotifyNewTab(int new_tab_id, int parent_tab_id = -1);

  void StartMemoryLogging();
  void StopMemoryLogging();
  void LogTabMemoryUsage();
  bool IsMemoryLoggingRunning() const { return memory_log_running_; }
  SplitViewState GetSplitViewState(int workspace_id) const;
  std::string BuildSplitViewStateJson(int workspace_id) const;
  bool ApplySplitViewState(int workspace_id);
  bool SetSplitViewTabs(int workspace_id,
                        int left_tab_id,
                        int right_tab_id,
                        int active_tab_id);
  bool ClearSplitViewState(int workspace_id);
  std::string GetCertificateJsonForTab(int tab_id);
  bool RestartBrowser();
  bool StartSnipCapture(bool hide_app_menu, std::string* error);

  TabManager* tab_manager_;
  CefRefPtr<CefBrowser> ui_browser_;
  CefRefPtr<CefBrowser> findbar_browser_;
  CefRefPtr<CefBrowser> certificate_browser_;
  CefRefPtr<CefBrowser> link_preview_browser_;
  CefRefPtr<CefBrowser> toast_browser_;
  CefRefPtr<CefBrowser> snip_preview_browser_;
  std::deque<ClosedTabInfo> recently_closed_tabs_;

  void SendEvent(const std::string& event_json);

  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> subscription_callback_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> findbar_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> zoombar_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> downloads_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> certificate_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> bookmark_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> image_preview_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> doc_preview_subscription_;
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> console_subscription_;
  // The cleardata popup ships its origin to the renderer via a restore
  // producer attached to the PopupOverlay. The producer reads this field,
  // which is set by the strict siteData.showClearPopup RPC.
  std::string pending_cleardata_origin_;
  std::string pending_qr_url_;
  // Pending popup data for the blockedpopup overlay restore producer.
  int popup_ask_pending_id_ = -1;
  std::string popup_ask_pending_url_;
  std::string popup_ask_pending_origin_;
  std::string download_ask_pending_url_;
  std::string download_ask_pending_origin_;
  std::string download_ask_pending_name_;
  CefRefPtr<CefBrowser> download_ask_pending_browser_;

  // Async DevTools-protocol callback router. Attached to the UI shell
  // browser in OnAfterCreated. Used by get-storage-for-site to call
  // Storage.getUsageAndQuota and route the response back to the
  // originating cefQuery callback.
  CefRefPtr<DevToolsBridge> devtools_bridge_;
  std::map<int, CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback>> tab_image_preview_subscriptions_;
  std::map<int, CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback>> tab_doc_preview_subscriptions_;

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
  void ScheduleImagePreviewPushForTab(int tab_id);
  void ScheduleDelayedImagePreviewPushForTab(int tab_id, int64_t delay_ms);
  void SetImagePreviewUrlForTab(int tab_id, const std::string& url);
  void SetImagePreviewLocalFileForTab(int tab_id,
                                      const std::string& public_url,
                                      const std::string& file_path);
  void ClearInlineImagePreviewForTab(int tab_id);
  void ClearImagePreviewStateForTab(int tab_id);
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
  bool HandleImagePreviewDecodeRequest(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
      const NativeRpcRequest& request,
      bool thumbnail_request,
      uint64_t decode_nonce,
      int page_index,
      int explicit_tab_id,
      const std::string& source_url);
  void SetImagePreviewPageForTab(int tab_id, int page);
  int GetImagePreviewPageForTab(int tab_id) const;
  void SetImagePreviewPageCountForTab(int tab_id, int count);
  int GetImagePreviewPageCountForTab(int tab_id) const;
  // Build a JSON `load-image` event for a tab using stored url/page state.
  // Also refreshes the stored page_count from the decode result. Returns ""
  // if the tab has no preview url.
  std::string BuildImagePreviewLoadEvent(int tab_id, bool bump_decode_nonce = true);

  // ── Doc preview state per tab ──
  std::map<int, std::string> tab_doc_preview_urls_;
  std::map<int, std::string> tab_doc_preview_local_files_;
  std::map<int, std::string> tab_doc_preview_content_urls_;
  std::map<int, int64_t> tab_doc_preview_file_sizes_;
  std::map<int, std::string> tab_doc_preview_formats_;
  std::map<int, DocPreviewRenderCache> tab_doc_preview_render_cache_;
  void ScheduleDocPreviewPushForTab(int tab_id);
  void ScheduleDocPreviewFetchForTab(int tab_id, const std::string& url);
  void ResetDocPreviewFetchStateForTab(int tab_id);
  void SetDocPreviewUrlForTab(int tab_id, const std::string& url);
  void SetDocPreviewLocalFileForTab(int tab_id, const std::string& public_url,
                                    const std::string& file_path);
  void SetDocPreviewContentUrlForTab(int tab_id, const std::string& content_url);
  std::string GetDocPreviewUrlForTab(int tab_id) const;
  std::string GetDocPreviewLocalFileForTab(int tab_id) const;
  std::string GetDocPreviewContentUrlForTab(int tab_id) const;
  void SetDocPreviewFileSizeForTab(int tab_id, int64_t file_size_bytes);
  int64_t GetDocPreviewFileSizeForTab(int tab_id) const;
  void SetDocPreviewFormatForTab(int tab_id, const std::string& format);
  std::string GetDocPreviewFormatForTab(int tab_id) const;
  void ClearDocPreviewStateForTab(int tab_id);
  std::string BuildDocPreviewLoadEvent(int tab_id);

  // Per-tab find state owned by tab_manager_ (text + case)
  // Pending find text for async result correlation
  std::string pending_find_text_;
  int         pending_find_tab_ = -1;
  int         pending_find_seq_ = 0;
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
    int failure_reason = 0;
  };

  std::map<int, DownloadState> downloads_;
  std::map<int, CefRefPtr<CefDownloadItemCallback>> download_callbacks_;
  std::map<uint32_t, int> runtime_download_ids_;
  std::unique_ptr<OtfStore> store_;

  struct PendingPopup {
    std::string url;
    std::string origin;
    int parent_tab_id = 0;
    int64_t expires_at = 0;
    bool open_as_popup = true;
    int popup_width = 600;
    int popup_height = 700;
    bool opener_private = false;
  };
  std::map<int, PendingPopup> pending_popups_;
  int next_pending_popup_id_ = 1;
  int pending_external_popups_ = 0;

  // URLs that are being opened in a new tab via middle-click or ctrl+click.
  // Populated in OnOpenURLFromTab, consumed (and erased) in OnBeforeBrowse so
  // the source tab does not navigate to the same URL.
  std::set<std::string> pending_new_tab_urls_;

  // Maps tab_id → original HTTP URL when OnBeforeBrowse upgrades http→https.
  // Used by OnLoadError to fall back to the insecure-blocked page when the
  // HTTPS version is unreachable.
  std::map<int, std::string> http_upgraded_urls_;

  // Transient allow-once set for downloads. Populated by the
  // permissions.download.allow RPC, consumed (and erased) by CanDownload so
  // the next download from that origin proceeds immediately.
  std::set<std::string> allow_once_downloads_;

  // Workspace state. active_workspace_id_ is the workspace whose tabs are
  // currently surfaced in the UI; new tabs join it. workspace_contexts_ is
  // populated lazily — the default workspace (id 1) uses the global
  // request context (nullptr sentinel), while subsequent workspaces get a
  // per-workspace CefRequestContext with its own cache_path.
  int active_workspace_id_ = 1;
  std::map<int, CefRefPtr<CefRequestContext>> workspace_contexts_;
  bool guest_session_active_ = false;
  int pre_guest_workspace_id_ = 1;
  int pre_guest_tab_id_ = -1;
  CefRefPtr<CefRequestContext> guest_context_;
  // Remembers the last active tab per workspace so switching back to a
  // workspace with live in-memory tabs restores the correct tab, not
  // just the first one that was created.
  std::map<int, int> workspace_last_active_tab_;
  std::map<int, SplitViewState> workspace_split_states_;
  // Set to true when startup_behavior is "newtab" so that the auto-opened
  // startup newtab tab cannot overwrite the saved workspace session in the DB.
  // Clears itself in PersistWorkspaceTabs once any live tab has a real URL.
  bool startup_session_guard_ = false;
  // Returns the request context for the active workspace. Returns nullptr
  // for the default workspace, which is the signal the BrowserView API
  // expects to mean "use the global context".
  CefRefPtr<CefRequestContext> GetActiveWorkspaceRequestContext();
  CefRefPtr<CefRequestContext> GetWorkspaceRequestContext(int workspace_id);
  CefRefPtr<CefRequestContext> GetGuestRequestContext();
  bool IsGuestSessionActive() const { return guest_session_active_; }
  bool IsGuestTab(int tab_id) const;
  void StartGuestSession();
  void EndGuestSession(bool restore_normal_tabs = true);
  // Shared in-memory (incognito) request context for private tabs. Created
  // lazily with an empty cache_path so nothing is persisted to disk, and
  // released once the last private tab closes so the ephemeral session is
  // wiped. All private tabs across workspaces share this single context.
  CefRefPtr<CefRequestContext> GetPrivateRequestContext();
  void MaybeReleasePrivateContext();
  void OpenAcceptedPopup(const PendingPopup& popup);
  CefRefPtr<CefRequestContext> private_context_;
  // Site-data inspection/clearing must act on the request context of the tab
  // whose data is being viewed — never blindly on the global profile.
  // Resolves which browser to run cookie/storage operations against: if the
  // requester is itself a content tab (the browser://sitedata page) use it;
  // otherwise (a popup overlay) fall back to the active content tab, which the
  // popup is overlaying. Lets a private tab inspect/clear only its ephemeral
  // session and keeps the global profile untouched.
  CefRefPtr<CefBrowser> ResolveSiteDataBrowser(CefRefPtr<CefBrowser> requester);
  // Snapshot the live state of every tab in a workspace and replace the
  // persisted workspace_tabs rows for it. Called from per-tab change hooks
  // (address/title/favicon) and lifecycle events (new/close/switch) so a
  // crash or quit never loses more than the most recent change.
  void PersistWorkspaceTabs(int workspace_id);
  void PersistWorkspaceForTab(int tab_id);
  void ApplyAlwaysOnPrivacyPreferences(CefRefPtr<CefRequestContext> ctx);
  void PersistWorkspaceSplitState(int workspace_id);
  void NotifySplitStateChanged(int workspace_id);
  void SyncSplitStateFromApp();
  bool IsSplitTab(int tab_id) const;
  bool IsSplitActive() const;

  void MarkTabJsDisabled(int tab_id) { js_disabled_tabs_.insert(tab_id); }
  void UnmarkTabJsDisabled(int tab_id) { js_disabled_tabs_.erase(tab_id); }
  bool IsTabJsDisabled(int tab_id) const {
    return js_disabled_tabs_.count(tab_id) > 0;
  }

  // Cross-origin resource tracking: page_origin -> set of external origins
  // accessed on that page. Populated on the IO thread in
  // GetResourceRequestHandler, read from the UI thread via cefQuery.
  std::map<std::string, std::set<std::string>> cross_origin_resources_;
  mutable std::mutex cross_origin_mutex_;

 private:
  const bool use_alloy_style_;
  CefRefPtr<CefMessageRouterBrowserSide> message_router_;

  typedef std::list<CefRefPtr<CefBrowser>> BrowserList;
  BrowserList browser_list_;
  bool is_closing_;
  bool memory_log_running_ = false;
  CefRefPtr<CefTaskManager> memory_task_manager_;

  std::set<int> js_disabled_tabs_;

  IMPLEMENT_REFCOUNTING(OtfHandler);
};

} // namespace otf

#endif // OTF_BROWSER_HANDLER_H_
