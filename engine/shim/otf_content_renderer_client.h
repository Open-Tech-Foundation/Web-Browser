// otf's ContentRendererClient: attaches the bridge RenderFrameObserver to every
// created frame. Stands alone on content::ContentRendererClient (no content_shell
// base) — the defaults are right for otf.

#ifndef OTF_ENGINE_SHIM_OTF_CONTENT_RENDERER_CLIENT_H_
#define OTF_ENGINE_SHIM_OTF_CONTENT_RENDERER_CLIENT_H_

#include "content/public/renderer/content_renderer_client.h"

namespace otf {

class OtfContentRendererClient : public content::ContentRendererClient {
 public:
  OtfContentRendererClient();
  ~OtfContentRendererClient() override;

  // content::ContentRendererClient:
  void RenderFrameCreated(content::RenderFrame* render_frame) override;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_CONTENT_RENDERER_CLIENT_H_
