import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('browser shows the insecure blocked page for unsafe HTTP navigation',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    let blockedCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'http://example.com');

      blockedCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith('browser://insecure-blocked') ||
        /insecure-blocked/i.test(target.url || '') ||
        (target.title || '') === 'Connection Insecure',
      );

      const visible = await waitFor(
        blockedCdp,
        `(() => {
          const text = document.body?.innerText || '';
          const blocked = [...document.querySelectorAll('div')].map((item) => item.textContent || '').join(' ');
          return { text, blocked };
        })()`,
        (value) =>
          value.text.includes('This site is not secure') &&
          value.blocked.includes('http://example.com'),
        15000,
      );

      assert.ok(visible.text.includes('This site is not secure'));
      assert.ok(visible.blocked.includes('http://example.com'));
    } finally {
      if (blockedCdp) {
        blockedCdp.close();
      }
      await browser.close();
    }
  });
