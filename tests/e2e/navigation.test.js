import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

const addressSelector = 'input[placeholder="Search or enter address..."]';

test('navigation RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      await waitFor(
        browser.cdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /New Tab/i.test(text) || /Search/i.test(text),
        15000,
      );

      const response = await browser.cdp.evaluate(`
        Promise.all([
          new Promise((resolve) => {
            window.cefQuery({
              request: JSON.stringify({
                id: 'navigation-extra-param',
                method: 'navigation.newTab',
                params: { url: 'browser://newtab', extra: true },
              }),
              onSuccess: resolve,
              onFailure: (code, message) => resolve(JSON.stringify({
                ok: false,
                error: { code: String(code), message },
              })),
            });
          }),
          new Promise((resolve) => {
            window.cefQuery({
              request: JSON.stringify({
                id: 'navigation-resolve-extra-param',
                method: 'navigation.resolveInput',
                params: { input: 'example.com', extra: true },
              }),
              onSuccess: resolve,
              onFailure: (code, message) => resolve(JSON.stringify({
                ok: false,
                error: { code: String(code), message },
              })),
            });
          }),
          new Promise((resolve) => {
            window.cefQuery({
              request: JSON.stringify({
                id: 'navigation-private-extra-param',
                method: 'navigation.newPrivateTab',
                params: { url: 'browser://newtab', extra: true },
              }),
              onSuccess: resolve,
              onFailure: (code, message) => resolve(JSON.stringify({
                ok: false,
                error: { code: String(code), message },
              })),
            });
          }),
        ])
      `);
      const parsed = response.map((item) => JSON.parse(item));
      assert.equal(parsed[0].id, 'navigation-extra-param');
      assert.equal(parsed[0].ok, false);
      assert.match(parsed[0].error.message, /unexpected param: extra/);
      assert.equal(parsed[1].id, 'navigation-resolve-extra-param');
      assert.equal(parsed[1].ok, false);
      assert.match(parsed[1].error.message, /unexpected param: extra/);
      assert.equal(parsed[2].id, 'navigation-private-extra-param');
      assert.equal(parsed[2].ok, false);
      assert.match(parsed[2].error.message, /unexpected param: extra/);
    } finally {
      await browser.close();
    }
  });

test('user can navigate to Settings from the address bar',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const cdp = browser.cdp;

      await navigateFromAddressBar(cdp, 'browser://settings');

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

test('address bar treats localhost input as a direct URL',
  { timeout: timeoutMs + 15000 },
  async () => {
    const uniqueTitle = `Localhost Navigation ${Date.now()}`;
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>${uniqueTitle}</title></head>
          <body><main><h1>${uniqueTitle}</h1></main></body>
        </html>`);
    });
    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      const input = server.origin.replace(/^http:\/\//, '') + '/direct-url';
      await navigateFromAddressBar(browser.cdp, input);

      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/direct-url`),
        15000,
      );
      await waitFor(
        pageCdp,
        'document.title',
        (title) => title === uniqueTitle,
        15000,
      );
      const address = await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ""`,
        (value) => value.includes('127.0.0.1') && value.includes('/direct-url'),
        15000,
      );
      assert.ok(address.includes('/direct-url'));
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

test('address bar sends search terms and dangerous schemes to search',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser({
      settings: {
        searchEngine: 'google',
        historyEnabled: true,
        downloadsEnabled: true,
        startupBehavior: 'newtab',
        startupUrls: [],
        httpsOnly: false,
        blockInsecure: false,
        appearanceMode: 'auto',
      },
    });
    try {
      await navigateFromAddressBar(browser.cdp, 'hello world');
      const searchAddress = await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ""`,
        (value) => value.includes('google.com/search') && value.includes('hello'),
        15000,
      );
      assert.ok(searchAddress.includes('q=hello+world') || searchAddress.includes('q=hello%20world'));

      const dangerous = 'javascript:alert(1)';
      await navigateFromAddressBar(browser.cdp, dangerous);
      const dangerousAddress = await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ""`,
        (value) => value.includes('google.com/search') && value.includes('javascript'),
        15000,
      );
      assert.ok(!dangerousAddress.toLowerCase().startsWith('javascript:'));
    } finally {
      await browser.close();
    }
  });

test('back and forward toolbar buttons follow page history',
  { timeout: timeoutMs + 20000 },
  async () => {
    const unique = Date.now();
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      const page = req.url.includes('second') ? 'Second' : 'First';
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html><title>${page} Navigation ${unique}</title><main>${page} Navigation ${unique}<a id="to-second" href="/second">Second</a></main>`);
    });
    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, `${server.origin}/first`);
      await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('/first'),
        15000,
      );
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/first`),
        15000,
      );
      await clickSelector(pageCdp, '#to-second');
      await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('/second'),
        15000,
      );
      await waitFor(browser.cdp, `!document.querySelector('button[title="Go back"]')?.disabled`, Boolean, 15000);
      await clickSelector(browser.cdp, 'button[title="Go back"]');
      await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('/first'),
        15000,
      );
      await waitFor(browser.cdp, `!document.querySelector('button[title="Go forward"]')?.disabled`, Boolean, 15000);
      await clickSelector(browser.cdp, 'button[title="Go forward"]');
      const address = await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('/second'),
        15000,
      );
      assert.ok(address.includes('/second'));
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });
