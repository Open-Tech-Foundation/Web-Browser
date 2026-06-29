#include "otf/shim/otf_content_renderer_client.h"

#include "otf/shim/otf_bridge_render_frame_observer.h"

namespace otf {

OtfContentRendererClient::OtfContentRendererClient() = default;
OtfContentRendererClient::~OtfContentRendererClient() = default;

void OtfContentRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  // Self-owned: deletes itself in OnDestruct() when the frame goes away.
  new OtfBridgeRenderFrameObserver(render_frame);
}

}  // namespace otf
