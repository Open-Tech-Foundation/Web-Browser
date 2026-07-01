// Bridge trust gating (plan.md §"Content security architecture"): only otf's own
// internal UI frames may use the JS<->Rust bridge; web content is denied. The
// same predicate runs in the browser (authoritative: gates the mojo bind) and in
// the renderer (gates whether window.otf is installed at all).

#ifndef OTF_ENGINE_SHIM_OTF_TRUST_H_
#define OTF_ENGINE_SHIM_OTF_TRUST_H_

class GURL;

namespace base {
class CommandLine;
}
namespace content {
class RenderFrameHost;
}

namespace otf {

// otf's internal page scheme (newtab, settings, history, ...). Frames on this
// scheme are always trusted UI frames.
inline constexpr char kInternalScheme[] = "browser";

// The URL the UI WebContents loads on launch: `--dev-ui-url=<url>` (e2e harness),
// then the first positional arg (the dev server URL in `bun run dev`), falling
// back to the internal `browser://newtab`.
GURL ResolveUiUrl();

// Browser-process only, called early (before child processes spawn): if the UI
// loads from a dev http(s) URL, record its origin as the trusted UI origin so
// IsTrustedBridgeUrl accepts the dev UI. No-op for the internal browser:// UI
// (its scheme is trusted directly). Idempotent.
void InitTrustedUiOrigin();

// Forwards the recorded trusted-UI-origin switch onto a child process command
// line so the renderer's IsTrustedBridgeUrl agrees with the browser's.
void AppendTrustSwitches(base::CommandLine* child_command_line);

// True if a frame at `url` is a trusted otf UI frame allowed to use the bridge:
// the internal browser:// scheme, or (dev only) the recorded dev UI origin.
bool IsTrustedBridgeUrl(const GURL& url);

// Browser-side convenience: gate on a frame's last committed URL.
bool IsTrustedBridgeFrame(content::RenderFrameHost* render_frame_host);

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_TRUST_H_
