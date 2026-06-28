# MIGRATION — Bridge Map (Step 2)

> **Status: STOP-AND-CONFIRM GATE.** This document maps the entire UI↔backend bridge
> surface as it exists on the CEF backend. No code has been modified. Per the migration
> prompt, review this before any adapter/quarantine/skeleton work begins.

This is the contract the new **Rust/content** backend must reproduce so the React UI can
be cut over by swapping *transport only*, not call sites.

---

## 1. Transport overview (how the bridge works today)

There is exactly **one** physical channel: CEF's message router, exposed to JS as
`window.cefQuery(...)`. Everything rides on it.

- **JS → C++ (request/response):** `window.cefQuery({ request, onSuccess, onFailure })`.
  - C++ entry point: `OtfMessageRouterHandler::OnQuery` (`src/otf_message_router_handler.cc`).
  - Router config: `js_query_function = "cefQuery"`, `js_cancel_function = "cefQueryCancel"`
    (`src/otf_event_runtime.cc:90` `EnsureMessageRouterInitialized`).
- **C++ → JS (event push):** `OtfHandler::SendEvent(json)` calls `Success(json)` on a stored
  **persistent** callback (`src/otf_event_runtime.cc:77`). One stored callback per subscribing
  browser/overlay.

There are **two message shapes** multiplexed over that one channel:

### 1a. RPC shape (the modern surface — almost everything)
JSON envelope, request/response, `persistent: false`.

```jsonc
// request  (JS → C++)
{ "id": "ui-<n>", "method": "<namespace>.<verb>", "params": { ... } }

// response (C++ → JS), delivered via onSuccess as a STRING
{ "id": "ui-<n>", "ok": true,  "result": <any> }
{ "id": "ui-<n>", "ok": false, "error": { "code": "<code>", "message": "<text>" } }
```

- Built/parsed in `src/otf_native_rpc.cc` (`ParseNativeRpcRequest`, `NativeRpcSuccess*`,
  `NativeRpcFailure`). Envelope fields are strictly validated: `id` (string, 1–80 chars),
  `method` (string, 1–96 chars), `params` (object). Unknown keys are rejected.
- Dispatched in `src/otf_rpc_dispatcher.cc` → 16 module handlers (chain of responsibility;
  first handler that claims the method wins). Unknown method → `error.code = "unknown_method"`.
- The JS side of this is centralized in **`ui/src/shared/nativeRequest.js`** — already a
  promise-returning RPC client. **This is effectively an existing bridge adapter** and the
  natural seam for Step 3.

### 1b. Subscription shape (event streams)
Same envelope, but `persistent: true`. The C++ handler stores the callback and pushes an
unbounded stream of event objects through it. Each pushed event is a **standalone JSON object
carrying a `key` field** (no `id`, not wrapped in the `ok/result` envelope):

```jsonc
{ "key": "<event-name>", ... }                       // generic event
{ "id": <tabId>, "key": "<prop>", "value": <v> }     // per-tab property delta
{ "key": "shortcut", "value": "<shortcut-name>" }    // reserved-shortcut forward
```

Subscriptions are invoked via dedicated `*.subscribe` RPC methods (see §4). The JS for these
does **not** go through `nativeRequest`; it calls `window.cefQuery({ ..., persistent: true })`
directly (4 raw call sites — see §6).

> **Migration note:** the new bridge must preserve BOTH shapes: a promise RPC channel AND a
> persistent event-stream channel keyed by `key`. The plan's `js_call` (in) / `rust_emit` (out)
> shim primitives map directly onto 1a and 1b respectively.

---

## 2. Trust / security gating (must be reproduced)

`OnQuery` enforces who may call the bridge — the new shim/router must keep this:

- Request size cap: **64 KiB** (`kMaxRequestBytes`); over → `Failure(1, "request too large")`.
- Only frames where `IsInternalBrowserUiUrl(frame_url)` is true (the internal `browser://` UI
  surfaces) are **trusted** and may call any method.
- Dev escape hatch: a frame matching the `--dev-ui-url` command-line switch is also trusted
  (HMR dev server).
- **Untrusted web content** is denied outright **except** a whitelist: methods for which
  `IsAllowedContentPermissionRequest(method)` is true AND `ShouldInjectPagePolicy(frame_url)`.
  For those, any `params.url` is force-overwritten with the caller's real `frame_url` (anti-spoof).
- All other web-content queries → `Failure(1, "denied")`.

---

## 3. RPC method surface (JS → C++ request/response)

Grouped by namespace / dispatcher module. Payload shapes shown are those the UI actually sends
(extracted from JS call sites) plus the C++ method registry. `params: {}` means none observed.
Results are JSON (object/array/string/bool) returned in `result`.

### `tabs.*` — `otf_tabs_rpc.cc`
| Method | params (UI) | Notes |
|---|---|---|
| `tabs.list` | `{}` | array of tab objects (see §5 tab shape) |
| `tabs.active` | `{}` | active tab id/context |
| `tabs.switch` | `{ tabId }` | |
| `tabs.close` | `{ tabId }` | |
| `tabs.reload` / `tabs.stop` | `{ tabId? }` | |
| `tabs.back` / `tabs.forward` | `{ tabId? }` | |
| `tabs.mute` / `tabs.unmute` | `{ tabId }` | |
| `tabs.zoomIn` / `tabs.zoomOut` / `tabs.zoomReset` | `{ tabId? }` | |
| `tabs.memory` | `{ tabId }` | per-tab memory bytes |
| `tabs.currentContext` | `{}` | current tab context (url/title/flags) |
| `tabs.splitState` | `{}` | split layout state |

### `navigation.*` — `otf_navigation_rpc.cc`
| Method | params | Notes |
|---|---|---|
| `navigation.current` | `{ url? }` | get or set current tab URL (navigate) |
| `navigation.tab` | `{}` | |
| `navigation.newTab` | `{ url? }` | |
| `navigation.newPrivateTab` | `{ url? }` | |
| `navigation.resolveInput` | `{ input }` | omnibox → URL/search resolution |

### `workspaces.*` — `otf_workspaces_rpc.cc`
`workspaces.list`, `workspaces.create`, `workspaces.delete`, `workspaces.rename`,
`workspaces.switch`.

### `split.*` — `otf_split_runtime.cc` (handler in tabs/ui chain)
`split.current`, `split.withCurrent`, `split.addTab {tabId}`, `split.swap`, `split.close`,
`split.closePane`.

### `settings.*` — `otf_settings_rpc.cc`
`settings.get`, `settings.set`, `settings.versionInfo`, `settings.storagePaths`,
`settings.setStoragePath`, `settings.storageTotals`, `settings.siteUsageList`,
`settings.selectFolder`, `settings.resetBrowserData`, `settings.restart`.

### `history.*` — `otf_history_bookmarks_rpc.cc`
`history.list`, `history.delete {id}`, `history.clear`.

### `bookmarks.*` — `otf_history_bookmarks_rpc.cc`
`bookmarks.list`, `bookmarks.add`, `bookmarks.remove {id}`, `bookmarks.update`,
`bookmarks.isBookmarked`, `bookmarks.toggleCurrent`, `bookmarks.subscribe` *(stream)*.

### `downloads.*` — `otf_downloads_rpc.cc`
`downloads.list`, `downloads.subscribe` *(stream)*, `downloads.cancel`, `downloads.pause`,
`downloads.resume`, `downloads.retry`, `downloads.open`, `downloads.openPage`,
`downloads.showInFolder`, `downloads.copyLink`, `downloads.clearFinished`.

### `siteData.*` — `otf_site_data_rpc.cc`
`siteData.getCookies {origin}`, `siteData.getStorage {origin}`,
`siteData.getPermissions {origin}`, `siteData.getCookiePolicy {origin}`,
`siteData.getCrossOriginResources {origin}`, `siteData.setPermission`,
`siteData.clearCookies`, `siteData.clearStorage`, `siteData.clearPermissions`,
`siteData.openPage {origin}`, `siteData.showClearPopup {origin}`.

### `browsingData.*` / `Storage.*` / `Browser.*` — `otf_clear_data_rpc.cc`
`browsingData.clear`, `Storage.getUsageAndQuota`, `Storage.clearDataForOrigin`,
`Browser.resetPermissions`. *(Capitalized names mirror CDP domains.)*

### `cookieTracking.*` — `otf_cookie_tracking_rpc.cc`
`cookieTracking.list`.

### `permissions.*` — `otf_permissions_rpc.cc` (content-callable whitelist lives here)
`permissions.popup.allow`, `permissions.popup.alwaysAllow`,
`permissions.download.allow`, `permissions.download.alwaysAllow`,
`permissions.autoPictureInPicture.request`.

### `search.*` / `session.*` — `otf_search_rpc.cc`
`search.suggestions`, `search.history.add`, `session.createGuest`, `session.isGuest`.

### `findbar.*` — `otf_findbar_rpc.cc`
`findbar.find`, `findbar.stop`, `findbar.close`, `findbar.subscribe` *(stream)*.

### `console.*` — `otf_console_rpc.cc`
`console.logs`, `console.clear`, `console.setWidth`, `console.subscribe` *(stream)*.

### `imagePreview.*` — `otf_image_preview_rpc.cc`
`imagePreview.subscribe` *(stream)*, `.decode`, `.thumbnail`, `.getSize`, `.setMeta`,
`.setInfoVisible`, `.refresh`, `.download`, `.close`.

### `docPreview.*` — `otf_doc_preview_rpc.cc`
`docPreview.subscribe` *(stream)*, `.refresh`, `.download`, `.close`,
plus `download.tiff`, `download.txt`.

### `ui.*` — `otf_ui_rpc.cc` (UI-shell orchestration: overlays, focus, chrome)
| Method | Purpose |
|---|---|
| `ui.events.subscribe` *(stream)* | **primary** app event stream (App.jsx) |
| `ui.focus` | focus the active tab / UI |
| `ui.fullscreen.toggle` | |
| `ui.toast.show` | transient toast |
| `ui.appMenu.toggle` / `ui.appMenu.hide` | app menu overlay |
| `ui.popup.show` / `.hide` / `.toggle` / `.restoreSubscribe` *(stream)* | generic popup overlay |
| `ui.findbar.show` | |
| `ui.zoomBar.toggle` / `.hide` / `.subscribe` *(stream)* | zoom indicator |
| `ui.downloadsBar.toggle` / `.hide` | downloads bar |
| `ui.bookmarkBar.hide` | |
| `ui.console.show` / `.hide` / `.toggle` | dev console overlay |
| `ui.certificate.get {tabId}` / `.toggle` / `.hide` / `.subscribe` *(stream)* | cert viewer |
| `ui.qr.show {url}` | QR overlay |
| `ui.snip.start` / `ui.snipPreview.hide` | screenshot/snip |

> **Total: ~130 RPC methods across 16 dispatcher modules.** The new bridge surface should
> mirror these names verbatim so the UI adapter (Step 6) only swaps transport.

---

## 4. Subscription channels (the `*.subscribe` methods)

Each is a `persistent: true` RPC that opens a one-way event stream. One stored callback per
subscribing browser/overlay on the C++ side.

| Subscribe method | Consumer (browser/overlay) | Carries (event `key`s) |
|---|---|---|
| `ui.events.subscribe` | `App.jsx` (app-shell) | tab/workspace state, shortcuts — see §5 |
| `findbar.subscribe` | `findbar/FindBar.jsx` | `find-result`, `find-restore`, `findbar-closed` |
| `downloads.subscribe` | `downloads/Downloads.jsx` | `downloads-update`, `downloads-refresh`, `downloads-badge` |
| `bookmarks.subscribe` | `bookmarks/Bookmarks.jsx` | `bookmarks-changed`, `bookmark-refresh`, `bookmark-sync` |
| `console.subscribe` | console overlay | `console-entry`, `console-tab-changed` |
| `ui.zoomBar.subscribe` | zoom indicator | `zoomPercent`, `zoom-restore` |
| `ui.certificate.subscribe` | `certificate/CertificateViewer.jsx` | `certificate-restore`, `sslError` |
| `ui.popup.restoreSubscribe` | `components/Popup.jsx` | `popup-restore`, `popup-blocked` |
| `imagePreview.subscribe` | image preview tab | `load-image`, `image-preview-download-progress` |
| `docPreview.subscribe` | doc preview tab | `load-doc` |

---

## 5. Event surface (C++ → JS push, keyed by `key`)

All emitted via `OtfHandler::SendEvent(...)`. Two families:

### 5a. Generic events (object with `key` + payload)
| `key` | Emitted by | Payload (besides `key`) |
|---|---|---|
| `new-tab` | `otf_event_runtime.cc` | `tab` (tab obj), `parentTabId` |
| `tab-closed` | | tab id |
| `active-tab-changed` | | active tab id |
| `workspaces-updated` | | workspace list |
| `workspace-changed` | | active workspace |
| `settings-changed` | | changed settings |
| `guest-session-changed` | | guest flag |
| `split-state-changed` | | split layout |
| `bookmarks-changed` / `bookmark-refresh` / `bookmark-sync` | bookmarks | |
| `history-changed` | history | |
| `downloads-update` / `downloads-refresh` / `downloads-badge` | downloads | |
| `find-result` / `find-restore` / `findbar-closed` | findbar | match counts / state |
| `popup-blocked` / `popup-restore` | popup overlay | |
| `certificate-restore` | cert viewer | |
| `console-entry` / `console-tab-changed` | console | log entry |
| `load-image` / `image-preview-download-progress` | image preview | `url`, progress |
| `load-doc` | doc preview | `url` |
| `zoom-restore` | zoom bar | |
| `memoryBytes` | tab memory | value |

### 5b. Per-tab property deltas — `BuildTabPropertyEvent(tab_id, key, value)`
Shape: `{ "id": <tabId>, "key": "<prop>", "value": <v> }`. Keys observed:
`title`, `url`, `favicon`, `loading`, `canGoBack`, `canGoForward`, `load-end`,
`zoomPercent`, `muted`, `pinned`. Also single-key pushes for `sslError`, `url`, `zoomPercent`.

App.jsx reducer coerces some values: `zoomPercent`→Number; `sslError`/`muted`/`pinned`→bool.

**Tab object shape** (from `BuildTabJson`, `otf_event_runtime.cc:16`):
`{ id, url, title, zoomPercent, sslError(bool), muted(bool), private(bool), pinned(bool),
guest(bool), favicon?, bookmarked(bool) }`.

### 5c. Reserved-shortcut forward — `{ "key": "shortcut", "value": "<name>" }`
Emitted by `SendShortcut` (`include/otf_keyboard_shortcuts.h:86`). Reserved shortcuts are
intercepted in C++ **before the page** and forwarded to the UI to act on. **Names** (the
reserved-shortcut table the plan calls for):

`new-tab`, `close-tab`, `reopen-tab`, `next-tab`, `prev-tab`, `focus-bar`, `reload`, `back`,
`forward`, `escape`, `zoom-in`, `zoom-out`, `zoom-reset`, `find`, `find-next`, `find-prev`,
`fullscreen`, `start-snip`.

Key/modifier tables also live in `otf_keyboard_shortcuts.h` (`Key::*`, `Mod::*`) and the
routing logic in `otf_keyboard_runtime.cc`. Plan §4 maps this onto
`RenderWidgetHost::AddKeyPressEventCallback` + the shim router + `on_unhandled_key`.

---

## 6. UI call sites (where the bridge is touched)

**Centralized client (the seam to keep):** `ui/src/shared/nativeRequest.js`
- `nativeRequest(request, options)` — promise RPC client (§1a). Used by **23 files**.
- `getNativeSettings()` — convenience wrapper for `settings.get`.
- `createNativeRequestScope()` — client-side request-versioning/cancellation helper
  (compensates for the bus having no native cancellation).

**Raw `window.cefQuery` call sites (bypass nativeRequest — all are `persistent: true` streams):**
| File | Subscribe method |
|---|---|
| `ui/src/App.jsx:217` | `ui.events.subscribe` |
| `ui/src/downloads/Downloads.jsx:92` | `downloads.subscribe` |
| `ui/src/components/Popup.jsx:75` | `ui.popup.restoreSubscribe` |
| `ui/src/certificate/CertificateViewer.jsx:91` | `ui.certificate.subscribe` |

**Files importing `nativeRequest`:** App, TabStrip, WorkspaceSwitcher, AddressBar(via Popup),
Popup, Downloads, ClearSiteData, SearchHero, QuickLinks, NewTab, QrCode, SiteData,
SplitPlaceholder, SplitMenu, Security, InsecureBlocked, CertificateViewer, Bookmarks, AppMenu,
History, Settings, DownloadRequest, BlockedPopup. (`grep`: 23 files import it; 14 reference
`cefQuery` directly, mostly through `nativeRequest`.)

> The `window.cefQuery` global is the **only** CEF-specific symbol the UI binds to. The clean
> Step-3 move is: (a) wrap the 4 raw persistent call sites into adapter `bridge.on(event, cb)`,
> (b) keep `nativeRequest` as the adapter's `bridge.call(method, params)`, (c) back both by the
> existing `window.cefQuery` initially, then swap the transport in Step 6/7.

---

## 7. C++ side — what gets REPLACED vs. what is bridge logic to PORT

**Replace (CEF host/lifecycle/transport):**
- `OtfMessageRouterHandler` / `CefMessageRouterBrowserSide` wiring (`otf_message_router_handler.*`,
  `EnsureMessageRouterInitialized`).
- `SendEvent` persistent-callback mechanism (CEF `Callback::Success`).
- CEF process model / `otf_app.cc` `CefApp`, `OnProcessMessageReceived` routing.

**Port to the new transport (the bridge *contract*, backend-agnostic):**
- Envelope parse/build + error codes (`otf_native_rpc.cc`) — pure JSON, reusable as-is shape.
- The 16-module dispatcher and all method handlers (`otf_*_rpc.cc`, `otf_*_runtime.cc`) — these
  are mostly browser logic, not CEF transport. Their CEF type usages (`CefBrowser`, `CefRefPtr`,
  `CefDictionaryValue`) are what get re-pointed at the content-layer shim.
- Trust gating (§2) — re-implement in the shim's input router.
- Reserved-shortcut table/router (§5c) — re-implement on `AddKeyPressEventCallback`.

---

## 8. Open questions for review (before Step 3)

1. **Adapter granularity:** keep `nativeRequest.js` as the adapter (`bridge.call`) and add a
   thin `bridge.on(event, cb)` for the 4 persistent streams — or introduce a brand-new
   `bridge` module and re-point all 23 files? Recommendation: **keep `nativeRequest` as the
   call channel, add `bridge.on` for streams** → minimal churn, smallest diff.
2. **Event-stream fan-out:** today each overlay opens its own persistent subscription. Should the
   new shim keep N independent streams (one per WebContents), or one multiplexed stream filtered
   by `key`? Recommendation: keep **per-WebContents streams** to preserve the trust boundary and
   lifecycle semantics.
3. **Capitalized CDP-style methods** (`Storage.*`, `Browser.*`) — keep verbatim or normalize to
   `namespace.verb`? Recommendation: keep verbatim for parity; rename later if desired.
4. **`download.tiff` / `download.txt`** sit outside the `docPreview.*` namespace though handled by
   the doc-preview module — confirm they stay as-is.

---

## 9. Summary numbers
- **1** physical transport (`window.cefQuery`), **2** message shapes (RPC + event stream).
- **~130** RPC methods, **16** dispatcher modules.
- **10** subscription channels, **~35** distinct event `key`s (incl. per-tab property deltas).
- **18** reserved-shortcut names.
- **1** CEF-specific JS symbol the UI binds (`window.cefQuery`), funneled through **1** client
  module (`nativeRequest.js`) + **4** raw persistent call sites.
