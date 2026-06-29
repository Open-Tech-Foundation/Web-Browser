// otf's ContentBrowserClient: reuses content_shell's browser client and adds the
// per-frame binder for the JS<->Rust bridge interface (otf::mojom::BridgeHost).

#ifndef OTF_ENGINE_SHIM_OTF_CONTENT_BROWSER_CLIENT_H_
#define OTF_ENGINE_SHIM_OTF_CONTENT_BROWSER_CLIENT_H_

#include <memory>

#include "content/shell/browser/shell_content_browser_client.h"

namespace content {
class BrowserMainParts;
}

namespace otf {

class OtfContentBrowserClient : public content::ShellContentBrowserClient {
 public:
  OtfContentBrowserClient();
  ~OtfContentBrowserClient() override;

  // content::ContentBrowserClient:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  void RegisterBrowserInterfaceBindersForFrame(
      content::RenderFrameHost* render_frame_host,
      mojo::BinderMapWithContext<content::RenderFrameHost*>* map) override;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_CONTENT_BROWSER_CLIENT_H_
