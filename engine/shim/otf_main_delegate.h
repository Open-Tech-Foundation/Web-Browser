// otf's ContentMainDelegate: the embedder entry the content layer calls into.
// Stands alone on content::ContentMainDelegate (no content_shell base): it loads
// otf's resource pack, defaults the Ozone backend, and supplies otf's content /
// browser / renderer clients.

#ifndef OTF_ENGINE_SHIM_OTF_MAIN_DELEGATE_H_
#define OTF_ENGINE_SHIM_OTF_MAIN_DELEGATE_H_

#include <memory>
#include <optional>

#include "content/public/app/content_main_delegate.h"

namespace otf {

class OtfContentBrowserClient;
class OtfContentClient;
class OtfContentRendererClient;

class OtfMainDelegate : public content::ContentMainDelegate {
 public:
  OtfMainDelegate();

  OtfMainDelegate(const OtfMainDelegate&) = delete;
  OtfMainDelegate& operator=(const OtfMainDelegate&) = delete;

  ~OtfMainDelegate() override;

  // content::ContentMainDelegate:
  std::optional<int> BasicStartupComplete() override;
  void PreSandboxStartup() override;
  content::ContentClient* CreateContentClient() override;
  content::ContentBrowserClient* CreateContentBrowserClient() override;
  content::ContentRendererClient* CreateContentRendererClient() override;

 private:
  std::unique_ptr<OtfContentClient> content_client_;
  std::unique_ptr<OtfContentBrowserClient> browser_client_;
  std::unique_ptr<OtfContentRendererClient> renderer_client_;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_MAIN_DELEGATE_H_
