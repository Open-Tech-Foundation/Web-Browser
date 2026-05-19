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

`history.test.js` verifies a user-visible history path:

- enable history from Settings > Privacy
- visit a test-served local web page through the address bar
- open History through the address bar
- observe the web page appears and `browser://settings` does not

`bookmarks.test.js` verifies a user-visible bookmark path:

- visit a test-served local web page through the address bar
- click the visible bookmark star
- open Bookmarks through the address bar
- observe the bookmark appears
- delete the bookmark through the Bookmarks UI

`workspaces.test.js` verifies the workspace popup:

- open the workspace popup from the visible shell control
- create a new workspace from the popup
- switch to it by clicking its row
- observe the shell workspace label changes

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

1. `security.test.js`: external web content cannot use privileged bridge commands.

Do not merge multiple feature areas into one E2E file. If a behavior can be
tested in C++ without launching the browser, keep it in native tests instead.
