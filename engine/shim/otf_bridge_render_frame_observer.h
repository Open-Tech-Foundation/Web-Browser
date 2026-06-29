// Renderer-side endpoint of the JS <-> Rust bridge (plan.md §5, Phase 2b).
//
// One per RenderFrame. Installs window.otf.postMessage (JS -> browser) via gin,
// and implements otf::mojom::BridgeClient (browser -> JS) by dispatching each
// delivered wire string to window.__otfReceive. The bridge.js "otf" transport
// (ui/src/shared/bridge.js) drives both ends.

#ifndef OTF_ENGINE_SHIM_OTF_BRIDGE_RENDER_FRAME_OBSERVER_H_
#define OTF_ENGINE_SHIM_OTF_BRIDGE_RENDER_FRAME_OBSERVER_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/renderer/render_frame_observer.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "otf/shim/otf_bridge.mojom.h"

namespace otf {

class OtfBridgeRenderFrameObserver : public content::RenderFrameObserver,
                                     public mojom::BridgeClient {
 public:
  explicit OtfBridgeRenderFrameObserver(content::RenderFrame* render_frame);
  ~OtfBridgeRenderFrameObserver() override;

  OtfBridgeRenderFrameObserver(const OtfBridgeRenderFrameObserver&) = delete;
  OtfBridgeRenderFrameObserver& operator=(const OtfBridgeRenderFrameObserver&) =
      delete;

  // content::RenderFrameObserver:
  void DidClearWindowObject() override;
  void OnDestruct() override;

  // mojom::BridgeClient:
  void Deliver(const std::string& message_json) override;

 private:
  // Lazily binds the BridgeHost remote and registers our BridgeClient.
  void EnsureConnected();
  // window.otf.postMessage(json) -> browser.
  void OnPostMessage(const std::string& message_json);

  mojo::Remote<mojom::BridgeHost> host_;
  mojo::Receiver<mojom::BridgeClient> client_receiver_{this};

  base::WeakPtrFactory<OtfBridgeRenderFrameObserver> weak_factory_{this};
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_BRIDGE_RENDER_FRAME_OBSERVER_H_
