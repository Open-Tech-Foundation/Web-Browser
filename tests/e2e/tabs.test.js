import test from 'node:test';
import assert from 'node:assert/strict';

import { launchDevBrowser, timeoutMs, waitFor } from './helpers/browserHarness.js';

const tabCountExpression = `document.querySelectorAll('a[href^="tab-context-menu:"]').length`;

test('user can open and close a tab from the tab strip',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const cdp = browser.cdp;
      const initialCount = await waitFor(
        cdp,
        tabCountExpression,
        (count) => count >= 1,
      );

      const clickedNewTab = await cdp.evaluate(`
        (() => {
          const button = document.querySelector('button[aria-label="New tab"], button[title="New tab"]');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedNewTab, true);

      await waitFor(
        cdp,
        tabCountExpression,
        (count) => count === initialCount + 1,
      );

      const closedNewTab = await cdp.evaluate(`
        (() => {
          const tabs = [...document.querySelectorAll('a[href^="tab-context-menu:"]')];
          const newest = tabs[tabs.length - 1];
          const close = newest?.querySelector('button[title="Close tab"]');
          if (!close) return false;
          close.click();
          return true;
        })()
      `);
      assert.equal(closedNewTab, true);

      const finalCount = await waitFor(
        cdp,
        tabCountExpression,
        (count) => count === initialCount,
      );
      assert.equal(finalCount, initialCount);
    } finally {
      await browser.close();
    }
  });
