# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **Migration status.** This browser runs on a Rust backend driving Chromium's
> **content** layer via a thin C++ shim. CEF is gone, and as of the de-Shell
> (branch `feat/rust-content-backend`) **content_shell is gone too**: otf has its
> own embedder (ContentMainDelegate/ContentBrowserClient/RendererClient +
> BrowserMainParts), its own BrowserContext, its own top-level window (behind the
> cross-OS `OtfPlatformWindow` seam), and its own resource pack (`otf.pak`); the
> binary is no longer `testonly`. Cross-OS is a first-class goal — keep all
> toolkit code behind `OtfPlatformWindow`. Ozone defaults to Wayland (X11
> fallback). Source of truth: `plan.md` (architecture) and `MIGRATION-bridge-map.md`
> (the UI↔backend bridge contract). The backend lives in `engine/` — see
> `engine/README.md` for status and the dev loop.

## Commands

```bash
bun run setup          # Fetch + configure the Chromium checkout (engine/scripts/bootstrap-chromium.sh)
bun run dev            # Start Vite dev server + launch the engine browser with HMR
bun run build:ui       # Build React/Vite assets into build/Release/ui
bun run build:engine   # gn/ninja build of otf_browser (in-tree rust_static_library + shim)
bun run build          # build:ui then build:engine

bun run test:engine    # Standalone Rust backend tests (no Chromium needed)
bun run test:js        # Run JS unit tests (search.test.js)
bun run test:e2e       # Run all e2e test suites
bun run test           # test:engine + test:js

# Run a single e2e test file:
scripts/run-e2e.sh tests/e2e/findbar.test.js

# Run a named e2e group (ui / state / media / permissions / security):
bun scripts/run-e2e-suite.js --group ui
```

**Fast inner loop:** `cd engine/backend && cargo test` exercises the backend
logic without a Chromium tree. The full browser needs the Chromium checkout
(`bun run setup`, one-time multi-hour build).

## Architecture

One Rust process is the control brain; Chromium's content layer owns the event
loop. The UI is a React app rendered by Chromium as its own WebContents.

### Engine (`engine/`)
- `shim/bridge.h` — the single, stable C FFI boundary (lifecycle, UI surface,
  tabs, async JS↔Rust bridge, content callbacks). `bindgen` turns it into Rust.
- `shim/bridge.cc` — thin C++ shim over content (standalone stub + `OTF_WITH_CONTENT`).
- `backend/` — Rust staticlib: tab model, navigation, bridge dispatch, privacy
  orchestration. Authoritative for all browser/tab state.
- `gn/` — additive `//otf` gn target + dev args; symlinked into the Chromium tree.

### React UI (`ui/`)
The UI renders in a content WebContents, separate from the page tabs.
- `ui/src/App.jsx` — Root: owns tab/workspace state via `useReducer`, subscribes
  to backend push events.
- `ui/src/shared/bridge.js` — **the only seam to the backend.** `call(method,
  params)` for RPC and `subscribe(method, params, onEvent)` for event streams,
  over a swappable transport (`setTransport`). No UI code talks to the backend
  any other way.
- `ui/src/components/` — `AddressBar`, `TabStrip`, `WorkspaceSwitcher`, etc.
- Overlay apps (`ui/findbar/`, `ui/console/`, `ui/zoombar/`, `ui/src/appmenu/`,
  `ui/src/settings/`, …) each render in their own overlay WebContents with their
  own `*.subscribe` stream.

### IPC model (async — see MIGRATION-bridge-map.md)
- **JS → backend:** `bridge.call(method, params)` → Promise. Wire envelope
  `{ id, method, params }` ↔ `{ id, ok, result | error }`.
- **backend → JS:** event push over a persistent `subscribe`, each event an
  object keyed by `key`. Reserved shortcuts arrive as `{ key: 'shortcut', value }`.
- ~130 RPC methods across namespaces (`tabs.*`, `navigation.*`, `ui.*`, …) and
  ~35 event keys; the full surface is in `MIGRATION-bridge-map.md`.

### Content security architecture
- `browser://` is the internal page scheme (newtab, settings, history, …).
- Only internal UI frames may call the bridge; web content is denied (a small
  content-permission whitelist excepted). Reproduced in the shim's input router.

### Test layout
- `engine/backend` — Rust unit tests (`cargo test`).
- `tests/e2e/` — JS tests driving the real browser binary via CDP. Use synthetic
  mouse/keyboard events — never JS `.click()` or direct bridge shortcuts.
- `ui/src/shared/search.test.js` — URL resolution unit tests.

## Conventions

- Rust backend; C++ shim is thin C++20. Keep the shim FFI surface broad/stable so
  Rust-only changes skip the C++ recompile.
- C++: `PascalCase` classes/methods, `snake_case` locals, `kName` constants.
- React components in `PascalCase` filenames; shared JS helpers under `ui/src/shared/`.
- The Rust backend is authoritative for all browser/tab state; React only mirrors it.
- Commit style: `feat:`, `fix:`, `refactor:`, `test:`, `chore:` — short subject, no Co-Authored-By lines.
- Never commit without explicit user approval; never push.
