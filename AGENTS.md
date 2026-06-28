# Repository Guidelines

## Project Structure & Module Organization

This project is a desktop browser on Chromium's content layer, driven by a Rust
backend, with a React UI. (Migrated off CEF — see `plan.md` and `engine/README.md`.)

- `engine/shim/`: thin C++ shim + the C FFI header (`bridge.h`) over content.
- `engine/backend/`: Rust control brain (tab model, bridge dispatch) as a staticlib.
- `engine/gn/`: additive gn target + dev args for the Chromium build.
- `ui/src/`: React toolbar, settings, new tab, shared helpers (incl. `bridge.js`), styles.
- `ui/findbar/`, `ui/console/`, etc.: standalone React overlay entry points.
- `ui/assets/`: browser UI assets such as search engine icons.
- `../chromium/`: the Chromium checkout (outside this repo; built via `engine/scripts`).

There is no dedicated test directory yet. Add tests near the code they validate or introduce a clear `tests/` structure when adding coverage.

## Build, Test, and Development Commands

- `bun run setup`: fetches + configures the Chromium checkout via `engine/scripts/bootstrap-chromium.sh`.
- `bun run dev`: starts Vite on port `3000`, waits for it, then launches the engine browser with the dev UI.
- `bun run build:ui`: builds React/Vite UI assets into `build/Release/ui`.
- `bun run build:engine`: builds the Rust staticlib and links the `otf_browser` binary via gn/ninja.
- `bun run test:engine`: runs the standalone Rust backend tests (no Chromium needed).

Required local tools include Bun, Rust/cargo, Ninja, and depot_tools for the Chromium build.

## Coding Style & Naming Conventions

Rust is the backend; the C++ shim is thin C++20 over content. Keep the shim's FFI surface broad and stable so Rust-only changes avoid a C++ recompile. C++ naming: classes/methods `PascalCase`, locals `snake_case`, constants `kName`.

React components use `PascalCase` filenames and exports, for example `AddressBar.jsx` and `SearchHero.jsx`. Shared JavaScript helpers live under `ui/src/shared/`. The UI talks to the backend only through `ui/src/shared/bridge.js`. Keep UI state ownership clear: the Rust backend is authoritative for browser/tab state, React mirrors it.

## Testing Guidelines

At minimum, run `bun run build:ui` after UI changes and `bun run test:engine` after backend changes. For bug fixes, add regression coverage where practical. Priority areas are URL resolution, native JSON escaping, settings validation, and tab lifecycle behavior.

Use descriptive test names that state behavior, such as `resolveUrl_adds_https_for_domain_input`.

## Commit & Pull Request Guidelines

Recent history uses short Conventional Commit-style subjects:

- `feat: keyboard shortcuts with backend-driven actions`
- `feat: settings sync and stop-load button`

Use `feat:`, `fix:`, `docs:`, `refactor:`, or `test:` as appropriate. Keep commits scoped and avoid mixing unrelated C++ and UI refactors.

Pull requests should include a short summary, linked issue when relevant, verification commands run, and screenshots or screen recordings for visible UI changes.

- **Never auto commit:** Always ask for permission before making a commit.
- **Never code push:** Never perform code pushes.

## Changelog Maintenance

Maintain the root `CHANGELOG.md` using the `Keep a Changelog` style sections already present there.

- After completing any task that changes behavior, fixes a bug, adds a feature, or makes a user-visible/internal workflow change worth tracking, update the current `## [Unreleased]` section before finishing.
- Coding agents must make this changelog update as part of the same task.
- Keep entries concise and place them under the most appropriate section such as `Added`, `Changed`, or `Fixed`.

## Security & Configuration Tips

Treat `browser://` pages, settings JSON, and native-to-React event payloads as security-sensitive. Prefer allowlists for internal pages and search engines. Never concatenate untrusted strings into JSON without escaping or structured serialization.
