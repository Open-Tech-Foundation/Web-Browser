// otf's BrowserContext — the browser-side "profile" that owns storage,
// permissions and the network state for all WebContents (UI + tabs).
//
// Replaces content_shell's ShellBrowserContext as part of dropping the
// content_shell scaffold (plan.md foundational work). Deliberately minimal and
// *not* testonly: where ShellBrowserContext wires in content/test mocks
// (background sync, reduce-accept-language) or shell-only delegates, otf returns
// nullptr until it grows real implementations. Pages render with this surface;
// the privacy/network delegates land in later phases (cookies/network groups).

#ifndef OTF_ENGINE_SHIM_OTF_BROWSER_CONTEXT_H_
#define OTF_ENGINE_SHIM_OTF_BROWSER_CONTEXT_H_

#include <memory>

#include "base/files/file_path.h"
#include "content/public/browser/browser_context.h"

class SimpleFactoryKey;

namespace otf {

class OtfBrowserContext : public content::BrowserContext {
 public:
  explicit OtfBrowserContext(bool off_the_record);

  OtfBrowserContext(const OtfBrowserContext&) = delete;
  OtfBrowserContext& operator=(const OtfBrowserContext&) = delete;

  ~OtfBrowserContext() override;

  // content::BrowserContext:
  base::FilePath GetPath() const override;
  std::unique_ptr<content::ZoomLevelDelegate> CreateZoomLevelDelegate(
      const base::FilePath& partition_path) override;
  bool IsOffTheRecord() override;
  content::DownloadManagerDelegate* GetDownloadManagerDelegate() override;
  content::BrowserPluginGuestManager* GetGuestManager() override;
  storage::SpecialStoragePolicy* GetSpecialStoragePolicy() override;
  content::PlatformNotificationService* GetPlatformNotificationService()
      override;
  content::PushMessagingService* GetPushMessagingService() override;
  content::StorageNotificationService* GetStorageNotificationService() override;
  content::SSLHostStateDelegate* GetSSLHostStateDelegate() override;
  content::PermissionControllerDelegate* GetPermissionControllerDelegate()
      override;
  content::BackgroundFetchDelegate* GetBackgroundFetchDelegate() override;
  content::BackgroundSyncController* GetBackgroundSyncController() override;
  content::BrowsingDataRemoverDelegate* GetBrowsingDataRemoverDelegate()
      override;
  content::ContentIndexProvider* GetContentIndexProvider() override;
  content::ClientHintsControllerDelegate* GetClientHintsControllerDelegate()
      override;
  content::ReduceAcceptLanguageControllerDelegate*
  GetReduceAcceptLanguageControllerDelegate() override;
  content::OriginTrialsControllerDelegate* GetOriginTrialsControllerDelegate()
      override;

 private:
  const bool off_the_record_;
  base::FilePath path_;
  std::unique_ptr<SimpleFactoryKey> key_;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_BROWSER_CONTEXT_H_
