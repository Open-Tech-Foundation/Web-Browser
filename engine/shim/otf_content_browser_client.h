// otf's ContentBrowserClient: stands alone on content::ContentBrowserClient (no
// content_shell base). It builds otf's BrowserMainParts and adds the per-frame
// binder for the JS<->Rust bridge interface (otf::mojom::BridgeHost); the rest of
// the surface uses content's defaults until otf needs to customize it.

#ifndef OTF_ENGINE_SHIM_OTF_CONTENT_BROWSER_CLIENT_H_
#define OTF_ENGINE_SHIM_OTF_CONTENT_BROWSER_CLIENT_H_

#include <memory>
#include <string>

#include "content/public/browser/content_browser_client.h"

namespace content {
class BrowserMainParts;
class RenderFrameHost;
}

namespace otf {

class OtfContentBrowserClient : public content::ContentBrowserClient {
 public:
  OtfContentBrowserClient();

  OtfContentBrowserClient(const OtfContentBrowserClient&) = delete;
  OtfContentBrowserClient& operator=(const OtfContentBrowserClient&) = delete;

  ~OtfContentBrowserClient() override;

  // content::ContentBrowserClient:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  void RegisterBrowserInterfaceBindersForFrame(
      content::RenderFrameHost* render_frame_host,
      mojo::BinderMapWithContext<content::RenderFrameHost*>* map) override;
  std::unique_ptr<content::DevToolsManagerDelegate>
  CreateDevToolsManagerDelegate() override;
  std::string GetProduct() override;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_CONTENT_BROWSER_CLIENT_H_
