import test from 'node:test';
import assert from 'node:assert/strict';

import { devUrl, launchDevBrowser, timeoutMs } from './helpers/browserHarness.js';

test('real dev browser launches and renders the React shell',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const pageState = await browser.cdp.evaluate(
        '({ href: location.href, title: document.title, hasRoot: !!document.querySelector("#root") })'
      );
      assert.ok(pageState.href.startsWith(devUrl) || pageState.href.startsWith('browser://'));
      assert.equal(pageState.hasRoot, true);
    } finally {
      await browser.close();
    }
  });
