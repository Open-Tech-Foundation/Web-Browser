#include "otf/shim/otf_bridge_host.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"

namespace otf {

// static
OtfBridgeHost& OtfBridgeHost::Get() {
  static base::NoDestructor<OtfBridgeHost> instance;
  return *instance;
}

OtfBridgeHost::OtfBridgeHost() {
  receivers_.set_disconnect_handler(base::BindRepeating(
      &OtfBridgeHost::OnReceiverDisconnect, base::Unretained(this)));
}

OtfBridgeHost::~OtfBridgeHost() = default;

void OtfBridgeHost::SetCallbacks(OtfCallbacks callbacks) {
  callbacks_ = callbacks;
}

void OtfBridgeHost::BindReceiver(
    content::RenderFrameHost* /*render_frame_host*/,
    mojo::PendingReceiver<mojom::BridgeHost> receiver) {
  // Trust is enforced upstream at bind registration (OtfContentBrowserClient's
  // binder checks IsTrustedBridgeFrame), so only trusted UI frames reach here.
  const uint64_t client_id = next_client_id_++;
  receivers_.Add(this, std::move(receiver), client_id);
}

void OtfBridgeHost::Connect(mojo::PendingRemote<mojom::BridgeClient> client) {
  const uint64_t client_id = receivers_.current_context();
  clients_[client_id].reset();
  clients_[client_id].Bind(std::move(client));
}

void OtfBridgeHost::Call(const std::string& request_json) {
  const uint64_t client_id = receivers_.current_context();
  const uint64_t reply_id = next_reply_id_++;
  reply_to_client_[reply_id] = client_id;
  if (callbacks_.on_js_call) {
    callbacks_.on_js_call(callbacks_.user_data, reply_id, request_json.c_str());
  }
  // The Rust backend may answer synchronously inside on_js_call (reentrant
  // Respond) or later; either way Respond() resolves reply_to_client_.
}

void OtfBridgeHost::Respond(uint64_t reply_id,
                            const std::string& response_json) {
  auto it = reply_to_client_.find(reply_id);
  if (it == reply_to_client_.end()) {
    return;
  }
  const uint64_t client_id = it->second;
  reply_to_client_.erase(it);
  auto client = clients_.find(client_id);
  if (client != clients_.end() && client->second.is_bound()) {
    client->second->Deliver(response_json);
  }
}

void OtfBridgeHost::Emit(const std::string& event_json) {
  // target==0 in the FFI means "the UI surface"; broadcast to every connected
  // client and let each surface's subscription filter by event key.
  for (auto& [client_id, client] : clients_) {
    if (client.is_bound()) {
      client->Deliver(event_json);
    }
  }
}

void OtfBridgeHost::OnReceiverDisconnect() {
  const uint64_t client_id = receivers_.current_context();
  clients_.erase(client_id);
}

}  // namespace otf
