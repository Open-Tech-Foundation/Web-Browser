import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  pressKey,
  timeoutMs,
  typeText,
  waitFor,
} from './helpers/browserHarness.js';

test('user can navigate to Settings from the address bar',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const cdp = browser.cdp;
      const addressSelector = 'input[placeholder="Search or enter address..."]';

      await waitFor(
        cdp,
        `!!document.querySelector(${JSON.stringify(addressSelector)})`,
        Boolean,
      );
      await clickSelector(cdp, addressSelector);
      await cdp.send('Input.dispatchKeyEvent', {
        type: 'keyDown',
        key: 'a',
        code: 'KeyA',
        modifiers: 2,
      });
      await cdp.send('Input.dispatchKeyEvent', {
        type: 'keyUp',
        key: 'a',
        code: 'KeyA',
        modifiers: 2,
      });
      await typeText(cdp, 'browser://settings');
      await pressKey(cdp, 'Enter');

      const state = await waitFor(
        cdp,
        `(() => {
          const address = document.querySelector(${JSON.stringify(addressSelector)})?.value || '';
          const tabText = [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
            .map((tab) => tab.textContent || '')
            .join(' ');
          return { address, tabText };
        })()`,
        (value) =>
          value.address.includes('browser://settings') ||
          /settings/i.test(value.tabText),
        15000,
      );

      assert.ok(
        state.address.includes('browser://settings') || /settings/i.test(state.tabText),
        `expected visible Settings navigation state, got ${JSON.stringify(state)}`,
      );
    } finally {
      await browser.close();
    }
  });
