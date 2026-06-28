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
engine/scripts/build-engine.sh         # cargo staticlib -> ninja link
../chromium/src/out/otf/otf_browser
```

## Status (phased — plan.md §10)

- [x] **Phase 1 skeleton:** shim header + stub, Rust staticlib, bindgen wiring,
      gn target, bootstrap/build scripts. `cargo test` green.
- [~] **Phase 2 (in progress):**
  - [x] Build actually links: bindgen points at Chromium's libclang + clang
        resource headers; `//otf:otf_browser` wired into the root `gn_all` so gn
        loads it. First full build (stub shim) validates the toolchain.
  - [x] Rust bridge transport live: `bridge.rs` parses the `{id,method,params}`
        envelope → `ok`/`error`/`unknown_method` responses + event envelopes;
        `backend.rs` builds the real `OtfCallbacks` table whose FFI trampolines
        route title/url/load events into the tab model and answer JS calls.
  - [ ] Content boot (first light): boot content from Rust via `ShellMainDelegate`
        + `ContentMain` so a window shows the UI URL (reuses `content_shell_lib`).
  - [ ] Our own UI WebContents renders the React app; child-view-in-hole layering.
  - [ ] Bridge bindings injected into the UI renderer (live JS↔Rust round-trip).
- [ ] **Phase 3:** full tab model, input router + reserved-shortcut table,
      navigation/title/url/load events — at parity with the bridge map.
- [ ] **Phase 4:** privacy APIs (DoH, HTTPS-only, partitioning, request filter).

`TODO(content)` / `TODO(phaseN)` mark the integration points still to fill in.
