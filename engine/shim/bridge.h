// otf engine shim — FFI boundary between the Rust backend and Chromium's
// content layer.
//
// This is the ONE hand-written C header (plan.md §3). It is consumed two ways:
//   * C++:  bridge.cc implements it over the content layer.
//   * Rust: bindgen turns it into `extern "C"` declarations, wrapped safely.
//
// Rules (plan.md):
//   * Keep this surface BROAD and STABLE early so Rust-only changes don't force
//     a C++ recompile / full relink.
//   * Pure C types only (opaque handles + POD structs + function pointers).
//   * The bridge is ASYNC: JS->Rust calls return immediately; results and events
//     flow back through the registered callbacks. No blocking round-trips.

#ifndef OTF_ENGINE_SHIM_BRIDGE_H_
#define OTF_ENGINE_SHIM_BRIDGE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------
// Opaque to Rust. A tab is a content::WebContents on the browser thread.
typedef uint64_t OtfTabHandle;       // 0 == invalid
typedef int32_t OtfStatus;           // 0 == ok, negative == error code

// ---------------------------------------------------------------------------
// Callbacks: shim -> Rust (content events + bridge requests from JS)
// ---------------------------------------------------------------------------
// `user_data` is the opaque pointer passed to otf_browser_init; it carries the
// Rust backend instance back into these C callbacks.
typedef struct OtfCallbacks {
  void* user_data;

  // A JS -> Rust bridge call arrived from a UI/tab WebContents. `request_json`
  // is the wire envelope { id, method, params }. Rust handles it asynchronously
  // and replies later via otf_bridge_respond(reply_id, ...).
  void (*on_js_call)(void* user_data, uint64_t reply_id, const char* request_json);

  // Content lifecycle signals (plan.md §3). `value_json` is UTF-8 JSON.
  void (*on_title_changed)(void* user_data, OtfTabHandle tab, const char* title);
  void (*on_url_changed)(void* user_data, OtfTabHandle tab, const char* url);
  void (*on_load_state)(void* user_data, OtfTabHandle tab, int32_t is_loading);

  // A key press reached the router and was not a reserved shortcut nor consumed
  // by the page (ESC / fullscreen fallback path). Rust decides what to do.
  // Returns 1 if Rust consumed it, 0 to let it propagate.
  int32_t (*on_unhandled_key)(void* user_data, OtfTabHandle tab,
                              int32_t key_code, uint32_t modifiers);
} OtfCallbacks;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
// Boots the content layer. Chromium owns the event loop natively after this
// call returns control via the run loop; otf_browser_run blocks until shutdown.
OtfStatus otf_browser_init(int argc, char** argv, OtfCallbacks callbacks);
OtfStatus otf_browser_run(void);        // enters Chromium's run loop (blocks)
void      otf_browser_shutdown(void);

// ---------------------------------------------------------------------------
// UI surface (the React app, hosted as its own WebContents)
// ---------------------------------------------------------------------------
OtfStatus otf_ui_create(const char* url);
// Position of the "hole" the active tab's native view is parented into.
OtfStatus otf_ui_set_content_bounds(int32_t x, int32_t y, int32_t w, int32_t h);

// ---------------------------------------------------------------------------
// Tabs (content::WebContents managed on one thread)
// ---------------------------------------------------------------------------
OtfTabHandle otf_tab_create(const char* url);
OtfStatus    otf_tab_navigate(OtfTabHandle tab, const char* url);
OtfStatus    otf_tab_show(OtfTabHandle tab);     // parent native view into the hole
OtfStatus    otf_tab_hide(OtfTabHandle tab);
OtfStatus    otf_tab_close(OtfTabHandle tab);

// ---------------------------------------------------------------------------
// Bridge: Rust -> JS
// ---------------------------------------------------------------------------
// Reply to an on_js_call (response envelope { id, ok, result|error }).
OtfStatus otf_bridge_respond(uint64_t reply_id, const char* response_json);
// Push an event to a subscribed surface ({ key, ... }). target==0 -> UI surface.
OtfStatus otf_bridge_emit(OtfTabHandle target, const char* event_json);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // OTF_ENGINE_SHIM_BRIDGE_H_
