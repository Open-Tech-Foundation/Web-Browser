#ifndef OTF_BROWSER_SHELL_H_
#define OTF_BROWSER_SHELL_H_

#include <string>
#include <vector>
#include <map>
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"

namespace otf {

// Core Tab Model for OTF Browser
struct BrowserTab {
  int id;
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<CefBrowserView> view;
  std::string url;
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
  }

  CefRefPtr<CefBrowserView> GetView(int tab_id) {
    auto it = view_map_.find(tab_id);
    if (it != view_map_.end()) return it->second;
    return nullptr;
  }

  void SetBrowser(int tab_id, CefRefPtr<CefBrowser> browser) {
    browser_map_[tab_id] = browser;
  }

  CefRefPtr<CefBrowser> GetBrowser(int tab_id) {
    auto it = browser_map_.find(tab_id);
    if (it != browser_map_.end()) return it->second;
    return nullptr;
  }

 private:
  std::map<int, CefRefPtr<CefBrowserView>> view_map_;
  std::map<int, CefRefPtr<CefBrowser>> browser_map_;
  int next_tab_id_;
};

} // namespace otf

#endif // OTF_BROWSER_SHELL_H_
