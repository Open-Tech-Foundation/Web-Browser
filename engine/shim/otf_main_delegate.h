// otf's ContentMainDelegate: reuses content_shell's ShellMainDelegate (so the
// content layer boots with a working embedder) but swaps in otf's browser and
// renderer clients so the JS<->Rust bridge interface is wired into every frame.

#ifndef OTF_ENGINE_SHIM_OTF_MAIN_DELEGATE_H_
#define OTF_ENGINE_SHIM_OTF_MAIN_DELEGATE_H_

#include <optional>

#include "content/shell/app/shell_main_delegate.h"

namespace otf {

class OtfMainDelegate : public content::ShellMainDelegate {
 public:
  OtfMainDelegate();
  ~OtfMainDelegate() override;

  // content::ContentMainDelegate:
  std::optional<int> BasicStartupComplete() override;
  content::ContentBrowserClient* CreateContentBrowserClient() override;
  content::ContentRendererClient* CreateContentRendererClient() override;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_MAIN_DELEGATE_H_
