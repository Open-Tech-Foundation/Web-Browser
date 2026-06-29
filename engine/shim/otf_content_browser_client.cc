#include "otf/shim/otf_content_browser_client.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/browser_main_parts.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "otf/shim/otf_browser_main_parts.h"
#include "otf/shim/otf_bridge.mojom.h"
#include "otf/shim/otf_bridge_host.h"

namespace otf {

OtfContentBrowserClient::OtfContentBrowserClient() = default;
OtfContentBrowserClient::~OtfContentBrowserClient() = default;

std::unique_ptr<content::BrowserMainParts>
OtfContentBrowserClient::CreateBrowserMainParts(bool /*is_integration_test*/) {
  auto parts = std::make_unique<OtfBrowserMainParts>();
  // Register with the Shell base so its browser_context() and other hooks keep
  // resolving (this overrides the base's own CreateBrowserMainParts).
  set_browser_main_parts(parts.get());
  return parts;
}

void OtfContentBrowserClient::RegisterBrowserInterfaceBindersForFrame(
    content::RenderFrameHost* render_frame_host,
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map) {
  ShellContentBrowserClient::RegisterBrowserInterfaceBindersForFrame(
      render_frame_host, map);
  map->Add<mojom::BridgeHost>(base::BindRepeating(
      [](content::RenderFrameHost* rfh,
         mojo::PendingReceiver<mojom::BridgeHost> receiver) {
        OtfBridgeHost::Get().BindReceiver(rfh, std::move(receiver));
      }));
}

}  // namespace otf
