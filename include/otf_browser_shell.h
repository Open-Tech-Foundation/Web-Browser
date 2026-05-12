#ifndef OTF_BROWSER_SHELL_H_
#define OTF_BROWSER_SHELL_H_

#include <string>
#include <vector>
#include <map>
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"

namespace otf {

constexpr int kUiBrowserViewId = 100;
constexpr int kFindBarBrowserViewId = 999;

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

  int AddTab(CefRefPtr<CefBrowserView> view) {
    int id = next_tab_id_++;
    view_map_[id] = view;
    return id;
  }

  std::vector<int> GetAllTabIds() const {
    std::vector<int> ids;
    for (auto const& [id, view] : view_map_) {
      ids.push_back(id);
    }
    return ids;
  }

  void RemoveTab(int tab_id) {
    view_map_.erase(tab_id);
    browser_map_.erase(tab_id);
    scheme_map_.erase(tab_id);
    url_map_.erase(tab_id);
    title_map_.erase(tab_id);
    find_text_map_.erase(tab_id);
    find_case_map_.erase(tab_id);
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
  int next_tab_id_;
};

} // namespace otf

#endif // OTF_BROWSER_SHELL_H_
