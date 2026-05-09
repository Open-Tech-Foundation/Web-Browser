#ifndef OTF_BROWSER_SHELL_H_
#define OTF_BROWSER_SHELL_H_

#include <string>
#include <vector>
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
  TabManager() : next_tab_id_(1), active_tab_index_(-1) {}

  int AddTab(CefRefPtr<CefBrowserView> view) {
    BrowserTab tab;
    tab.id = next_tab_id_++;
    tab.view = view;
    tab.is_loading = false;
    tabs_.push_back(tab);
    
    if (active_tab_index_ == -1) {
      active_tab_index_ = 0;
    }
    return tab.id;
  }

  void SetBrowser(int tab_id, CefRefPtr<CefBrowser> browser) {
    for (auto& tab : tabs_) {
      if (tab.id == tab_id) {
        tab.browser = browser;
        break;
      }
    }
  }

  BrowserTab* GetActiveTab() {
    if (active_tab_index_ >= 0 && (size_t)active_tab_index_ < tabs_.size()) {
      return &tabs_[active_tab_index_];
    }
    return nullptr;
  }

 private:
  std::vector<BrowserTab> tabs_;
  int next_tab_id_;
  int active_tab_index_;
};

} // namespace otf

#endif // OTF_BROWSER_SHELL_H_
