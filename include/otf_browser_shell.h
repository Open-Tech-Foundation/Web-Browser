#ifndef OTF_BROWSER_SHELL_H_
#define OTF_BROWSER_SHELL_H_

#include <string>
#include <vector>
#include <map>
#include <algorithm>
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
  std::vector<int> tab_order_;
  int next_tab_id_;
};

} // namespace otf

#endif // OTF_BROWSER_SHELL_H_
