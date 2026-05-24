import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickByText,
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('site data page shows permissions and can change a site permission',
  { timeout: timeoutMs + 15000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><title>Site Data E2E</title><main>Site Data E2E</main>');
    });
    const browser = await launchDevBrowser();
    let siteDataCdp = null;
    try {
      const siteDataUrl = `browser://sitedata?origin=${encodeURIComponent(server.origin)}`;
      await navigateFromAddressBar(browser.cdp, siteDataUrl);
      siteDataCdp = await browser.connectToTarget((target) =>
        /sitedata\.html/i.test(target.url || '') ||
        /browser:\/\/sitedata/i.test(target.url || ''),
        15000,
      );

      await waitFor(
        siteDataCdp,
        `document.body.innerText`,
        (text) => text.includes(server.origin) && text.includes('Permissions'),
        15000,
      );
      await clickByText(siteDataCdp, 'button', 'Permissions');
      await clickByText(siteDataCdp, 'button', 'Ask');

      const text = await waitFor(
        siteDataCdp,
        `document.body.innerText`,
        (value) => value.includes('Downloads') && value.includes('Allow'),
        15000,
      );
      assert.ok(text.includes('Allow'));
    } finally {
      if (siteDataCdp) siteDataCdp.close();
      await browser.close();
      await server.close();
    }
  });
