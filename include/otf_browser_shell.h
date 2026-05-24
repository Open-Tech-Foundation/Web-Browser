#ifndef OTF_BROWSER_SHELL_H_
#define OTF_BROWSER_SHELL_H_

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <deque>
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"

namespace otf {

constexpr int kUiBrowserViewId = 100;
constexpr int kFindBarBrowserViewId = 999;
constexpr int kZoomBarBrowserViewId = 1000;
constexpr int kDownloadsBrowserViewId = 1001;
constexpr int kAppMenuBrowserViewId = 1002;
constexpr int kCertificateBrowserViewId = 1003;
constexpr int kBookmarkBrowserViewId = 1004;
constexpr int kImagePreviewBrowserViewId = 1005;
constexpr int kClearSiteDataBrowserViewId = 1006;
constexpr int kWorkspaceBrowserViewId = 1007;
constexpr int kQrBrowserViewId = 1008;
constexpr int kBlockedPopupBrowserViewId = 1009;
constexpr int kDownloadRequestBrowserViewId = 1010;
constexpr int kLinkPreviewBrowserViewId = 1011;
constexpr int kConsoleBrowserViewId = 1012;

struct ConsoleEntry {
  int level;        // cef_log_severity_t value
  std::string message;
  std::string source;
  int line;
  int64_t timestamp_ms;
};

enum class ImagePreviewMode {
  kNone = 0,
  kInline = 1,
  kDedicated = 2,
};

// Core Tab Model for OTF Browser
struct BrowserTab {
  int id;
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<CefBrowserView> view;
  std::string url;
  std::string scheme_url;
  std::string title;
  bool is_loading;
};

// Tab Manager for OTF Browser
class TabManager {
 public:
  TabManager() : next_tab_id_(1) {}

  int AddTab(CefRefPtr<CefBrowserView> view, int parent_id = -1) {
    int id = next_tab_id_++;
    view_map_[id] = view;
    
    if (parent_id != -1) {
      auto it = std::find(tab_order_.begin(), tab_order_.end(), parent_id);
      if (it != tab_order_.end()) {
        tab_order_.insert(it + 1, id);
      } else {
        tab_order_.push_back(id);
      }
    } else {
      tab_order_.push_back(id);
    }
    return id;
  }

  std::vector<int> GetAllTabIds() const {
    return tab_order_;
  }

  void RemoveTab(int tab_id) {
    view_map_.erase(tab_id);
    browser_map_.erase(tab_id);
    scheme_map_.erase(tab_id);
    url_map_.erase(tab_id);
    title_map_.erase(tab_id);
    find_text_map_.erase(tab_id);
    find_case_map_.erase(tab_id);
    history_suppressed_url_map_.erase(tab_id);
    workspace_map_.erase(tab_id);
    image_preview_width_map_.erase(tab_id);
    image_preview_height_map_.erase(tab_id);
    image_preview_info_visible_map_.erase(tab_id);
    image_preview_mode_map_.erase(tab_id);
    muted_map_.erase(tab_id);
    console_visible_map_.erase(tab_id);
    console_log_map_.erase(tab_id);
    
    auto it = std::find(tab_order_.begin(), tab_order_.end(), tab_id);
    if (it != tab_order_.end()) {
      tab_order_.erase(it);
    }
  }

  void SetUrl(int tab_id, const std::string& url) {
    url_map_[tab_id] = url;
  }

  std::string GetUrl(int tab_id) {
    auto it = url_map_.find(tab_id);
    if (it != url_map_.end()) return it->second;
    return "";
  }

  void SetTitle(int tab_id, const std::string& title) {
    title_map_[tab_id] = title;
  }

  std::string GetTitle(int tab_id) {
    auto it = title_map_.find(tab_id);
    if (it != title_map_.end()) return it->second;
    return "New Tab";
  }

  CefRefPtr<CefBrowserView> GetView(int tab_id) {
    auto it = view_map_.find(tab_id);
    if (it != view_map_.end()) return it->second;
    return nullptr;
  }

  void SetMuted(int tab_id, bool muted) {
    muted_map_[tab_id] = muted;
  }

  bool GetMuted(int tab_id) {
    auto it = muted_map_.find(tab_id);
    if (it != muted_map_.end()) return it->second;
    return false;
  }

  void SetConsoleVisible(int tab_id, bool visible) {
    console_visible_map_[tab_id] = visible;
  }

  bool IsConsoleVisible(int tab_id) const {
    auto it = console_visible_map_.find(tab_id);
    return it != console_visible_map_.end() ? it->second : false;
  }

  void SetBrowser(int tab_id, CefRefPtr<CefBrowser> browser) {
    browser_map_[tab_id] = browser;
  }

  void SetSchemeUrl(int tab_id, const std::string& url) {
    scheme_map_[tab_id] = url;
  }

  std::string GetSchemeUrl(int tab_id) {
    auto it = scheme_map_.find(tab_id);
    if (it != scheme_map_.end()) return it->second;
    return "";
  }

  CefRefPtr<CefBrowser> GetBrowser(int tab_id) {
    auto it = browser_map_.find(tab_id);
    if (it != browser_map_.end()) return it->second;
    return nullptr;
  }

  int GetId(CefRefPtr<CefBrowser> browser) {
    if (!browser) return -1;
    for (auto const& [id, b] : browser_map_) {
      if (b && b->IsSame(browser)) return id;
    }
    return -1;
  }

  // ── Find state per tab ──
  void SetFindText(int tab_id, const std::string& text) {
    find_text_map_[tab_id] = text;
  }

  std::string GetFindText(int tab_id) {
    auto it = find_text_map_.find(tab_id);
    if (it != find_text_map_.end()) return it->second;
    return "";
  }

  void SetFindCase(int tab_id, bool match_case) {
    find_case_map_[tab_id] = match_case;
  }

  bool GetFindCase(int tab_id) {
    auto it = find_case_map_.find(tab_id);
    if (it != find_case_map_.end()) return it->second;
    return false;
  }

  void ClearFindState(int tab_id) {
    find_text_map_.erase(tab_id);
    find_case_map_.erase(tab_id);
    find_count_map_.erase(tab_id);
    find_active_map_.erase(tab_id);
    find_visible_map_.erase(tab_id);
  }

  void SetFindVisible(int tab_id, bool visible) {
    find_visible_map_[tab_id] = visible;
  }

  bool IsFindVisible(int tab_id) {
    auto it = find_visible_map_.find(tab_id);
    if (it != find_visible_map_.end()) return it->second;
    return false;
  }

  void SetFindCount(int tab_id, int count) {
    find_count_map_[tab_id] = count;
  }

  int GetFindCount(int tab_id) {
    auto it = find_count_map_.find(tab_id);
    if (it != find_count_map_.end()) return it->second;
    return 0;
  }

  void SetFindActive(int tab_id, int active) {
    find_active_map_[tab_id] = active;
  }

  int GetFindActive(int tab_id) {
    auto it = find_active_map_.find(tab_id);
    if (it != find_active_map_.end()) return it->second;
    return 0;
  }

  void SetZoomPercent(int tab_id, int zoom_percent) {
    zoom_percent_map_[tab_id] = zoom_percent;
  }

  int GetZoomPercent(int tab_id) {
    auto it = zoom_percent_map_.find(tab_id);
    if (it != zoom_percent_map_.end()) return it->second;
    return 100;
  }

  void SetFaviconUrl(int tab_id, const std::string& url) {
    favicon_url_map_[tab_id] = url;
  }

  std::string GetFaviconUrl(int tab_id) {
    auto it = favicon_url_map_.find(tab_id);
    if (it != favicon_url_map_.end()) return it->second;
    return "";
  }

  void SetSslError(int tab_id, bool has_error) {
    ssl_error_map_[tab_id] = has_error;
    if (!has_error) {
      ssl_error_url_map_.erase(tab_id);
    }
  }

  bool HasSslError(int tab_id) {
    auto it = ssl_error_map_.find(tab_id);
    if (it != ssl_error_map_.end()) return it->second;
    return false;
  }

  void SetSslErrorUrl(int tab_id, const std::string& url) {
    ssl_error_url_map_[tab_id] = url;
  }

  std::string GetSslErrorUrl(int tab_id) {
    auto it = ssl_error_url_map_.find(tab_id);
    if (it != ssl_error_url_map_.end()) return it->second;
    return "";
  }

  void SetHistorySuppressedUrl(int tab_id, const std::string& url) {
    if (url.empty()) {
      history_suppressed_url_map_.erase(tab_id);
    } else {
      history_suppressed_url_map_[tab_id] = url;
    }
  }

  std::string GetHistorySuppressedUrl(int tab_id) {
    auto it = history_suppressed_url_map_.find(tab_id);
    if (it != history_suppressed_url_map_.end()) return it->second;
    return "";
  }

  void SetWorkspaceId(int tab_id, int workspace_id) {
    workspace_map_[tab_id] = workspace_id;
  }

  int GetWorkspaceId(int tab_id) const {
    auto it = workspace_map_.find(tab_id);
    if (it != workspace_map_.end()) return it->second;
    return 0;
  }

  void SetImagePreviewDimensions(int tab_id, int width, int height) {
    image_preview_width_map_[tab_id] = width < 0 ? 0 : width;
    image_preview_height_map_[tab_id] = height < 0 ? 0 : height;
  }

  int GetImagePreviewWidth(int tab_id) const {
    auto it = image_preview_width_map_.find(tab_id);
    return it != image_preview_width_map_.end() ? it->second : 0;
  }

  int GetImagePreviewHeight(int tab_id) const {
    auto it = image_preview_height_map_.find(tab_id);
    return it != image_preview_height_map_.end() ? it->second : 0;
  }

  void SetImagePreviewInfoVisible(int tab_id, bool visible) {
    image_preview_info_visible_map_[tab_id] = visible;
  }

  bool IsImagePreviewInfoVisible(int tab_id) const {
    auto it = image_preview_info_visible_map_.find(tab_id);
    return it != image_preview_info_visible_map_.end() ? it->second : true;
  }

  void SetImagePreviewMode(int tab_id, ImagePreviewMode mode) {
    if (mode == ImagePreviewMode::kNone) {
      image_preview_mode_map_.erase(tab_id);
    } else {
      image_preview_mode_map_[tab_id] = mode;
    }
  }

  ImagePreviewMode GetImagePreviewMode(int tab_id) const {
    auto it = image_preview_mode_map_.find(tab_id);
    return it != image_preview_mode_map_.end() ? it->second
                                               : ImagePreviewMode::kNone;
  }

  std::vector<int> GetTabIdsForWorkspace(int workspace_id) const {
    std::vector<int> out;
    for (int id : tab_order_) {
      auto it = workspace_map_.find(id);
      const int ws = (it != workspace_map_.end()) ? it->second : 0;
      if (ws == workspace_id) out.push_back(id);
    }
    return out;
  }

  // Replace the relative ordering of workspace tabs inside tab_order_ with
  // ordered_ids (which must be exactly the current workspace tab set).
  void SetWorkspaceTabOrder(int workspace_id,
                            const std::vector<int>& ordered_ids) {
    // Build a position map for the new order.
    std::unordered_map<int, size_t> pos;
    for (size_t i = 0; i < ordered_ids.size(); ++i) pos[ordered_ids[i]] = i;

    // Stable-sort tab_order_: workspace tabs move to their new relative
    // positions; non-workspace tabs are unaffected and keep their slots.
    std::vector<int> ws_slots;   // workspace tab IDs in current tab_order_ order
    std::vector<size_t> ws_idx;  // their indices in tab_order_
    for (size_t i = 0; i < tab_order_.size(); ++i) {
      int id = tab_order_[i];
      auto it = workspace_map_.find(id);
      const int ws = (it != workspace_map_.end()) ? it->second : 0;
      if (ws == workspace_id) {
        ws_slots.push_back(id);
        ws_idx.push_back(i);
      }
    }
    // Sort the workspace slots by the desired order.
    std::sort(ws_slots.begin(), ws_slots.end(),
              [&pos](int a, int b) {
                const auto ait = pos.find(a);
                const auto bit = pos.find(b);
                if (ait == pos.end() && bit == pos.end()) return false;
                if (ait == pos.end()) return false;
                if (bit == pos.end()) return true;
                return ait->second < bit->second;
              });
    // Write sorted IDs back into the same indices in tab_order_.
    for (size_t k = 0; k < ws_idx.size(); ++k) {
      tab_order_[ws_idx[k]] = ws_slots[k];
    }
  }

 private:
  std::map<int, CefRefPtr<CefBrowserView>> view_map_;
  std::map<int, CefRefPtr<CefBrowser>> browser_map_;
  std::map<int, std::string> scheme_map_;
  std::map<int, std::string> url_map_;
  std::map<int, std::string> title_map_;
  std::map<int, std::string> find_text_map_;
  std::map<int, bool> find_case_map_;
  std::map<int, int> find_count_map_;
  std::map<int, int> find_active_map_;
  std::map<int, bool> find_visible_map_;
  std::map<int, int> zoom_percent_map_;
  std::map<int, std::string> favicon_url_map_;
  std::map<int, bool> ssl_error_map_;
  std::map<int, std::string> ssl_error_url_map_;
  std::map<int, std::string> history_suppressed_url_map_;
  std::map<int, int> workspace_map_;
  std::map<int, int> image_preview_width_map_;
  std::map<int, int> image_preview_height_map_;
  std::map<int, bool> image_preview_info_visible_map_;
  std::map<int, ImagePreviewMode> image_preview_mode_map_;
  std::map<int, bool> muted_map_;
  std::map<int, bool> console_visible_map_;
  std::map<int, std::deque<ConsoleEntry>> console_log_map_;
  std::vector<int> tab_order_;
  int next_tab_id_;

 public:
  static constexpr size_t kMaxConsoleEntries = 500;

  void AddConsoleEntry(int tab_id, ConsoleEntry entry) {
    auto& log = console_log_map_[tab_id];
    if (log.size() >= kMaxConsoleEntries) log.pop_front();
    log.push_back(std::move(entry));
  }

  const std::deque<ConsoleEntry>& GetConsoleLogs(int tab_id) const {
    static const std::deque<ConsoleEntry> kEmpty;
    auto it = console_log_map_.find(tab_id);
    return it != console_log_map_.end() ? it->second : kEmpty;
  }

  void ClearConsoleLogs(int tab_id) {
    console_log_map_.erase(tab_id);
  }
};

} // namespace otf

#endif // OTF_BROWSER_SHELL_H_
