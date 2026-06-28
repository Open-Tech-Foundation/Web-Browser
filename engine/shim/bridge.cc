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
// TODO(content): includes such as
//   #include "content/public/app/content_main.h"
//   #include "content/public/browser/web_contents.h"
//   #include "ui/views/widget/widget.h"
// plus the OtfContentMainDelegate / OtfBrowserMainParts wiring.

extern "C" {

OtfStatus otf_browser_init(int /*argc*/, char** /*argv*/, OtfCallbacks callbacks) {
  g_callbacks = callbacks;
  // TODO(content): build content::ContentMainParams with OtfContentMainDelegate.
  return 0;
}

OtfStatus otf_browser_run(void) {
  // TODO(content): content::RunBrowserProcessMain / enter the run loop.
  return 0;
}

void otf_browser_shutdown(void) { /* TODO(content): teardown */ }

OtfStatus otf_ui_create(const char* /*url*/) { return 0; }       // TODO(content)
OtfStatus otf_ui_set_content_bounds(int32_t, int32_t, int32_t, int32_t) { return 0; }

OtfTabHandle otf_tab_create(const char* /*url*/) { return 0; }   // TODO(content)
OtfStatus otf_tab_navigate(OtfTabHandle, const char*) { return 0; }
OtfStatus otf_tab_show(OtfTabHandle) { return 0; }
OtfStatus otf_tab_hide(OtfTabHandle) { return 0; }
OtfStatus otf_tab_close(OtfTabHandle) { return 0; }

OtfStatus otf_bridge_respond(uint64_t /*reply_id*/, const char* /*json*/) { return 0; }
OtfStatus otf_bridge_emit(OtfTabHandle, const char* /*json*/) { return 0; }

}  // extern "C"

#endif  // OTF_WITH_CONTENT
