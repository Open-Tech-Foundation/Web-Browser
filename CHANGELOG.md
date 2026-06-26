# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project adheres to Semantic Versioning.

## [Unreleased]


## [0.1.0-alpha.53] - 2026-06-26

### Added

- Windows release CI now runs a smoke test that launches the freshly built `otf-browser.exe` and verifies it stays alive for 10 seconds before packaging. Catches renderer startup crashes (CRT mismatch, missing DLLs, sandbox config) that unit tests cannot detect.

### Fixed

- Windows production builds no longer crash on launch. The root cause was a C runtime linker mismatch: CEF's cmake adds `/MT` (static CRT) to compiler flags, but CMake's default `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL` caused the linker to link against `MSVCRT.lib` (dynamic CRT). Object files compiled with `/MT` but linked against the dynamic CRT caused `STATUS_ACCESS_VIOLATION` in the renderer process. The fix sets `CMAKE_MSVC_RUNTIME_LIBRARY` to `MultiThreaded` so the linker also uses `LIBCMT.lib`. Additionally, the erroneous `CEF_USE_BOOTSTRAP` define (auto-added by CEF's cmake for the bootstrap DLL mechanism, which this project does not use) is now stripped, and `SET_LPAC_ACLS` is called for CEF 147 Windows LPAC support (CEF issue #3791).

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
