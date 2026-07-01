#ifndef OTF_SHIM_OTF_DEVTOOLS_H_
#define OTF_SHIM_OTF_DEVTOOLS_H_

#include <memory>

namespace content {
class BrowserContext;
class DevToolsManagerDelegate;
}

namespace otf {

// The DevTools manager delegate content requires before the remote-debugging
// server may start (StartRemoteDebuggingServer CHECKs it). otf uses a minimal
// delegate: it only pins the default browser context so targets are
// discoverable; it adds no custom CDP domains (content's built-in agents handle
// the standard protocol). Returned from OtfContentBrowserClient.
std::unique_ptr<content::DevToolsManagerDelegate> CreateDevToolsManagerDelegate();

// Starts the DevTools/CDP remote-debugging HTTP server when the process was
// launched with --remote-debugging-port (an ephemeral port is used if the value
// is 0 or absent-but-forced). content_shell provided this via
// ShellDevToolsManagerDelegate::StartHttpHandler; otf re-wires just the transport
// (no custom protocol domains) so e2e tooling and DevTools can attach to the
// existing UI + tab targets. No-op if the port switch is not present.
void MaybeStartDevToolsServer(content::BrowserContext* browser_context);

// Tears the server down on shutdown. Safe to call even if it was never started.
void StopDevToolsServer();

}  // namespace otf

#endif  // OTF_SHIM_OTF_DEVTOOLS_H_
