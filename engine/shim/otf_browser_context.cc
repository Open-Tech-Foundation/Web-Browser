#include "otf/shim/otf_browser_context.h"

#include <memory>
#include <utility>

#include "base/files/file_util.h"
#include "base/path_service.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/keyed_service/core/simple_dependency_manager.h"
#include "components/keyed_service/core/simple_factory_key.h"
#include "components/keyed_service/core/simple_key_map.h"

namespace otf {

namespace {

// User-data directory for the profile. Off-the-record contexts get an
// ephemeral subdir; the persistent one will move to a real per-user path once
// session persistence lands. For now both live under the system temp dir.
base::FilePath ResolveUserDataDir(bool off_the_record) {
  base::FilePath dir;
  CHECK(base::PathService::Get(base::DIR_TEMP, &dir));
  dir = dir.Append(FILE_PATH_LITERAL("otf-browser"));
  dir = dir.Append(off_the_record ? FILE_PATH_LITERAL("incognito")
                                  : FILE_PATH_LITERAL("default"));
  if (!base::PathExists(dir)) {
    base::CreateDirectory(dir);
  }
  return dir;
}

}  // namespace

OtfBrowserContext::OtfBrowserContext(bool off_the_record)
    : off_the_record_(off_the_record),
      path_(ResolveUserDataDir(off_the_record)) {
  key_ = std::make_unique<SimpleFactoryKey>(path_, off_the_record_);
  SimpleKeyMap::GetInstance()->Associate(this, key_.get());

  BrowserContextDependencyManager::GetInstance()->CreateBrowserContextServices(
      this);
}

OtfBrowserContext::~OtfBrowserContext() {
  NotifyWillBeDestroyed();

  // The SimpleDependencyManager must be passed after the
  // BrowserContextDependencyManager: keyed services in the latter's graph may
  // depend on ones in the former's.
  DependencyManager::PerformInterlockedTwoPhaseShutdown(
      BrowserContextDependencyManager::GetInstance(), this,
      SimpleDependencyManager::GetInstance(), key_.get());

  SimpleKeyMap::GetInstance()->Dissociate(this);

  ShutdownStoragePartitions();
}

base::FilePath OtfBrowserContext::GetPath() const {
  return path_;
}

std::unique_ptr<content::ZoomLevelDelegate>
OtfBrowserContext::CreateZoomLevelDelegate(const base::FilePath&) {
  return nullptr;
}

bool OtfBrowserContext::IsOffTheRecord() {
  return off_the_record_;
}

// otf does not yet provide these browser-side services; nullptr is a valid
// answer for a content embedder and pages still render. They fill in with the
// grouped privacy/network work in later phases.
content::DownloadManagerDelegate*
OtfBrowserContext::GetDownloadManagerDelegate() {
  return nullptr;
}

content::BrowserPluginGuestManager* OtfBrowserContext::GetGuestManager() {
  return nullptr;
}

storage::SpecialStoragePolicy* OtfBrowserContext::GetSpecialStoragePolicy() {
  return nullptr;
}

content::PlatformNotificationService*
OtfBrowserContext::GetPlatformNotificationService() {
  return nullptr;
}

content::PushMessagingService* OtfBrowserContext::GetPushMessagingService() {
  return nullptr;
}

content::StorageNotificationService*
OtfBrowserContext::GetStorageNotificationService() {
  return nullptr;
}

content::SSLHostStateDelegate* OtfBrowserContext::GetSSLHostStateDelegate() {
  return nullptr;
}

content::PermissionControllerDelegate*
OtfBrowserContext::GetPermissionControllerDelegate() {
  return nullptr;
}

content::BackgroundFetchDelegate*
OtfBrowserContext::GetBackgroundFetchDelegate() {
  return nullptr;
}

content::BackgroundSyncController*
OtfBrowserContext::GetBackgroundSyncController() {
  return nullptr;
}

content::BrowsingDataRemoverDelegate*
OtfBrowserContext::GetBrowsingDataRemoverDelegate() {
  return nullptr;
}

content::ContentIndexProvider* OtfBrowserContext::GetContentIndexProvider() {
  return nullptr;
}

content::ClientHintsControllerDelegate*
OtfBrowserContext::GetClientHintsControllerDelegate() {
  return nullptr;
}

content::ReduceAcceptLanguageControllerDelegate*
OtfBrowserContext::GetReduceAcceptLanguageControllerDelegate() {
  return nullptr;
}

content::OriginTrialsControllerDelegate*
OtfBrowserContext::GetOriginTrialsControllerDelegate() {
  return nullptr;
}

}  // namespace otf
