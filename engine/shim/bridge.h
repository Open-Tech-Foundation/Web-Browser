// otf engine shim — FFI boundary between the Rust backend and Chromium's
// content layer.
//
// This is the ONE hand-written C header (plan.md §3). It is consumed two ways:
//   * C++:  bridge.cc implements it over the content layer.
//   * Rust: bindgen turns it into `extern "C"` declarations, wrapped safely.
//
// Shape (plan.md): the Rust->Chromium surface is a set of *grouped interfaces*,
// each a struct of function pointers for one logical area (lifecycle, UI, tabs,
// bridge; future: cookies, network, gpu, …). A single exported entry point,
// `otf_api()`, returns an immutable, versioned table aggregating them. This keeps
// the surface broad/stable and discoverable: Rust calls `api->tabs->create(...)`
// rather than a flat soup of free functions, and new areas slot in as new
// sub-interfaces without disturbing existing ones.
//
// The reverse direction (Chromium -> Rust: content events + bridge requests) is
// the `OtfCallbacks` observer table, handed to `lifecycle->init`.
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
// Handles & status
// ---------------------------------------------------------------------------
// A tab id. CALLER-ASSIGNED: Rust owns the tab id space (it is authoritative
// for tab state, plan.md), so it picks the id and the shim maps id -> a
// content::WebContents on the browser thread. The same id flows back in the
// content-event callbacks. 0 == invalid.
typedef uint64_t OtfTabHandle;
typedef int32_t OtfStatus;           // 0 == ok, negative == error code

// ---------------------------------------------------------------------------
// Observer: Chromium -> Rust (content events + bridge requests from JS)
// ---------------------------------------------------------------------------
// `user_data` is the opaque pointer passed to lifecycle->init; it carries the
// Rust backend instance back into these C callbacks.
typedef struct OtfCallbacks {
  void* user_data;

  // A JS -> Rust bridge call arrived from a UI/tab WebContents. `request_json`
  // is the wire envelope { id, method, params }. Rust handles it asynchronously
  // and replies later via bridge->respond(reply_id, ...).
  void (*on_js_call)(void* user_data, uint64_t reply_id, const char* request_json);

  // Content lifecycle signals (plan.md §3). Strings are UTF-8.
  void (*on_title_changed)(void* user_data, OtfTabHandle tab, const char* title);
  void (*on_url_changed)(void* user_data, OtfTabHandle tab, const char* url);
  void (*on_load_state)(void* user_data, OtfTabHandle tab, int32_t is_loading);

  // A key press reached the router and was not a reserved shortcut nor consumed
  // by the page (ESC / fullscreen fallback path). Rust decides what to do.
  // Returns 1 if Rust consumed it, 0 to let it propagate.
  int32_t (*on_unhandled_key)(void* user_data, OtfTabHandle tab,
                              int32_t key_code, uint32_t modifiers);

  // A popup overlay was dismissed by the shim itself (a click outside its
  // bounds), not by an explicit ui.popup.hide. Rust clears its open-state so a
  // subsequent toggle re-opens it. `name` is the popup's name.
  void (*on_popup_closed)(void* user_data, const char* name);

  // A context menu was requested on a page (right-click). `params_json` carries
  // the hit-test context ({ x, y, mediaType, linkUrl, srcUrl, selectionText,
  // isEditable, hasImage, canCopy/canPaste/…, pageUrl }); Rust shows otf's own
  // menu overlay against it.
  void (*on_context_menu)(void* user_data, OtfTabHandle tab, const char* params_json);
} OtfCallbacks;

// ---------------------------------------------------------------------------
// Interface: Lifecycle — process boot/run/shutdown
// ---------------------------------------------------------------------------
typedef struct OtfLifecycleApi {
  // Boots the content layer; `callbacks` is the Chromium -> Rust observer.
  OtfStatus (*init)(int argc, char** argv, OtfCallbacks callbacks);
  // Enters Chromium's run loop; blocks until shutdown. Returns the exit code.
  OtfStatus (*run)(void);
  void      (*shutdown)(void);
} OtfLifecycleApi;

// ---------------------------------------------------------------------------
// Interface: Ui — the React app, hosted as its own WebContents
// ---------------------------------------------------------------------------
typedef struct OtfUiApi {
  OtfStatus (*create)(const char* url);
  // Position of the "hole" the active tab's native view is parented into.
  OtfStatus (*set_content_bounds)(int32_t x, int32_t y, int32_t w, int32_t h);

  // Named popup overlays: transparent WebContents layered over the window (its
  // page is `<name>.html`). `show` opens/raises it, `hide` dismisses it. The shim
  // also dismisses on a click outside its bounds (reported via on_popup_closed).
  OtfStatus (*popup_show)(const char* name);
  OtfStatus (*popup_hide)(const char* name);
} OtfUiApi;

// ---------------------------------------------------------------------------
// Interface: Tabs — content::WebContents managed on the browser thread
// ---------------------------------------------------------------------------
typedef struct OtfTabsApi {
  // Bind a tab to a workspace *before* its WebContents is created, so the tab's
  // cookies/cache/storage come from that workspace's isolated context. `id` is
  // the workspace id string (UUID-ready). Must be called before create/navigate.
  OtfStatus (*set_workspace)(OtfTabHandle tab, const char* workspace_id);

  // Create a WebContents bound to the caller-assigned `tab` id (no-op if it
  // already exists). `navigate` lazily creates one if needed, so explicit
  // create is optional.
  OtfStatus (*create)(OtfTabHandle tab, const char* url);
  OtfStatus (*navigate)(OtfTabHandle tab, const char* url);
  OtfStatus (*show)(OtfTabHandle tab);     // parent native view into the hole
  OtfStatus (*hide)(OtfTabHandle tab);
  OtfStatus (*close)(OtfTabHandle tab);
  OtfStatus (*reload)(OtfTabHandle tab);
  OtfStatus (*stop)(OtfTabHandle tab);
  OtfStatus (*go_back)(OtfTabHandle tab);
  OtfStatus (*go_forward)(OtfTabHandle tab);

  // Run a context-menu action on the tab's page: `action` is one of
  // undo/redo/cut/copy/paste/pasteMatchStyle/delete/selectAll/copyImage/saveImage.
  // (x, y) are the page-relative hit point, needed for the image actions.
  OtfStatus (*context_action)(OtfTabHandle tab, const char* action,
                              int32_t x, int32_t y);
} OtfTabsApi;

// ---------------------------------------------------------------------------
// Interface: Bridge — Rust -> JS (responses + pushed events)
// ---------------------------------------------------------------------------
typedef struct OtfBridgeApi {
  // Reply to an on_js_call (response envelope { id, ok, result|error }).
  OtfStatus (*respond)(uint64_t reply_id, const char* response_json);
  // Push an event to a subscribed surface ({ key, ... }). target==0 -> UI.
  OtfStatus (*emit)(OtfTabHandle target, const char* event_json);
} OtfBridgeApi;

// ---------------------------------------------------------------------------
// Aggregate: the versioned table of all Rust -> Chromium interfaces
// ---------------------------------------------------------------------------
// Returned by otf_api(). Immutable and process-wide. `version` lets the Rust
// side guard against ABI drift; new interfaces (cookies, network, gpu, …) are
// added as additional pointers at the end without breaking older fields.
typedef struct OtfApi {
  uint32_t version;                 // == OTF_API_VERSION at build time

  const OtfLifecycleApi* lifecycle;
  const OtfUiApi*        ui;
  const OtfTabsApi*      tabs;
  const OtfBridgeApi*    bridge;
  // TODO(phase4): const OtfCookiesApi* cookies; const OtfNetworkApi* network;
  //               const OtfGpuApi* gpu; …
} OtfApi;

#define OTF_API_VERSION 1u

// The single exported symbol. Returns the immutable interface table.
const OtfApi* otf_api(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // OTF_ENGINE_SHIM_BRIDGE_H_
