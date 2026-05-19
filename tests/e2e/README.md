# E2E Browser Tests

Keep this suite intentionally small and expand it one browser feature at a time.

## Current Minimum

`dev-browser-smoke.test.js` verifies only that:

- Vite dev UI starts.
- The real built CEF browser launches with `--dev-ui-url`.
- Chrome DevTools Protocol is reachable.
- The React shell page exposes `#root`.

The first feature test is `tabs.test.js`. It uses only visible UI controls:

- click the tab strip's "New tab" button
- observe the rendered tab count increase
- click the new tab's close button
- observe the rendered tab count return

`navigation.test.js` verifies address-bar navigation through visible UI:

- click the address bar
- type `browser://settings`
- press Enter
- observe visible Settings state in the toolbar/tab strip

`settings.test.js` verifies one Settings-page interaction:

- click the visible Settings toolbar button
- click the Appearance section
- choose Light mode
- observe the Settings page marks Light selected and leaves dark mode

Run the current E2E suite with:

```bash
bun run test:e2e
```

On headless Linux, run through Xvfb:

```bash
xvfb-run -a bun run test:e2e
```

## Expansion Order

Add one file per feature, keeping each scenario narrow:

1. `history.test.js`: web page is recorded, internal UI is not.
2. `bookmarks.test.js`: add/remove one bookmark and verify persistence.
3. `workspaces.test.js`: create/switch/delete one workspace.
4. `security.test.js`: external web content cannot use privileged bridge commands.

Do not merge multiple feature areas into one E2E file. If a behavior can be
tested in C++ without launching the browser, keep it in native tests instead.
