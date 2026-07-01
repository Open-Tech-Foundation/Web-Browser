#include "otf/shim/otf_content_browser_client.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/browser_main_parts.h"
#include "content/public/browser/devtools_manager_delegate.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "otf/shim/otf_browser_main_parts.h"
#include "otf/shim/otf_bridge.mojom.h"
#include "otf/shim/otf_bridge_host.h"
#include "otf/shim/otf_devtools.h"

namespace otf {

OtfContentBrowserClient::OtfContentBrowserClient() = default;
OtfContentBrowserClient::~OtfContentBrowserClient() = default;

std::unique_ptr<content::BrowserMainParts>
OtfContentBrowserClient::CreateBrowserMainParts(bool /*is_integration_test*/) {
  return std::make_unique<OtfBrowserMainParts>();
}

void OtfContentBrowserClient::RegisterBrowserInterfaceBindersForFrame(
    content::RenderFrameHost* render_frame_host,
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map) {
  content::ContentBrowserClient::RegisterBrowserInterfaceBindersForFrame(
      render_frame_host, map);
  // TODO(security): gate on internal browser:// frames; web content must not see
  // the bridge (mirrors the content-permission whitelist in plan.md).
  map->Add<mojom::BridgeHost>(base::BindRepeating(
      [](content::RenderFrameHost* rfh,
         mojo::PendingReceiver<mojom::BridgeHost> receiver) {
        OtfBridgeHost::Get().BindReceiver(rfh, std::move(receiver));
      }));
}

std::unique_ptr<content::DevToolsManagerDelegate>
OtfContentBrowserClient::CreateDevToolsManagerDelegate() {
  return otf::CreateDevToolsManagerDelegate();
}

std::string OtfContentBrowserClient::GetProduct() {
  // Identifies the browser in the DevTools/CDP /json/version "Browser" field
  // (and anywhere content derives a product string).
  return "OTF/0.1";
}

}  // namespace otf
