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

OtfStatus StubTabCreate(OtfTabHandle, const char* /*url*/) {
  (void)g_next_tab;
  return 0;
}
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
// Boots Chromium's content layer from Rust through otf's own embedder
// (OtfMainDelegate — no content_shell). The bridge interface (Rust <-> JS) and
// the tabs interface (real WebContents) are live.
#include "content/public/app/content_main.h"
#include "otf/shim/otf_bridge_host.h"
#include "otf/shim/otf_main_delegate.h"
#include "otf/shim/otf_tab_host.h"

namespace {
int g_argc = 0;
char** g_argv = nullptr;

const char* str_or_empty(const char* s) { return s ? s : ""; }

// --- Lifecycle ---
OtfStatus LifecycleInit(int argc, char** argv, OtfCallbacks callbacks) {
  g_callbacks = callbacks;
  g_argc = argc;
  g_argv = argv;
  // The bridge host lives in the browser process and marshals JS calls to Rust.
  // Child processes also run this init but never bind a renderer-side receiver,
  // so wiring the callbacks unconditionally is harmless. The tab host reuses the
  // same observer table to push content events (title/url/load) back to Rust.
  otf::OtfBridgeHost::Get().SetCallbacks(callbacks);
  otf::OtfTabHost::Get().SetCallbacks(callbacks);
  return 0;
}

OtfStatus LifecycleRun(void) {
  // The delegate must outlive ContentMain. OtfMainDelegate is otf's own
  // ContentMainDelegate: it loads otf's resources and installs otf's
  // browser/renderer clients (exposing the bridge to every frame) and builds
  // otf's own window. ContentMain runs this process — the browser process blocks
  // in the run loop until shutdown; re-exec'd child processes run their logic
  // and return. Returns the process exit code.
  otf::OtfMainDelegate delegate;
  content::ContentMainParams params(&delegate);
  params.argc = g_argc;
  params.argv = const_cast<const char**>(g_argv);
  return content::ContentMain(std::move(params));
}

void LifecycleShutdown(void) { g_callbacks = {}; }

// --- Ui --- (the UI WebContents + window are created by OtfBrowserMainParts)
OtfStatus UiCreate(const char* /*url*/) { return 0; }
OtfStatus UiSetContentBounds(int32_t x, int32_t y, int32_t w, int32_t h) {
  otf::OtfTabHost::Get().SetContentBounds(x, y, w, h);
  return 0;
}

// --- Tabs --- real WebContents hosted in the UI window (otf_tab_host.cc).
OtfStatus TabCreate(OtfTabHandle id, const char* url) {
  return otf::OtfTabHost::Get().Create(id, str_or_empty(url));
}
OtfStatus TabNavigate(OtfTabHandle id, const char* url) {
  return otf::OtfTabHost::Get().Navigate(id, str_or_empty(url));
}
OtfStatus TabShow(OtfTabHandle id) { return otf::OtfTabHost::Get().Show(id); }
OtfStatus TabHide(OtfTabHandle id) { return otf::OtfTabHost::Get().Hide(id); }
OtfStatus TabClose(OtfTabHandle id) { return otf::OtfTabHost::Get().Close(id); }
OtfStatus TabReload(OtfTabHandle id) { return otf::OtfTabHost::Get().Reload(id); }
OtfStatus TabStop(OtfTabHandle id) { return otf::OtfTabHost::Get().Stop(id); }
OtfStatus TabGoBack(OtfTabHandle id) { return otf::OtfTabHost::Get().GoBack(id); }
OtfStatus TabGoForward(OtfTabHandle id) {
  return otf::OtfTabHost::Get().GoForward(id);
}

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
