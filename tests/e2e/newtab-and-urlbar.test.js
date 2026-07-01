import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser, navigateFromAddressBar, sleep, timeoutMs, waitFor, waitForTarget,
} from './helpers/browserHarness.js';

const addressBarValue = (cdp) =>
  cdp.evaluate(`document.querySelector('input[placeholder="Search or enter address..."]')?.value ?? 'NO-BAR'`);

// Foundation: the built-in new tab page actually loads (internal browser://newtab
// is served as the UI's newtab.html), and the address bar tracks the active tab's
// live URL — blank on the new tab page, the real URL after navigating.
test('new tab page loads and the address bar tracks navigation',
  { timeout: timeoutMs + 20000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      // The boot tab loads its page once the UI subscribes.
      await waitForTarget((t) => (t.url || '').includes('newtab.html'));
      // The new tab page shows an empty address bar.
      const barOnNewTab = await waitFor(browser.cdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value ?? 'NO-BAR'`,
        (v) => v === '', 8000);
      assert.equal(barOnNewTab, '');

      // Navigate via the real address bar (synthetic typing can miss keystrokes,
      // so retry until the tab commits the page).
      let navigated = false;
      for (let attempt = 0; attempt < 5 && !navigated; attempt++) {
        await navigateFromAddressBar(browser.cdp, 'http://127.0.0.1:5000/history.html');
        navigated = await waitForTarget((t) => (t.url || '').includes('history.html'), 3000)
          .then(() => true).catch(() => false);
      }
      assert.ok(navigated, 'address-bar navigation should load the page');

      // The address bar now reflects the navigated URL (protocol stripped).
      const barAfter = await waitFor(browser.cdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value ?? ''`,
        (v) => v.includes('history.html'), 8000);
      assert.match(barAfter, /history\.html/);
    } finally {
      await browser.close();
    }
  });
