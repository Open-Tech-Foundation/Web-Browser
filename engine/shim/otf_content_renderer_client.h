// otf's ContentRendererClient: reuses content_shell's renderer client and
// attaches the bridge RenderFrameObserver to every created frame.

#ifndef OTF_ENGINE_SHIM_OTF_CONTENT_RENDERER_CLIENT_H_
#define OTF_ENGINE_SHIM_OTF_CONTENT_RENDERER_CLIENT_H_

#include "content/shell/renderer/shell_content_renderer_client.h"

namespace otf {

class OtfContentRendererClient : public content::ShellContentRendererClient {
 public:
  OtfContentRendererClient();
  ~OtfContentRendererClient() override;

  // content::ContentRendererClient:
  void RenderFrameCreated(content::RenderFrame* render_frame) override;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_CONTENT_RENDERER_CLIENT_H_
