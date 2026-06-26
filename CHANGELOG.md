# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project adheres to Semantic Versioning.

## [Unreleased]

### Added

- Strict cookie privacy enforcement that blocks third-party cookies, caps first-party cookies to 7 days, records policy actions, and documents the behavior.
- Biome-based linting and formatting commands for JavaScript and TypeScript sources across the repository.
- Release automation for converting the current `Unreleased` changelog entries into a versioned release section.
- A standalone `website/public/picture-in-picture-test.html` page for manually checking whether web video Picture-in-Picture works in the current browser build over HTTP.
- Site permission controls for automatic Picture-in-Picture, with per-origin allow/block behavior exposed in site settings.
- An auto-PiP-on-tab-switch probe in `website/public/picture-in-picture-test.html` for manually exercising the new automatic Picture-in-Picture allow/block behavior.

### Changed

- Repository guidance now requires coding agents to update this changelog after completing any task that changes behavior, fixes bugs, or adds features.

### Fixed

- Session cookies, including HttpOnly login cookies, now persist across browser restarts for disk-backed profiles.
- Pinned tabs now clear restored or stale favicons when a page reports no favicon instead of keeping the previous/default icon.
- Explicit close actions such as the tab context menu can now close pinned tabs instead of being blocked by the generic pinned-tab guard.
- Automatic Picture-in-Picture is now blocked by default instead of prompting, avoiding stale deferred PiP requests while still allowing explicit per-site opt-in.
- Automatic Picture-in-Picture tab-switch attempts now respect the default block even if they happen immediately after a user click that briefly leaves Chromium's transient user-activation flag set.
