import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { pressShortcut as pressNativeShortcut } from './helpers/e2eUtils.js';

const addressSelector = 'input[placeholder="Search or enter address..."]';
const tabCountExpression = `document.querySelectorAll('a[href^="tab-context-menu:"]').length`;

async function pressShortcut(cdp, key, code, windowsVirtualKeyCode, modifiers = 2) {
  await pressNativeShortcut(cdp, key, code, windowsVirtualKeyCode, modifiers);
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

test('shortcuts can open history and downloads pages',
  { timeout: timeoutMs + 20000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><title>Shortcut Bookmark E2E</title><main>Shortcut Bookmark E2E</main>');
    });
    const browser = await launchDevBrowser();
    try {
      await navigateFromAddressBar(browser.cdp, `${server.origin}/shortcut-bookmark`);
      await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('/shortcut-bookmark'),
        15000,
      );

      await pressShortcut(browser.cdp, 'h', 'KeyH', 72);
      await waitFor(
        browser.cdp,
        `(() => {
          const address = document.querySelector(${JSON.stringify(addressSelector)})?.value || '';
          const tabs = [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
            .map((tab) => tab.textContent || '')
            .join(' ');
          return { address, tabs };
        })()`,
        (value) => value.address.includes('browser://history') || /history/i.test(value.tabs),
        15000,
      );

      await pressShortcut(browser.cdp, 'j', 'KeyJ', 74);
      await waitFor(
        browser.cdp,
        `(() => {
          const address = document.querySelector(${JSON.stringify(addressSelector)})?.value || '';
          const tabs = [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
            .map((tab) => tab.textContent || '')
            .join(' ');
          return { address, tabs };
        })()`,
        (value) => value.address.includes('browser://downloads') || /downloads/i.test(value.tabs),
        15000,
      );
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('Ctrl+Shift+T reopens the last closed tab',
  { timeout: timeoutMs + 20000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const initialCount = await waitFor(browser.cdp, tabCountExpression, (count) => count >= 1);
      await pressShortcut(browser.cdp, 't', 'KeyT', 84);
      await waitFor(browser.cdp, tabCountExpression, (count) => count === initialCount + 1, 15000);
      await pressShortcut(browser.cdp, 'w', 'KeyW', 87);
      await waitFor(browser.cdp, tabCountExpression, (count) => count === initialCount, 15000);
      await pressShortcut(browser.cdp, 'T', 'KeyT', 84, 6);
      const reopenedCount = await waitFor(
        browser.cdp,
        tabCountExpression,
        (count) => count === initialCount + 1,
        15000,
      );
      assert.equal(reopenedCount, initialCount + 1);
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
