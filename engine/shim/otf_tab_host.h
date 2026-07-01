// Browser-side implementation of the Tabs interface (bridge.h OtfTabsApi).
//
// Owns the content::WebContents for each caller-assigned tab id; the active one
// is shown through OtfPlatformWindow (the cross-OS window seam), so this class
// holds no toolkit types. Content events (title/url/load) flow back to the Rust
// backend via the OtfCallbacks observer, keyed by the same tab id the caller
// assigned.

#ifndef OTF_ENGINE_SHIM_OTF_TAB_HOST_H_
#define OTF_ENGINE_SHIM_OTF_TAB_HOST_H_

#include <map>
#include <memory>
#include <string>

#include "otf/shim/bridge.h"

namespace base {
template <typename T>
class NoDestructor;
}
namespace content {
class WebContents;
}

namespace otf {

class OtfTabObserver;
class OtfTabWebContentsDelegate;

class OtfTabHost {
 public:
  static OtfTabHost& Get();

  OtfTabHost(const OtfTabHost&) = delete;
  OtfTabHost& operator=(const OtfTabHost&) = delete;

  void SetCallbacks(OtfCallbacks callbacks);

  // Destroy all tab WebContents; called at shutdown before the browser contexts
  // they belong to are torn down (WebContents must not outlive their context).
  void Shutdown();

  // OtfTabsApi ops (caller-assigned ids).
  // Bind a tab to its workspace before its WebContents is created, so the tab
  // uses that workspace's isolated storage context.
  OtfStatus SetWorkspace(OtfTabHandle id, const std::string& workspace_id);
  OtfStatus Create(OtfTabHandle id, const std::string& url);
  OtfStatus Navigate(OtfTabHandle id, const std::string& url);
  OtfStatus Show(OtfTabHandle id);
  OtfStatus Hide(OtfTabHandle id);
  OtfStatus Close(OtfTabHandle id);
  OtfStatus Reload(OtfTabHandle id);
  OtfStatus Stop(OtfTabHandle id);
  OtfStatus GoBack(OtfTabHandle id);
  OtfStatus GoForward(OtfTabHandle id);

  // The "hole" the active tab is sized into (else derived from the UI window).
  void SetContentBounds(int32_t x, int32_t y, int32_t w, int32_t h);

  // Called by the per-tab observer; forwarded to the Rust observer table.
  void NotifyTitle(OtfTabHandle id, const std::string& title);
  void NotifyUrl(OtfTabHandle id, const std::string& url);
  void NotifyLoad(OtfTabHandle id, bool loading);
  // Called by the per-tab delegate on a page right-click.
  void NotifyContextMenu(OtfTabHandle id, const std::string& params_json);

  // Run a context-menu action on the tab's page (see OtfTabsApi::context_action).
  OtfStatus ContextAction(OtfTabHandle id, const std::string& action, int x, int y);

 private:
  friend class base::NoDestructor<OtfTabHost>;

  struct TabEntry {
    std::unique_ptr<content::WebContents> contents;
    std::unique_ptr<OtfTabObserver> observer;
    std::unique_ptr<OtfTabWebContentsDelegate> delegate;
    // For an internal page: the `browser://…` URL the tab shows (what the URL bar
    // should display) and the real `<ui-base>/<page>.html` URL we actually load.
    // Empty for normal web pages.
    std::string internal_url;
    std::string internal_target;
  };

  OtfTabHost();
  ~OtfTabHost();

  content::WebContents* Find(OtfTabHandle id);
  content::WebContents* EnsureContents(OtfTabHandle id);

  OtfCallbacks callbacks_ = {};
  std::map<OtfTabHandle, TabEntry> tabs_;
  // Tab -> workspace id, set before the WebContents is created.
  std::map<OtfTabHandle, std::string> tab_workspace_;
  OtfTabHandle active_ = 0;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_TAB_HOST_H_
