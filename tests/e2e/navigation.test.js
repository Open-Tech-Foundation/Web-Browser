import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  pressKey,
  startStaticServer,
  timeoutMs,
  typeText,
  waitFor,
} from './helpers/browserHarness.js';

const addressSelector = 'input[placeholder="Search or enter address..."]';

function cefQuery(cdp, request) {
  return cdp.evaluate(`
    new Promise((resolve) => {
      window.cefQuery?.({
        request: ${JSON.stringify(request)},
        onSuccess: (value) => resolve({ ok: true, value }),
        onFailure: (_, message) => resolve({ ok: false, value: message || '' }),
      });
    })
  `);
}

test('user can navigate to Settings from the address bar',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const cdp = browser.cdp;

      await waitFor(
        cdp,
        `!!document.querySelector(${JSON.stringify(addressSelector)})`,
        Boolean,
      );
      await clickSelector(cdp, addressSelector);
      await cdp.send('Input.dispatchKeyEvent', {
        type: 'keyDown',
        key: 'a',
        code: 'KeyA',
        modifiers: 2,
      });
      await cdp.send('Input.dispatchKeyEvent', {
        type: 'keyUp',
        key: 'a',
        code: 'KeyA',
        modifiers: 2,
      });
      await typeText(cdp, 'browser://settings');
      await pressKey(cdp, 'Enter');

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
      const resolved = await cefQuery(
        browser.cdp,
        `resolve-input-url:${input.length}:${input}`,
      );
      assert.equal(resolved.ok, true);
      assert.equal(resolved.value, `http://${input}`);

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

test('input resolver sends search terms and dangerous schemes to search',
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
      const search = await cefQuery(
        browser.cdp,
        'resolve-input-url:11:hello world',
      );
      assert.equal(search.ok, true);
      assert.equal(search.value, 'https://www.google.com/search?q=hello+world');

      const dangerous = 'javascript:alert(1)';
      const dangerousResult = await cefQuery(
        browser.cdp,
        `resolve-input-url:${dangerous.length}:${dangerous}`,
      );
      assert.equal(dangerousResult.ok, true);
      assert.equal(
        dangerousResult.value,
        'https://www.google.com/search?q=javascript%3Aalert(1)',
      );
    } finally {
      await browser.close();
    }
  });
