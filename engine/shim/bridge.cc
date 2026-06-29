// Implementation of the otf engine shim (bridge.h).
//
// Two build modes:
//   * Default (standalone): a no-Chromium stub so the Rust backend + bindgen +
//     a host harness can be built and unit-tested WITHOUT a Chromium tree
//     (plan.md §9 "standalone Rust harness" — the biggest dev-loop speedup).
//   * OTF_WITH_CONTENT: the real implementation over the content layer, compiled
//     by the gn target inside the Chromium checkout.
//
// Either way the surface is the grouped interface table from bridge.h: each
// area (lifecycle, ui, tabs, bridge) is a struct of function pointers, and
// otf_api() returns the immutable aggregate.

#include "bridge.h"

#include <atomic>
#include <cstdio>

namespace {
OtfCallbacks g_callbacks = {};
// Only the standalone stub hands out synthetic tab handles; the content path
// gets real WebContents handles, so this is unused there (content builds -Werror).
[[maybe_unused]] std::atomic<uint64_t> g_next_tab{1};
}  // namespace

#ifndef OTF_WITH_CONTENT
// ----------------------------- standalone stub -----------------------------
namespace {

OtfStatus StubInit(int /*argc*/, char** /*argv*/, OtfCallbacks callbacks) {
  g_callbacks = callbacks;
  std::fprintf(stderr, "[otf-shim] init (standalone stub)\n");
  return 0;
}
OtfStatus StubRun(void) {
  std::fprintf(stderr, "[otf-shim] run (standalone stub: no event loop)\n");
  return 0;
}
void StubShutdown(void) { g_callbacks = {}; }

OtfStatus StubUiCreate(const char* url) {
  std::fprintf(stderr, "[otf-shim] ui.create(%s)\n", url ? url : "");
  return 0;
}
OtfStatus StubUiSetContentBounds(int32_t, int32_t, int32_t, int32_t) { return 0; }

OtfTabHandle StubTabCreate(const char* /*url*/) { return g_next_tab.fetch_add(1); }
OtfStatus StubTabNavigate(OtfTabHandle, const char*) { return 0; }
OtfStatus StubTabShow(OtfTabHandle) { return 0; }
OtfStatus StubTabHide(OtfTabHandle) { return 0; }
OtfStatus StubTabClose(OtfTabHandle) { return 0; }
OtfStatus StubTabReload(OtfTabHandle) { return 0; }
OtfStatus StubTabStop(OtfTabHandle) { return 0; }
OtfStatus StubTabGoBack(OtfTabHandle) { return 0; }
OtfStatus StubTabGoForward(OtfTabHandle) { return 0; }

OtfStatus StubBridgeRespond(uint64_t, const char*) { return 0; }
OtfStatus StubBridgeEmit(OtfTabHandle, const char*) { return 0; }

const OtfLifecycleApi kLifecycle = {StubInit, StubRun, StubShutdown};
const OtfUiApi kUi = {StubUiCreate, StubUiSetContentBounds};
const OtfTabsApi kTabs = {StubTabCreate,  StubTabNavigate, StubTabShow,
                          StubTabHide,    StubTabClose,    StubTabReload,
                          StubTabStop,    StubTabGoBack,   StubTabGoForward};
const OtfBridgeApi kBridge = {StubBridgeRespond, StubBridgeEmit};
const OtfApi kApi = {OTF_API_VERSION, &kLifecycle, &kUi, &kTabs, &kBridge};

}  // namespace

extern "C" const OtfApi* otf_api(void) { return &kApi; }

#else
// ----------------------------- content layer -------------------------------
// First light (Phase 2): boot Chromium's content layer from Rust by reusing
// content_shell's embedder scaffolding (OtfMainDelegate). The bridge interface
// (Rust <-> JS) is live; the tabs interface is being filled in with real
// WebContents in Phase 2c — until then its ops are no-ops.
#include "content/public/app/content_main.h"
#include "otf/shim/otf_bridge_host.h"
#include "otf/shim/otf_main_delegate.h"

namespace {
int g_argc = 0;
char** g_argv = nullptr;

// --- Lifecycle ---
OtfStatus LifecycleInit(int argc, char** argv, OtfCallbacks callbacks) {
  g_callbacks = callbacks;
  g_argc = argc;
  g_argv = argv;
  // The bridge host lives in the browser process and marshals JS calls to Rust.
  // Child processes also run this init but never bind a renderer-side receiver,
  // so wiring the callbacks unconditionally is harmless.
  otf::OtfBridgeHost::Get().SetCallbacks(callbacks);
  return 0;
}

OtfStatus LifecycleRun(void) {
  // Mirrors content/shell/app/shell_main.cc: the delegate must outlive
  // ContentMain. OtfMainDelegate reuses content_shell's embedder scaffolding but
  // installs otf's browser/renderer clients so the bridge interface is exposed
  // to every frame. ContentMain runs this process — the browser process blocks
  // in the run loop until shutdown; re-exec'd child processes run their logic
  // and return. Returns the process exit code.
  otf::OtfMainDelegate delegate;
  content::ContentMainParams params(&delegate);
  params.argc = g_argc;
  params.argv = const_cast<const char**>(g_argv);
  return content::ContentMain(std::move(params));
}

void LifecycleShutdown(void) { g_callbacks = {}; }

// --- Ui --- (TODO(phase2c): own UI WebContents + content hole)
OtfStatus UiCreate(const char* /*url*/) { return 0; }
OtfStatus UiSetContentBounds(int32_t, int32_t, int32_t, int32_t) { return 0; }

// --- Tabs --- (TODO(phase2c): real WebContents hosted in the window)
OtfTabHandle TabCreate(const char* /*url*/) { return 0; }
OtfStatus TabNavigate(OtfTabHandle, const char*) { return 0; }
OtfStatus TabShow(OtfTabHandle) { return 0; }
OtfStatus TabHide(OtfTabHandle) { return 0; }
OtfStatus TabClose(OtfTabHandle) { return 0; }
OtfStatus TabReload(OtfTabHandle) { return 0; }
OtfStatus TabStop(OtfTabHandle) { return 0; }
OtfStatus TabGoBack(OtfTabHandle) { return 0; }
OtfStatus TabGoForward(OtfTabHandle) { return 0; }

// --- Bridge --- (Rust -> JS, live)
OtfStatus BridgeRespond(uint64_t reply_id, const char* json) {
  otf::OtfBridgeHost::Get().Respond(reply_id, json ? json : "");
  return 0;
}
OtfStatus BridgeEmit(OtfTabHandle /*target*/, const char* json) {
  otf::OtfBridgeHost::Get().Emit(json ? json : "");
  return 0;
}

const OtfLifecycleApi kLifecycle = {LifecycleInit, LifecycleRun,
                                    LifecycleShutdown};
const OtfUiApi kUi = {UiCreate, UiSetContentBounds};
const OtfTabsApi kTabs = {TabCreate, TabNavigate, TabShow,   TabHide,    TabClose,
                          TabReload, TabStop,      TabGoBack, TabGoForward};
const OtfBridgeApi kBridge = {BridgeRespond, BridgeEmit};
const OtfApi kApi = {OTF_API_VERSION, &kLifecycle, &kUi, &kTabs, &kBridge};

}  // namespace

extern "C" const OtfApi* otf_api(void) { return &kApi; }

#endif  // OTF_WITH_CONTENT
