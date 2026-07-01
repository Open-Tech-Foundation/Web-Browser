// Owns otf's BrowserContexts and the on-disk layout under the user-data root.
//
// Data isolation model (see plan): each workspace is its own profile-like
// context rooted at `<root>/workspaces/<id>/`, so cookies/cache/site storage
// never collide across workspaces. The UI shell and overlays use a separate
// persistent `system` context (`<root>/system/`) that holds no web data. Phase 1
// introduces the manager + system context; page tabs still use the system
// context until Phase 2 routes them to their workspace's context.

#ifndef OTF_ENGINE_SHIM_OTF_BROWSER_CONTEXT_MANAGER_H_
#define OTF_ENGINE_SHIM_OTF_BROWSER_CONTEXT_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include "base/files/file_path.h"

namespace content {
class BrowserContext;
}

namespace otf {

class OtfBrowserContext;

class OtfBrowserContextManager {
 public:
  OtfBrowserContextManager();
  ~OtfBrowserContextManager();

  OtfBrowserContextManager(const OtfBrowserContextManager&) = delete;
  OtfBrowserContextManager& operator=(const OtfBrowserContextManager&) = delete;

  // The process instance, or nullptr before OtfBrowserMainParts creates it /
  // after teardown. Used by the shim (tab host, overlays, devtools) to reach a
  // context without threading it through every call.
  static OtfBrowserContextManager* Get();

  // The persistent context for the UI shell + overlays (browser:// chrome).
  content::BrowserContext* System();

  // The isolated context for a workspace, rooted at `<root>/workspaces/<id>/`.
  // Lazily created and cached; `id` is the workspace id string (UUID-ready).
  content::BrowserContext* ForWorkspace(const std::string& id);

  // The workspace's in-memory (off-the-record) context, shared by all of its
  // private tabs and isolated from its persistent data. Keyed by workspace id.
  content::BrowserContext* ForIncognito(const std::string& id);

  // Delete a workspace's data: destroy its context now (its tabs are already
  // closed) and mark its directory for wipe. The directory is fully removed on
  // the next launch (WipeMarkedWorkspaces), matching "async, wiped on restart".
  void ReleaseWorkspace(const std::string& id);

  // The user-data root (`<root>/system`, `<root>/workspaces/<id>`,
  // `<root>/downloads`). Resolved once at construction.
  const base::FilePath& Root() const { return root_; }

 private:
  base::FilePath root_;
  std::unique_ptr<OtfBrowserContext> system_;
  std::map<std::string, std::unique_ptr<OtfBrowserContext>> workspaces_;
  // Per-workspace in-memory context for that workspace's private tabs.
  std::map<std::string, std::unique_ptr<OtfBrowserContext>> incognito_;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_BROWSER_CONTEXT_MANAGER_H_
