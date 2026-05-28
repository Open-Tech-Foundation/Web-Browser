# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

```bash
bun run setup          # Install/prepare all dependencies (run once after clone)
bun run dev            # Start Vite dev server + launch browser with HMR
bun run build:ui       # Build React/Vite assets into build/Release/ui
bun run build:cpp      # CMake + Ninja build of the C++ engine
bun run build          # build:ui then build:cpp

bun run test:cpp       # Build and run all C++ unit tests via ctest
bun run test:js        # Run JS unit tests (search.test.js)
bun run test:e2e       # Run all e2e test suites
bun run test           # test:cpp + test:js

# Run a single e2e test file:
scripts/run-e2e.sh tests/e2e/findbar.test.js

# Run a named e2e group (ui / state / media / permissions / security):
bun scripts/run-e2e-suite.js --group ui

# Run a specific test by name:
bun scripts/run-e2e-suite.js --test findbar
```

**After any C++ change**, run `bun run build:cpp` to verify compilation before anything else.

## Architecture

This is a desktop browser built on **CEF (Chromium Embedded Framework)**. There are two distinct processes and two distinct rendering contexts:

### C++ process (`src/`)
- `otf_handler.cc` — the single large handler (~6500 lines). Implements all CEF callbacks: tab lifecycle, navigation, downloads, find, context menus, keyboard shortcuts, resource requests. Also contains the `OtfMessageRouterHandler` inner class that handles all `cefQuery` RPC calls from the UI.
- `otf_app.cc` — `CefApp` subclass; process startup, scheme registration, render-process message routing.
- `otf_browser_shell.h` / `otf_app.h` — `TabManager` (maps tab IDs ↔ CEF browsers) and `OtfApp` interface.
- `otf_store.cc` — SQLite-backed persistence (settings, history, bookmarks, workspace sessions, downloads).
- `otf_page_policy.cc` — Content security policy injection into frames.
- `otf_devtools_bridge.cc` — Routes async CDP responses back to per-call cefQuery callbacks.
- `otf_popup_overlay.cc` — Manages floating overlay browsers (blocked-popup prompt, QR, clear-site-data, etc.).

### React UI (`ui/src/`)
The UI runs in the **app-shell browser** (`ui_browser_`), a CEF browser whose view ID is `kUiBrowserViewId`. It is a completely separate browser from the content tabs.

- `App.jsx` — Root: owns tab/workspace state via `useReducer`, runs the persistent `subscribe-events` cefQuery subscription that receives all push events from C++.
- `components/` — `AddressBar`, `TabStrip`, `WorkspaceSwitcher`, `SecurityIconButton`.
- `findbar/FindBar.jsx` — Runs in its own CEF browser (`findbar_browser_`), has its own `findbar-subscribe` persistent subscription.
- Feature popups (`appmenu/`, `settings/`, `bookmarks/`, etc.) each run in their own overlay browser and have their own `subscribe`/`restore` cefQuery subscriptions.

### IPC model
**JS → C++**: `window.cefQuery({ request: '<verb>:<payload>' })` — synchronous RPC with `onSuccess`/`onFailure` callbacks. No built-in cancellation or sequencing.

**C++ → JS**: `SendEvent(json)` calls `callback->Success(json)` on a stored persistent subscription callback. Events carry a `key` field. Each overlay browser has its own subscription.

**Key subscriptions:**
| Subscription | Who uses it |
|---|---|
| `subscribe-events` | `App.jsx` — tab/workspace state, shortcuts |
| `findbar-subscribe` | `FindBar.jsx` — find results |
| `zoombar-subscribe` | Zoom indicator |
| `downloads-subscribe` | Downloads overlay |
| `certificate-subscribe` | Certificate overlay |
| `bookmark-subscribe` | Bookmark popup |
| `image-preview-subscribe` | Image preview tab |

### Content security architecture
- `browser://` scheme is the internal page scheme (newtab, settings, history, bookmarks, etc.).
- `tab-context-menu:<tabId>` is a synthetic link URL used by `TabStrip` to trigger native right-click menus without a separate IPC call.
- `OtfPagePolicy` injects CSP headers/meta into page frames.
- The `ui_browser_` and content tab browsers are entirely separate CEF browser instances; they do not share a frame tree.

### Test layout
- `tests/native/` — C++ unit tests (tab manager, URL policy, JSON bridge, store). Built and run via `bun run test:cpp`.
- `tests/e2e/` — Playwright-style JS tests driving the real browser binary via CDP. Manifest at `tests/e2e/e2e.manifest.json` groups tests into `ui`, `state`, `media`, `permissions`, `security`. E2e tests must use synthetic mouse/keyboard events — never JS `.click()` or direct cefQuery shortcuts.
- `ui/src/shared/search.test.js` — URL resolution unit tests.

## Conventions

- C++20; `PascalCase` for classes/methods, `snake_case` for locals, `kName` for constants.
- React components in `PascalCase` filenames; shared JS helpers under `ui/src/shared/`.
- C++ is authoritative for all browser/tab state; React only mirrors it.
- Commit style: `feat:`, `fix:`, `refactor:`, `test:`, `chore:` — short subject, no Co-Authored-By lines.
- Never commit without explicit user approval; never push.
