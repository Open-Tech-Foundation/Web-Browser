// Browser-side endpoint of the JS <-> Rust bridge (plan.md §5, Phase 2b).
//
// A process-wide singleton implementing otf::mojom::BridgeHost. Each UI/tab
// renderer frame binds a receiver (via OtfContentBrowserClient) and registers a
// BridgeClient remote. Inbound JS calls are marshaled to the Rust backend
// through the C callback table (OtfCallbacks.on_js_call); the Rust backend
// answers later via otf_bridge_respond / otf_bridge_emit, which fan back out to
// the right renderer over the stored BridgeClient remotes.

#ifndef OTF_ENGINE_SHIM_OTF_BRIDGE_HOST_H_
#define OTF_ENGINE_SHIM_OTF_BRIDGE_HOST_H_

#include <cstdint>
#include <map>
#include <string>

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "otf/shim/bridge.h"
#include "otf/shim/otf_bridge.mojom.h"

namespace base {
template <typename T>
class NoDestructor;
}

namespace content {
class RenderFrameHost;
}

namespace otf {

class OtfBridgeHost : public mojom::BridgeHost {
 public:
  static OtfBridgeHost& Get();

  OtfBridgeHost(const OtfBridgeHost&) = delete;
  OtfBridgeHost& operator=(const OtfBridgeHost&) = delete;

  // Wires the Rust callback table (set once at otf_browser_init).
  void SetCallbacks(OtfCallbacks callbacks);

  // Binds a new BridgeHost receiver for a renderer frame.
  void BindReceiver(content::RenderFrameHost* render_frame_host,
                    mojo::PendingReceiver<mojom::BridgeHost> receiver);

  // Rust -> JS: reply to a specific JS call (routed to the originating frame).
  void Respond(uint64_t reply_id, const std::string& response_json);
  // Rust -> JS: push an event to all connected UI surfaces.
  void Emit(const std::string& event_json);

  // mojom::BridgeHost:
  void Connect(mojo::PendingRemote<mojom::BridgeClient> client) override;
  void Call(const std::string& request_json) override;

 private:
  friend class base::NoDestructor<OtfBridgeHost>;

  OtfBridgeHost();
  ~OtfBridgeHost() override;

  void OnReceiverDisconnect();

  OtfCallbacks callbacks_ = {};
  // Keyed by a per-frame client id carried as the receiver's context.
  mojo::ReceiverSet<mojom::BridgeHost, uint64_t> receivers_;
  std::map<uint64_t, mojo::Remote<mojom::BridgeClient>> clients_;
  std::map<uint64_t, uint64_t> reply_to_client_;
  uint64_t next_client_id_ = 1;
  uint64_t next_reply_id_ = 1;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_BRIDGE_HOST_H_
