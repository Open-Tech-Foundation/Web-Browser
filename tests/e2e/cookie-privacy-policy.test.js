import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  sleep,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

const nativeRpc = (method, params, id = `cookie-policy-${Date.now()}`) => `
  new Promise((resolve) => {
    window.cefQuery({
      request: JSON.stringify({
        id: ${JSON.stringify(id)},
        method: ${JSON.stringify(method)},
        params: ${JSON.stringify(params)},
      }),
      onSuccess: (json) => {
        try {
          const parsed = JSON.parse(json);
          resolve(parsed?.ok ? parsed.result : { error: parsed?.error?.message || 'RPC failed' });
        } catch (error) {
          resolve({ error: String(error) });
        }
      },
      onFailure: (code, message) => resolve({ error: message || String(code) }),
    });
  })
`;

test('strict cookie policy blocks third-party cookies and caps first-party expiry',
  { timeout: timeoutMs + 30000, concurrency: false },
  async () => {
    const unique = Date.now();
    const firstPartyCookie = `otf_first_party_cap_${unique}`;
    const thirdPartyCookie = `otf_third_party_block_${unique}`;
    const thirdPartyChecks = [];

    let thirdPartyOrigin = '';
    const thirdPartyServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      if (req.url === '/set-first') {
        res.writeHead(200, {
          'content-type': 'text/html; charset=utf-8',
          'cache-control': 'no-store',
          'set-cookie': `${firstPartyCookie}=present; Path=/; Max-Age=31536000; SameSite=Lax`,
        });
        res.end('<!doctype html><title>first party set</title><main>first party set</main>');
        return;
      }
      if (req.url === '/set-third') {
        res.writeHead(200, {
          'content-type': 'text/html; charset=utf-8',
          'cache-control': 'no-store',
          'set-cookie': `${thirdPartyCookie}=blocked; Path=/; SameSite=None; Secure`,
        });
        res.end('<!doctype html><title>third party set</title><main>third party set</main>');
        return;
      }
      if (req.url === '/check') {
        const cookie = req.headers.cookie || '';
        thirdPartyChecks.push(cookie);
        res.writeHead(200, {
          'content-type': 'text/html; charset=utf-8',
          'cache-control': 'no-store',
        });
        res.end(`<!doctype html><title>third party check</title><main>${cookie}</main>`);
        return;
      }
      res.writeHead(404);
      res.end('not found');
    });
    thirdPartyOrigin = thirdPartyServer.origin.replace('127.0.0.1', 'localhost');

    const topServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      if (req.url === '/embed-set') {
        res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
        res.end(`<!doctype html>
          <title>embed set</title>
          <iframe src="${thirdPartyOrigin}/set-third"></iframe>`);
        return;
      }
      if (req.url === '/embed-check') {
        res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
        res.end(`<!doctype html>
          <title>embed check</title>
          <iframe src="${thirdPartyOrigin}/check"></iframe>`);
        return;
      }
      res.writeHead(404);
      res.end('not found');
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    let siteDataCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, `${thirdPartyOrigin}/set-first`);
      pageCdp = await browser.connectToTarget(
        (target) => (target.url || '').startsWith(`${thirdPartyOrigin}/set-first`),
        15000,
      );
      await waitFor(pageCdp, 'document.readyState', (state) => state === 'complete', 15000);
      pageCdp.close();
      pageCdp = null;

      await navigateFromAddressBar(browser.cdp, `${topServer.origin}/embed-set`);
      pageCdp = await browser.connectToTarget(
        (target) => (target.url || '').startsWith(`${topServer.origin}/embed-set`),
        15000,
      );
      await waitFor(pageCdp, 'document.readyState', (state) => state === 'complete', 15000);
      pageCdp.close();
      pageCdp = null;

      await navigateFromAddressBar(browser.cdp, `${topServer.origin}/embed-check`);
      pageCdp = await browser.connectToTarget(
        (target) => (target.url || '').startsWith(`${topServer.origin}/embed-check`),
        15000,
      );
      await waitFor(pageCdp, 'document.readyState', (state) => state === 'complete', 15000);
      for (let i = 0; i < 60 && thirdPartyChecks.length === 0; i += 1) {
        await sleep(250);
      }
      pageCdp.close();
      pageCdp = null;

      assert.ok(thirdPartyChecks.length > 0, 'third-party iframe check should hit server');
      assert.ok(
        !thirdPartyChecks.at(-1).includes(firstPartyCookie) &&
          !thirdPartyChecks.at(-1).includes(thirdPartyCookie),
        `third-party request should not send cookies, got: ${thirdPartyChecks.at(-1)}`,
      );

      const siteDataUrl = `browser://sitedata?origin=${encodeURIComponent(thirdPartyOrigin)}`;
      await navigateFromAddressBar(browser.cdp, siteDataUrl);
      siteDataCdp = await browser.connectToTarget((target) =>
        /sitedata\.html/i.test(target.url || '') ||
        /browser:\/\/sitedata/i.test(target.url || ''),
        15000,
      );
      await waitFor(siteDataCdp, `document.body?.innerText || ""`, (text) =>
        text.includes(thirdPartyOrigin), 15000);

      const nowSeconds = Math.floor(Date.now() / 1000);
      const cookies = await waitFor(
        siteDataCdp,
        nativeRpc('siteData.getCookies', { origin: thirdPartyOrigin }, 'cookie-policy-cookies'),
        (value) => Array.isArray(value) && value.some((cookie) =>
          cookie.name === firstPartyCookie &&
          cookie.expiresAt > 0 &&
          cookie.expiresAt <= nowSeconds + 7 * 24 * 60 * 60 + 120),
        15000,
      );
      const firstParty = cookies.find((cookie) => cookie.name === firstPartyCookie);
      assert.ok(firstParty.expiresAt > 0, 'first-party session should have an imposed expiry');
      assert.ok(
        firstParty.expiresAt <= nowSeconds + 7 * 24 * 60 * 60 + 120,
        `first-party cookie expiry should be capped to 7 days, got ${firstParty.expiresAt}`,
      );
      assert.equal(
        cookies.some((cookie) => cookie.name === thirdPartyCookie),
        false,
        'third-party Set-Cookie should not be stored',
      );

      const policy = await siteDataCdp.evaluate(
        nativeRpc('siteData.getCookiePolicy', { origin: thirdPartyOrigin }, 'cookie-policy-records'),
      );
      assert.ok(Array.isArray(policy), 'cookie policy RPC should return an array');
    } finally {
      if (siteDataCdp) siteDataCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await topServer.close();
      await thirdPartyServer.close();
    }
  });
