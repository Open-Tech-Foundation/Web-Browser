import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

const addressSelector = 'input[placeholder="Search or enter address..."]';
const tabCountExpression = `document.querySelectorAll('a[href^="tab-context-menu:"]').length`;

async function pressShortcut(cdp, key, code, windowsVirtualKeyCode, modifiers = 2) {
  await cdp.send('Input.dispatchKeyEvent', {
    type: 'rawKeyDown',
    key,
    code,
    windowsVirtualKeyCode,
    nativeVirtualKeyCode: windowsVirtualKeyCode,
    modifiers,
  });
  await cdp.send('Input.dispatchKeyEvent', {
    type: 'keyUp',
    key,
    code,
    windowsVirtualKeyCode,
    nativeVirtualKeyCode: windowsVirtualKeyCode,
    modifiers,
  });
}

test('Ctrl+L focuses the address bar',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      await waitFor(browser.cdp, `!!document.querySelector(${JSON.stringify(addressSelector)})`, Boolean);

      await pressShortcut(browser.cdp, 'l', 'KeyL', 76);

      const focused = await waitFor(
        browser.cdp,
        `(() => {
          const input = document.querySelector(${JSON.stringify(addressSelector)});
          return document.activeElement === input;
        })()`,
        Boolean,
        15000,
      );
      assert.equal(focused, true);
    } finally {
      await browser.close();
    }
  });

test('Ctrl+T opens a tab and Ctrl+W closes the active tab',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const initialCount = await waitFor(
        browser.cdp,
        tabCountExpression,
        (count) => count >= 1,
      );

      await pressShortcut(browser.cdp, 't', 'KeyT', 84);
      await waitFor(
        browser.cdp,
        tabCountExpression,
        (count) => count === initialCount + 1,
        15000,
      );

      await pressShortcut(browser.cdp, 'w', 'KeyW', 87);
      const finalCount = await waitFor(
        browser.cdp,
        tabCountExpression,
        (count) => count === initialCount,
        15000,
      );
      assert.equal(finalCount, initialCount);
    } finally {
      await browser.close();
    }
  });
