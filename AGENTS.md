# Repository Guidelines

## Project Structure & Module Organization

This project is a CEF-based desktop browser with a React UI.

- `src/`: C++ browser process, CEF handlers, tab lifecycle, and native bridge code.
- `include/`: C++ headers and shared browser model definitions.
- `ui/src/`: React toolbar, settings, new tab, shared helpers, and styles.
- `ui/findbar/`: standalone React find bar entry point.
- `ui/assets/`: browser UI assets such as search engine icons.
- `third_party/cef/`: vendored CEF SDK and wrapper sources.
- `build/`: generated build output; do not edit manually.

There is no dedicated test directory yet. Add tests near the code they validate or introduce a clear `tests/` structure when adding coverage.

## Build, Test, and Development Commands

- `bun run setup`: installs or prepares project dependencies via `setup_deps.sh`.
- `bun run dev`: starts Vite on port `3000`, waits for it, then launches the CEF browser with the dev UI.
- `bun run build:ui`: builds React/Vite UI assets into `build/Release/ui`.
- `bun run build:cpp`: configures CMake with Ninja and builds the `otf-browser` executable.

Required local tools include Bun, CMake 3.21+, Ninja, GCC 14+, and the checked-in CEF SDK.

## Coding Style & Naming Conventions

C++ uses C++20. Keep native code focused and explicit; prefer small helpers for repeated CEF or JSON/event logic. Match existing naming: classes use `PascalCase`, methods use `PascalCase`, local variables use `snake_case`, and constants use `kName`.

React components use `PascalCase` filenames and exports, for example `AddressBar.jsx` and `SearchHero.jsx`. Shared JavaScript helpers live under `ui/src/shared/`. Keep UI state ownership clear: native C++ is authoritative for browser/tab state, React mirrors it.

## Testing Guidelines

At minimum, run `bun run build:ui` after UI changes and `bun run build:cpp` after C++ changes. For bug fixes, add regression coverage where practical. Priority areas are URL resolution, native JSON escaping, settings validation, and tab lifecycle behavior.

Use descriptive test names that state behavior, such as `resolveUrl_adds_https_for_domain_input`.

## Commit & Pull Request Guidelines

Recent history uses short Conventional Commit-style subjects:

- `feat: keyboard shortcuts with backend-driven actions`
- `feat: settings sync and stop-load button`

Use `feat:`, `fix:`, `docs:`, `refactor:`, or `test:` as appropriate. Keep commits scoped and avoid mixing unrelated C++ and UI refactors.

Pull requests should include a short summary, linked issue when relevant, verification commands run, and screenshots or screen recordings for visible UI changes.

## Security & Configuration Tips

Treat `browser://` pages, settings JSON, and native-to-React event payloads as security-sensitive. Prefer allowlists for internal pages and search engines. Never concatenate untrusted strings into JSON without escaping or structured serialization.
