# Privacy Browser — Bootstrap Plan

A custom-branded, privacy-focused desktop browser. Chromium engine (untouched), Rust backend, web-based React UI. Desktop only.

---

## 1. Locked Decisions

| Area | Decision |
|---|---|
| Engine | Full Chromium fork. Renderer / GPU / network = untouched. Rebase future Chromium. |
| Layer used | **content** layer only. **No** `chrome` layer. |
| Rust role | Headless **backend + control brain**. Owns tab model, navigation, privacy orchestration, lifecycle. Owns no widgets. |
| Loop | **Chromium owns the event loop** natively. No Rust GUI toolkit → no loop pumping, no nested-loop problem. |
| UI | **Web-based React** (WebUI-style), rendered by Chromium as its own WebContents. |
| Bridge | C++ shim, hand-written C header → `bindgen` → safe Rust wrappers. |
| JS ↔ Rust | **Async** — Promise-based request/response + Rust→JS event push. |
| Windowing | **Windowed mode**, child-view-in-hole (tab view positioned inside UI's content area). |
| Privacy | **No bespoke stack.** Implemented by driving Chromium content/network APIs from Rust. |
| Build | **gn/ninja is master.** Rust → `staticlib`, consumed by a custom gn target + shim. |
| Out of scope (v1) | Extensions, auto-update, mobile. |

---

## 2. Architecture

```
ONE Rust browser process (control + backend logic)
   │  Rust → shim → content  (runtime call direction)
   ▼
C++ shim (thin, extern "C")
   │ hosts
   ▼
content layer ── Chromium owns its loop natively
   │
   ├─ UI WebContents      → your React app (chrome:// style page)
   │
   └─ N Tab WebContents   → actual web pages
            │ Chromium-managed IPC
            ▼
   renderer / GPU / network processes (Chromium-spawned, untouched)
```

- **1 Rust process, always.** Tabs are `WebContents` objects on one thread, **not** Rust processes/threads.
- **Renderer processes** are spawned and managed by Chromium (site isolation), never by Rust.
- **Tab switching** = show/hide the active tab's native child view. Background renderers keep running.

---

## 3. The Shim (FFI boundary)

Hand-written `extern "C"` over content. Clean C header → `bindgen` → safe Rust wrappers on top.

**Surface (indicative):**
- Lifecycle: `browser_init`, `browser_shutdown`
- UI surface: `ui_create(url)`, layout/positioning of the tab "hole"
- Tabs: `tab_create(url) -> TabHandle`, `tab_navigate`, `tab_resize`, `tab_close`, `tab_native_view(TabHandle) -> OS handle`
- Bridge: `js_call` dispatch in, `rust_emit` event out
- Callbacks → Rust: `on_title_changed`, `on_url_changed`, `on_load_state`, `on_unhandled_key`

**Rule:** freeze the shim API broad and early. Rust-only changes then avoid C++ recompile.

---

## 4. Input Routing (single router)

One thread, one router (in shim, on the browser thread). Every key lands there first:

```
key → browser thread → ROUTER
        ├─ reserved shortcut (Ctrl+T/W/L, F11, Ctrl+Tab…) → consume → Rust
        ├─ UI-targeted (urlbar typing)                    → Rust
        └─ otherwise                                      → forward to renderer
```

- **Reserved shortcuts** intercepted **before the page** via `RenderWidgetHost::AddKeyPressEventCallback` (page can't steal Ctrl+T).
- **ESC / fullscreen** → page tries first → Rust as fallback (unhandled-key path).
- Reserved-shortcut table is a real artifact to define during build.

---

## 5. JS ↔ Rust Bridge

Crosses renderer↔browser process → inherently async.

```js
// async, Promise-based calls
const tabs = await browser.tabs.list();
await browser.navigate(tabId, url);

// Rust → JS events
browser.on('tabTitleChanged', cb);
```

- JS→Rust → Promises. Rust→JS → event push. No sync calls (freeze/deadlock risk).
- Transport: injected JS bindings (CEF-style), shim marshals to Rust.

---

## 6. UI / Content Layering

Child-view-in-hole (simple, CEF-style):
- UI React page fills the window.
- Active tab's native child view is positioned into the UI's "content area" rectangle.
- Router/Rust reparents + resizes the child view on tab switch / window resize.

---

## 7. Privacy (via Chromium APIs only)

Rust orchestrates existing content/network APIs through the shim:
- Network: proxy config, DoH, HTTPS-only mode
- Storage: cookie/cache partitioning, permissions
- Requests: interception/filtering via Chromium's request hooks
- Telemetry-off flags / privacy policies

No custom blocking engine. Configuration + orchestration only.

---

## 8. Build Integration

**gn/ninja owns the build and final link. Rust joins as a static lib.**

```
gn/ninja (master)
   ├─ builds Chromium + C++ shim (custom gn target)
   └─ consumes libbackend.a  ◄── cargo (crate-type = "staticlib")
                                  + bindgen on shim's C header
   → single browser binary
```

- Keep all additions in **new files / new gn targets** → minimal upstream edits → easy rebase.
- **Toolchain alignment** (pin once): Rust target triple, libc, Chromium clang/sysroot, Windows MSVC runtime.
- On rebase: re-sync upstream → re-apply small gn hooks → rebuild. Rust crate insulated behind the shim.

---

## 9. Dev Loop (expectations)

- First Chromium build: **huge** (hours). Then cached — Chromium won't recompile unless its source changes.
- **But** every Rust change forces a **full-binary relink** (slow on a monolith). This is the iteration tax.

**Mitigations (planned):**
1. **Standalone Rust harness** — unit-test/run backend logic without Chromium. Link into browser only when the real engine is needed. Biggest speedup.
2. **Component build** (`is_component_build=true`) for dev → fast links. Monolithic static for release.
3. **Stable shim API** → Rust-only edits skip C++ recompile.
4. **Fast linker** (mold/lld).

---

## 10. Phased Roadmap

**Phase 0 — Foundations**
- Fetch + build vanilla Chromium (`content_shell` as reference).
- Pin toolchain; get `is_component_build` dev config working.

**Phase 1 — Shim + Rust skeleton**
- Thin C++ shim over content; clean C header.
- `bindgen` bindings; Rust `staticlib`; custom gn target links it.
- Boot Chromium from Rust; open one hardcoded tab in windowed mode.

**Phase 2 — UI surface + bridge**
- UI WebContents renders a React page.
- Async JS↔Rust bridge (calls + events).
- Child-view-in-hole layering; resize handling.

**Phase 3 — Browser core**
- Tab model in Rust (create/close/switch, show/hide native views).
- Router + reserved-shortcut table.
- Navigation, title/url/load-state events to UI.

**Phase 4 — Privacy layer**
- Wire Chromium privacy APIs from Rust (DoH, HTTPS-only, partitioning, request filtering, telemetry-off).

**Phase 5 — Hardening + rebase drill**
- Nested-loop / focus / IME edge cases.
- Do one full Chromium rebase early to validate the additive-file strategy.

**Later (post-v1):** extensions, auto-update, mobile.

---

## 11. Top Risks

| Risk | Mitigation |
|---|---|
| Relink-dominated dev loop | Standalone Rust harness + component build |
| Rebase friction | Additive-only files; one early rebase drill |
| Toolchain mismatch (Rust↔clang/sysroot) | Pin & document once in Phase 0 |
| Native view lifecycle (content is more manual than CEF) | Absorb in shim; test resize/focus/close early |
| Page stealing reserved keys | Pre-page interception via key callback |
