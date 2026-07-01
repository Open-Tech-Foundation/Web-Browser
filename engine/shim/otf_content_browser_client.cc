#include "otf/shim/otf_content_browser_client.h"

#include <utility>

#include "base/command_line.h"
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
#include "otf/shim/otf_trust.h"

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
  // Authoritative bridge gate: only otf's own internal UI frames may bind the
  // BridgeHost interface; web content is denied (a compromised renderer that
  // requests it anyway is rejected here). The check runs at bind time, when the
  // frame has committed and its origin is known.
  map->Add<mojom::BridgeHost>(base::BindRepeating(
      [](content::RenderFrameHost* rfh,
         mojo::PendingReceiver<mojom::BridgeHost> receiver) {
        if (!IsTrustedBridgeFrame(rfh)) {
          return;  // Drop the receiver; the pipe closes.
        }
        OtfBridgeHost::Get().BindReceiver(rfh, std::move(receiver));
      }));
}

void OtfContentBrowserClient::AppendExtraCommandLineSwitches(
    base::CommandLine* command_line,
    int /*child_process_id*/) {
  // Propagate the trusted-UI-origin switch so the renderer's bridge gate agrees.
  AppendTrustSwitches(command_line);
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
