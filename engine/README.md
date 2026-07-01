# otf engine — Rust backend over Chromium's content layer

The new browser backend (replacing CEF). A single Rust control process drives
Chromium's **content layer** through a thin C++ shim. See `../plan.md` for the
locked architecture and `../MIGRATION-bridge-map.md` for the UI bridge contract
this backend must reproduce.

```
engine/
  BUILD.gn     additive gn target -> //otf:otf_browser in the Chromium tree
  shim/        hand-written C++ shim + the C FFI header (bridge.h)
  backend/     Rust control brain (staticlib): tab model, bridge dispatch
  gn/          dev args.gn (copied into out/otf by bootstrap)
  scripts/     bootstrap (fetch latest Chromium) + build
```

## FFI boundary

`shim/bridge.h` is the single, deliberately broad/stable C header (plan.md §3).
`bindgen` turns it into Rust `extern "C"` decls; `backend/` wraps them safely.
Keeping it stable means Rust-only changes skip the C++ recompile/relink.

The shim has two build modes:
- **standalone (default):** no Chromium needed — the backend's tab model and
  bridge routing build and unit-test on their own (plan.md §9). The biggest
  dev-loop win.
- **`OTF_WITH_CONTENT`:** the real implementation over content, compiled by the
  gn target inside the Chromium checkout.

## Dev loop

```bash
# fast inner loop — pure backend logic, no Chromium:
cd engine/backend && cargo test

# full browser (one-time multi-hour, ~100GB+):
engine/scripts/bootstrap-chromium.sh   # fetch LATEST chromium + gn gen
engine/scripts/build-engine.sh         # single ninja build (in-tree rust + shim)
../chromium/src/out/otf/otf_browser
```

## Status (phased — plan.md §10)

- [x] **Phase 1 skeleton:** shim header + stub, Rust staticlib, bindgen wiring,
      gn target, bootstrap/build scripts. `cargo test` green.
- [x] **Phase 2 (UI surface + bridge + own embedder) — done:**
  - [x] Build actually links: bindgen points at Chromium's libclang + clang
        resource headers; `//otf:otf_browser` wired into the root `gn_all` so gn
        loads it. First full build (stub shim) validates the toolchain.
  - [x] Rust bridge transport live: `bridge.rs` parses the `{id,method,params}`
        envelope → `ok`/`error`/`unknown_method` responses + event envelopes;
        `backend.rs` builds the real `OtfCallbacks` table whose FFI trampolines
        route title/url/load events into the tab model and answer JS calls.
  - [x] Content boot (first light): `otf_browser` boots content from Rust via
        `ShellMainDelegate` + `ContentMain` (reusing `content_shell_lib`) and
        renders a real browser window. Verified end to end via CDP screenshot.
        Toolchain aligned to Chromium's rustc so the bundled std doesn't clash.
  - [x] Bridge bindings injected into the UI renderer (live JS↔Rust round-trip):
        a Mojo interface (`otf::mojom::BridgeHost`/`BridgeClient`) wired through
        otf's Content{Browser,Renderer}Client + a RenderFrameObserver that
        installs `window.otf` via gin. The real React chrome now boots against
        the Rust backend (workspaces/tabs come from the backend, not CEF).
  - [x] Clean grouped FFI: the Rust→Chromium surface is grouped interface tables
        (`OtfLifecycleApi`/`OtfUiApi`/`OtfTabsApi`/`OtfBridgeApi`) aggregated into a
        versioned `OtfApi` returned by `otf_api()`; Rust drives it via a safe
        `ffi::Api` facade. New areas (cookies/network/gpu) slot in cleanly.
  - [x] Tabs render real pages: `otf_tab_host.cc` hosts a `content::WebContents`
        per tab (caller-assigned ids), parented into the UI window below the
        chrome; `navigation.tab`/back/forward/reload/stop + title/url/load events
        are live. Verified via CDP (a navigated tab renders in its own target).
  - [x] In-tree Rust backend: the staticlib is now a gn `rust_static_library`
        (`//otf:otf_backend`) with `rust_bindgen` (`//otf:otf_bridge_bindgen`) on
        bridge.h, depending on vendored `//third_party/rust/serde_json/v1`. It
        shares Chromium's single Rust std, so the cargo staticlib copy, the
        manual `-lotf_backend`/`inputs` wiring, and `--allow-multiple-definition`
        are all gone. cargo stays only for the standalone `cargo test` loop (the
        backend's `ffi` module switches binding source via the `in-tree` feature).
  - [x] Cross-OS window seam: `OtfPlatformWindow` (otf_platform_window.h) is the
        only interface the shim uses for the OS window; all aura/views code lives
        in the desktop backend `otf_window_aura.cc` (selected by `use_aura` in
        BUILD.gn). macOS (Cocoa) / Android (surface) add their own backend file.
        Ozone defaults to **Wayland** when a compositor is present, else X11.
  - [x] **content_shell fully dropped — own embedder, no `testonly`.** otf now
        boots on its own `content::ContentMainDelegate` / `ContentBrowserClient` /
        `ContentRendererClient` / `BrowserMainParts`, its own `BrowserContext`
        (shared by UI + tabs) and its own top-level window, with its own resource
        pack (`otf.pak`, a `repack()` target). The binary links zero content_shell
        libs. Verified on Wayland: boots, UI renders, no crashes.

Run it: `out/otf/otf_browser <url>` (omit url → `browser://newtab`). Build:
`bun run build:engine` (single ninja: rust_bindgen + rust_static_library + shim +
otf.pak). Component build, so run from `out/otf` (needs the .so set). Wayland is
auto-selected; force a backend with `--ozone-platform=x11|wayland`.

- [ ] **Phase 3:** full tab model, input router + reserved-shortcut table, more
      RPC breadth (most namespaces still resolve `Deferred`) — at parity with the
      bridge map.
  - [x] **`browser://` internal-page scheme** (`otf_internal_url_loader_factory.{h,cc}`):
        registered as a standard/secure scheme (`OtfContentClient::AddAdditionalSchemes`)
        and served from the UI asset dir (`--otf-ui-dir`, else `<DIR_ASSETS>/ui`)
        for navigations + subresources. `browser://shell`→`index.html`,
        `browser://<page>`→`<page>.html`, shared `/assets/…`. Production internal
        pages now load with no dev server; dev still redirects tabs to the dev
        origin (`tests/e2e/internal-scheme.test.js`).
  - [x] **Popup overlays** (`otf_popup_overlay.{h,cc}` + `ui.popup.*`): named
        popups render in their own **transparent** overlay WebContents (`<name>.html`)
        layered over the window, so they float over the chrome/page instead of the
        old opaque child window. Common close contract: ESC + close button
        (`Popup.jsx`) and **click-outside** (an aura pre-target handler in the
        window backend → `on_popup_closed`). `show`/`hide`/`toggle` track open state
        (`tests/e2e/popup-overlay.test.js`). Placement uses per-name defaults for
        now (anchor rects can be passed later).
  - [x] **Workspaces:** the backend owns a workspace model (`workspaces.list/
        create/rename/delete/switch`); tabs belong to a workspace and `tabs.list`/
        `tabs.active`/subscribe replay are scoped to the active one, emitting
        `workspaces-updated`/`workspace-changed` (`tests/e2e/workspaces-model.test.js`,
        cargo tests). Persistence lands later (external Rust embeddable DB).
  - [x] **Bridge trust gate** (`otf_trust.{h,cc}`): only otf's own internal UI
        frames may use the JS<->Rust bridge. Trusted = the internal `browser://`
        scheme, or (dev only) the resolved UI origin (forwarded to renderers via a
        switch). Enforced authoritatively in the browser (gates the mojo bind) and
        in the renderer (gates whether `window.otf` is installed). Web content is
        denied (`tests/e2e/bridge-trust.test.js`).
  - [x] **DevTools http handler re-wired** (`otf_devtools.{h,cc}`): otf provides a
        minimal `DevToolsManagerDelegate` + starts the CDP TCP server on
        `--remote-debugging-port` (loopback), so DevTools/e2e can attach again
        (content_shell used to do this). `--dev-ui-url` now selects the UI URL and
        `GetProduct()` names the browser `OTF/…`. e2e harness is live again.
  - [x] **Window-resize reflow:** `OtfWindowAura::OnWidgetBoundsChanged` reflows
        the shown page tab (a child aura window) into the content region on resize
        (covered by `tests/e2e/window-resize.test.js`).
- [ ] **Foundation for app features:** SQLite for history/bookmarks + session
      persistence/restore (back/forward UX builds on this).
- [ ] **Phase 4:** privacy APIs (DoH, HTTPS-only, partitioning, request filter).
- [ ] **Cross-OS:** Windows (shares Desktop Aura), then macOS (Cocoa backend),
      then Android (surface) — only the OtfPlatformWindow backend changes.

`TODO(...)` markers in the shim flag the integration points still to fill in.
