# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project adheres to Semantic Versioning.

## [Unreleased]

### Added

- Page context menu, drawn as the browser's own overlay. Right-clicking a page shows a transparent, rounded menu built from the engine's hit-test: link actions (open in new tab, copy link address; copy email for mailto links), image actions (copy/save), editable-field actions (undo/redo/cut/copy/paste/select-all, gated by capability), selection actions (search the selection via the configured engine, copy), and reload on a bare page. Page edits and image actions run through the content layer; link/selection copying uses the clipboard; selection search goes through the backend resolver. No view-source / dev-tools items are offered (browser policy). Esc, an outside click, or picking an item dismisses it.
- Real `browser://` internal-page scheme. Internal pages (the shell, new tab, settings, history, …) are now served over a registered, standard, secure `browser://` scheme from the built UI assets, so they work in a packaged build with no dev server. The scheme is handled by a dedicated URL loader factory for both navigations and subresources; in dev the UI still loads over the dev server.

### Changed

- Workspaces now have fully isolated data: each workspace is its own profile-like storage context, so cookies, cache, and site storage never leak between workspaces (a cookie set in one workspace is invisible to the same site in another). Browser data lives under a stable user-data root — `$HOME/.otf-browser` in production, `$HOME/.otf-browser-dev` in dev, or an explicit `--user-data-dir` — with the UI shell in a dedicated `system` profile and each workspace under `workspaces/<id>/`.

### Fixed

- The built-in new tab page now actually loads. Opening a tab (and the initial boot tab) navigates its WebContents to the internal `browser://newtab` page. In production it is served natively over `browser://`; in dev it is redirected to the dev server's `newtab.html`, with the address bar keeping the `browser://` form.
- The address bar now tracks the active tab's live URL. Per-tab title/URL/loading updates were emitted in a shape (`tabUrlChanged { tabId, url }`) the UI never consumed; they are now sent as the `{ id, key, value }` property deltas the UI applies, so the URL bar, title, and loading spinner update during navigation. The new tab page keeps an empty address bar.

### Added

- Modern popup/overlay subsystem: named popups (workspace switcher, split menu, blocked-popup prompt, etc.) now render in their own transparent overlay WebContents layered over the window instead of the previous opaque child window, so they float over the chrome and page. Every popup shares a common close contract — Esc, an in-popup close button, and click-outside-to-dismiss (handled in the window backend) — driven by `ui.popup.show`/`hide`/`toggle`, which also tracks open state.
- Workspaces are now backed by the Rust engine: create, rename, delete, and switch workspaces, each owning its own set of tabs. `tabs.list`/`tabs.active` and the subscribe replay are scoped to the active workspace, and workspace changes emit `workspaces-updated`/`workspace-changed`. Duplicate workspace names and deleting the last workspace are rejected. (On-disk persistence is deferred to the upcoming embedded store.)
- Re-wired the DevTools/CDP remote-debugging HTTP handler that was dropped when content_shell was removed. The engine now provides a minimal `DevToolsManagerDelegate` and starts a loopback CDP server on `--remote-debugging-port`, so DevTools and the CDP-driven e2e suite can attach to the UI and tab targets again. `--dev-ui-url=<url>` selects the UI URL and the browser identifies itself as `OTF/0.1` in `/json/version`.
- Bridge trust gate: the JS↔Rust bridge (`window.otf`) is now exposed only to otf's own internal UI frames — the internal `browser://` scheme, or (in dev) the resolved UI origin. Web content is denied both in the renderer (no `window.otf` installed) and authoritatively in the browser (the mojo interface bind is rejected). Covered by a new `tests/e2e/bridge-trust.test.js`.

### Fixed

- Page tab content now tracks the window on resize. The page tab is a child window layered over the content region and was not following the top-level window; it is now reflowed into the content region whenever the window bounds change. Covered by a new `tests/e2e/window-resize.test.js`.


## [0.1.0-alpha.56] - 2026-06-27

### Added

- Cookie policy rows now show parsed blocked `Set-Cookie` domain/path details and include an allow action for third-party cookie origins.

### Changed

- Third-party cookie blocking now runs through the browser's own per-origin policy filter instead of Chromium's global preference so site-data exceptions can take effect.

## [0.1.0-alpha.55] - 2026-06-27

### Fixed

- Capped HttpOnly cookies are now flushed after the 7-day expiry rewrite so login cookies remain available after browser restart.
- The tab context menu can now close an inactive pinned tab without switching away from the current tab.

## [0.1.0-alpha.54] - 2026-06-26

## [0.1.0-alpha.53] - 2026-06-26

### Added

- Windows release CI now runs a smoke test that launches the freshly built `otf-browser.exe` and verifies it stays alive for 10 seconds before packaging. Catches renderer startup crashes (CRT mismatch, missing DLLs, sandbox config) that unit tests cannot detect.

### Fixed

- Windows production builds no longer crash on launch. The root cause was twofold: (1) the CEF sandbox was incorrectly configured — starting with M138, `cef_sandbox.lib` is no longer distributed and the officially supported way to build a standalone EXE is `USE_SANDBOX=OFF` in cmake (previously `USE_SANDBOX=ON` was left as default, which added the `CEF_USE_BOOTSTRAP` define meant for DLL+bootstrap.exe apps); (2) a C runtime linker mismatch — CEF's cmake adds `/MT` (static CRT) to compiler flags, but CMake's default `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL` caused the linker to link against `MSVCRT.lib` (dynamic CRT). The fix sets `USE_SANDBOX=OFF` before `find_package(CEF)`, and `CMAKE_MSVC_RUNTIME_LIBRARY` to `MultiThreaded` (plus an explicit `MSVC_RUNTIME_LIBRARY` target property) so the linker uses `LIBCMT.lib` consistently. See [CEF sandbox setup docs](https://chromiumembedded.github.io/cef/sandbox_setup).

## [0.1.0-alpha.52] - 2026-06-26

## [0.1.0-alpha.51] - 2026-06-26

## [0.1.0-alpha.50] - 2026-06-26

### Added

- Strict cookie privacy enforcement that blocks third-party cookies, caps first-party cookies to 7 days, records policy actions, and documents the behavior.
- Main-frame navigation now strips common tracking query parameters before requests are sent.
- Biome-based linting and formatting commands for JavaScript and TypeScript sources across the repository.
- Release automation for converting the current `Unreleased` changelog entries into a versioned release section.
- A standalone `website/public/picture-in-picture-test.html` page for manually checking whether web video Picture-in-Picture works in the current browser build over HTTP.
- A standalone `website/public/storage-access-test.html` page for manually reproducing Storage Access API requests and permission outcomes over HTTP.
- Site permission controls for automatic Picture-in-Picture, with per-origin allow/block behavior exposed in site settings.
- An auto-PiP-on-tab-switch probe in `website/public/picture-in-picture-test.html` for manually exercising the new automatic Picture-in-Picture allow/block behavior.
- A new internal `browser://apitest` page that probes whether `Gamepad`, `Bluetooth`, `USB`, `XR`, `PaymentRequest`, `Credentials`, and `WebTransport` are exposed to page JavaScript in the current browser build.

### Changed

- Repository guidance now requires coding agents to update this changelog after completing any task that changes behavior, fixes bugs, or adds features.

### Fixed

- Windows production builds no longer crash on launch. The Chromium renderer process was repeatedly terminating with `STATUS_ACCESS_VIOLATION` due to a C runtime library mismatch: CEF's cmake forces `/MT` (static CRT) but vcpkg was building SQLite3 with `/MD` (dynamic CRT) via the default `x64-windows` triplet. The fix switches the vcpkg triplet to `x64-windows-static` so all libraries use `/MT` consistently. Additionally, `SET_LPAC_ACLS` is now called on the executable target to grant the LPAC SID read/execute access required by CEF 147's Windows sandbox support (CEF issue #3791).
- Session cookies, including HttpOnly login cookies, now persist across browser restarts for disk-backed profiles.
- Pinned tabs now clear restored or stale favicons when a page reports no favicon instead of keeping the previous/default icon.
- Explicit close actions such as the tab context menu can now close pinned tabs instead of being blocked by the generic pinned-tab guard.
- Automatic Picture-in-Picture is now blocked by default instead of prompting, avoiding stale deferred PiP requests while still allowing explicit per-site opt-in.
- Automatic Picture-in-Picture tab-switch attempts now respect the default block even if they happen immediately after a user click that briefly leaves Chromium's transient user-activation flag set.
- Alloy permission handling now accepts Storage Access API prompts so pages like YouTube stop failing `requestStorageAccessFor(...)` by default.
