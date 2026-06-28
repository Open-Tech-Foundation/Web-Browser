// Implementation of the otf engine shim (bridge.h).
//
// Two build modes:
//   * Default (standalone): a no-Chromium stub so the Rust backend + bindgen +
//     a host harness can be built and unit-tested WITHOUT a Chromium tree
//     (plan.md §9 "standalone Rust harness" — the biggest dev-loop speedup).
//   * OTF_WITH_CONTENT: the real implementation over the content layer, compiled
//     by the gn target inside the Chromium checkout.
//
// Phase 1 scope: the content-mode functions are scaffolded with the integration
// points marked TODO(content). They are filled in as Phases 2-3 land (UI surface,
// tab model, router). Keeping both modes behind one header preserves the stable
// FFI boundary either way.

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
extern "C" {

OtfStatus otf_browser_init(int /*argc*/, char** /*argv*/, OtfCallbacks callbacks) {
  g_callbacks = callbacks;
  std::fprintf(stderr, "[otf-shim] init (standalone stub)\n");
  return 0;
}

OtfStatus otf_browser_run(void) {
  std::fprintf(stderr, "[otf-shim] run (standalone stub: no event loop)\n");
  return 0;
}

void otf_browser_shutdown(void) { g_callbacks = {}; }

OtfStatus otf_ui_create(const char* url) {
  std::fprintf(stderr, "[otf-shim] ui_create(%s)\n", url ? url : "");
  return 0;
}
OtfStatus otf_ui_set_content_bounds(int32_t, int32_t, int32_t, int32_t) { return 0; }

OtfTabHandle otf_tab_create(const char* /*url*/) { return g_next_tab.fetch_add(1); }
OtfStatus otf_tab_navigate(OtfTabHandle, const char*) { return 0; }
OtfStatus otf_tab_show(OtfTabHandle) { return 0; }
OtfStatus otf_tab_hide(OtfTabHandle) { return 0; }
OtfStatus otf_tab_close(OtfTabHandle) { return 0; }

OtfStatus otf_bridge_respond(uint64_t /*reply_id*/, const char* /*json*/) { return 0; }
OtfStatus otf_bridge_emit(OtfTabHandle, const char* /*json*/) { return 0; }

}  // extern "C"

#else
// ----------------------------- content layer -------------------------------
// First light (Phase 2): boot Chromium's content layer from Rust by reusing
// content_shell's embedder scaffolding — ShellMainDelegate brings the
// ContentClient, ContentBrowserClient, a BrowserContext and a Views window, so a
// real browser window appears showing the startup URL. This proves the whole
// Rust -> content stack end to end. Phase 2b swaps this scaffold for our own UI
// WebContents + tab model + live bridge over the content/public APIs directly.
#include "content/public/app/content_main.h"
#include "content/shell/app/shell_main_delegate.h"

namespace {
int g_argc = 0;
char** g_argv = nullptr;
}  // namespace

extern "C" {

OtfStatus otf_browser_init(int argc, char** argv, OtfCallbacks callbacks) {
  g_callbacks = callbacks;
  g_argc = argc;
  g_argv = argv;
  return 0;
}

OtfStatus otf_browser_run(void) {
  // Mirrors content/shell/app/shell_main.cc: the delegate must outlive
  // ContentMain. ContentMain runs this process — the browser process blocks in
  // the run loop until shutdown; re-exec'd child processes (renderer/gpu/...)
  // run their logic and return. Returns the process exit code.
  content::ShellMainDelegate delegate;
  content::ContentMainParams params(&delegate);
  params.argc = g_argc;
  params.argv = const_cast<const char**>(g_argv);
  return content::ContentMain(std::move(params));
}

void otf_browser_shutdown(void) { g_callbacks = {}; }

// TODO(phase2b): our own UI WebContents + tab model + live bridge replace these.
OtfStatus otf_ui_create(const char* /*url*/) { return 0; }
OtfStatus otf_ui_set_content_bounds(int32_t, int32_t, int32_t, int32_t) { return 0; }

OtfTabHandle otf_tab_create(const char* /*url*/) { return 0; }
OtfStatus otf_tab_navigate(OtfTabHandle, const char*) { return 0; }
OtfStatus otf_tab_show(OtfTabHandle) { return 0; }
OtfStatus otf_tab_hide(OtfTabHandle) { return 0; }
OtfStatus otf_tab_close(OtfTabHandle) { return 0; }

OtfStatus otf_bridge_respond(uint64_t /*reply_id*/, const char* /*json*/) { return 0; }
OtfStatus otf_bridge_emit(OtfTabHandle, const char* /*json*/) { return 0; }

}  // extern "C"

#endif  // OTF_WITH_CONTENT
