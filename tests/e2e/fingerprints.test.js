import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('browser can open the fingerprint protections test page',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let fingerprintsCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'browser://fingerprints');

      fingerprintsCdp = await browser.connectToTarget((target) =>
        (target.url || '').endsWith('/fingerprints.html') ||
        /fingerprint/i.test(target.title || '') ||
        /fingerprints/i.test(target.url || ''),
      );

      const visible = await waitFor(
        fingerprintsCdp,
        `(() => {
          const text = document.body.innerText || '';
          return {
            text,
            title: document.title || '',
          };
        })()`,
        (value) =>
          value.text.includes('Fingerprint Protection Proof') &&
          value.text.includes('Canvas') &&
          value.text.includes('WebGL'),
        15000,
      );

      assert.ok(visible.text.includes('Fingerprint Protection Proof'));
      assert.ok(visible.text.includes('Canvas'));
      assert.ok(visible.text.includes('WebGL'));
    } finally {
      if (fingerprintsCdp) {
        fingerprintsCdp.close();
      }
      await browser.close();
    }
  });
