import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('main-frame navigation strips tracking query parameters before request',
  { timeout: timeoutMs + 15000 },
  async () => {
    const requests = [];
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      requests.push(req.url);
      res.writeHead(200, {
        'content-type': 'text/html; charset=utf-8',
        'cache-control': 'no-store',
      });
      res.end(`<!doctype html>
        <title>tracking params stripped</title>
        <main id="url">${req.url}</main>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      const dirtyUrl = `${server.origin}/landing?keep=1&utm_source=newsletter&fbclid=abc&gclid=xyz&page=2#section`;
      await navigateFromAddressBar(browser.cdp, dirtyUrl);
      pageCdp = await browser.connectToTarget(
        (target) => (target.url || '').startsWith(`${server.origin}/landing`),
        15000,
      );
      const cleanUrl = await waitFor(
        pageCdp,
        `document.querySelector('#url')?.textContent || ''`,
        (value) => value.includes('/landing') && value.includes('keep=1'),
        15000,
      );

      assert.equal(cleanUrl, '/landing?keep=1&page=2');
      assert.equal(requests.at(-1), '/landing?keep=1&page=2');
      assert.ok(!cleanUrl.includes('utm_source'));
      assert.ok(!cleanUrl.includes('fbclid'));
      assert.ok(!cleanUrl.includes('gclid'));
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });
