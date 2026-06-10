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
        `document.body?.innerText || ""`,
        (text) => text.includes(server.origin) && text.includes('Permissions'),
        15000,
      );
      await clickByText(siteDataCdp, 'button', 'Permissions');
      await clickByText(siteDataCdp, 'button', 'Ask');

      const text = await waitFor(
        siteDataCdp,
        `document.body?.innerText || ""`,
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

test('site data page lists HttpOnly cookies',
  { timeout: timeoutMs + 15000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, {
        'content-type': 'text/html; charset=utf-8',
        'set-cookie': [
          'otf_visible_cookie=visible; Path=/; SameSite=Lax',
          'otf_http_only_cookie=secret; Path=/; HttpOnly; SameSite=Lax',
        ],
      });
      res.end('<!doctype html><title>Site Data Cookies</title><main>Site Data Cookies</main>');
    });
    const browser = await launchDevBrowser();
    let pageCdp = null;
    let siteDataCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, `${server.origin}/cookies`);
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/cookies`),
        15000,
      );
      await waitFor(pageCdp, 'document.readyState', (state) => state === 'complete', 15000);

      const siteDataUrl = `browser://sitedata?origin=${encodeURIComponent(server.origin)}`;
      await navigateFromAddressBar(browser.cdp, siteDataUrl);
      siteDataCdp = await browser.connectToTarget((target) =>
        /sitedata\.html/i.test(target.url || '') ||
        /browser:\/\/sitedata/i.test(target.url || ''),
        15000,
      );

      const text = await waitFor(
        siteDataCdp,
        `document.body?.innerText || ""`,
        (value) =>
          value.includes('otf_visible_cookie') &&
          value.includes('otf_http_only_cookie') &&
          value.includes('HttpOnly'),
        15000,
      );
      assert.ok(text.includes('otf_http_only_cookie'));
    } finally {
      if (siteDataCdp) siteDataCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

test('site data RPC rejects unknown params',
  { timeout: timeoutMs + 15000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><title>Site Data Strict RPC</title><main>Strict RPC</main>');
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
        `document.body?.innerText || ""`,
        (text) => text.includes(server.origin),
        15000,
      );

      const response = await siteDataCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'strict-extra-param',
              method: 'siteData.getCookies',
              params: {
                origin: ${JSON.stringify(server.origin)},
                extra: true,
              },
            }),
            onSuccess: resolve,
            onFailure: (code, message) => resolve(JSON.stringify({
              ok: false,
              error: { code: String(code), message },
            })),
          });
        })
      `);
      const parsed = JSON.parse(response);
      assert.equal(parsed.id, 'strict-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (siteDataCdp) siteDataCdp.close();
      await browser.close();
      await server.close();
    }
  });
