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

class OtfTabHost {
 public:
  static OtfTabHost& Get();

  OtfTabHost(const OtfTabHost&) = delete;
  OtfTabHost& operator=(const OtfTabHost&) = delete;

  void SetCallbacks(OtfCallbacks callbacks);

  // OtfTabsApi ops (caller-assigned ids).
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

 private:
  friend class base::NoDestructor<OtfTabHost>;

  struct TabEntry {
    std::unique_ptr<content::WebContents> contents;
    std::unique_ptr<OtfTabObserver> observer;
  };

  OtfTabHost();
  ~OtfTabHost();

  content::WebContents* Find(OtfTabHandle id);
  content::WebContents* EnsureContents(OtfTabHandle id);

  OtfCallbacks callbacks_ = {};
  std::map<OtfTabHandle, TabEntry> tabs_;
  OtfTabHandle active_ = 0;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_TAB_HOST_H_
