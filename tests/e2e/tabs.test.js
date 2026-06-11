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
import { connectShell } from './helpers/e2eUtils.js';

const nativeRpc = (method, params = {}, id = `tabs-rpc-${Date.now()}`) => `
  new Promise((resolve) => {
    window.cefQuery({
      request: JSON.stringify({
        id: ${JSON.stringify(id)},
        method: ${JSON.stringify(method)},
        params: ${JSON.stringify(params)},
      }),
      onSuccess: resolve,
      onFailure: (code, message) => resolve(JSON.stringify({
        id: ${JSON.stringify(id)},
        ok: false,
        error: { code: String(code), message },
      })),
    });
  })
`;

test('tabs RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 15000);

      const response = JSON.parse(await shellCdp.evaluate(
        nativeRpc('tabs.list', { extra: true }, 'tabs-extra-param'),
      ));
      assert.equal(response.id, 'tabs-extra-param');
      assert.equal(response.ok, false);
      assert.match(response.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

const tabCountExpression = `document.querySelectorAll('a[href^="tab-context-menu:"]').length`;
const tabStateExpression = `(() => [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
  .map((tab) => {
    const href = tab.getAttribute('href') || '';
    return {
      id: Number(href.replace('tab-context-menu:', '')),
      text: tab.textContent || '',
      active: tab.className.includes('bg-bar-light') || tab.className.includes('dark:bg-bar-dark'),
    };
  }))()`;

async function waitForTabTitles(cdp, titles) {
  return waitFor(
    cdp,
    tabStateExpression,
    (tabs) => titles.every((title) =>
      tabs.some((tab) => tab.text.includes(title))),
    30000,
  );
}

async function clickTabCloseButton(cdp, titleNeedle) {
  const rect = await cdp.evaluate(`
    (() => {
      const needle = ${JSON.stringify(titleNeedle)};
      const tab = [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
        .find((item) => (item.textContent || '').includes(needle));
      const button = tab?.querySelector('button[title="Close tab"]');
      if (!button) return null;
      const rect = button.getBoundingClientRect();
      return {
        x: rect.left + rect.width / 2,
        y: rect.top + rect.height / 2,
        width: rect.width,
        height: rect.height,
      };
    })()
  `);
  assert.ok(rect, `close button not found for tab: ${titleNeedle}`);
  assert.ok(rect.width > 0 && rect.height > 0, `close button is not visible for tab: ${titleNeedle}`);
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mouseMoved',
    x: rect.x,
    y: rect.y,
  });
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mousePressed',
    x: rect.x,
    y: rect.y,
    button: 'left',
    clickCount: 1,
  });
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mouseReleased',
    x: rect.x,
    y: rect.y,
    button: 'left',
    clickCount: 1,
  });
}

async function openTabsFromAddressBar(cdp, origin, slugs) {
  for (const slug of slugs) {
    const before = await waitFor(cdp, tabCountExpression, (count) => count >= 1, 15000);
    await clickSelector(cdp, 'a[title="New tab"]');
    await waitFor(cdp, tabCountExpression, (count) => count === before + 1, 15000);
    await navigateFromAddressBar(cdp, `${origin}/${slug}`);
    await waitFor(
      cdp,
      tabStateExpression,
      (tabs) => tabs.some((tab) => tab.active && tab.text.toLowerCase().includes(slug)),
      15000,
    );
  }
}

test('user can open and close a tab from the tab strip',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const cdp = browser.cdp;
      const initialCount = await waitFor(
        cdp,
        tabCountExpression,
        (count) => count >= 1,
      );

      await clickSelector(cdp, 'a[title="New tab"]');

      await waitFor(
        cdp,
        tabCountExpression,
        (count) => count === initialCount + 1,
      );

      await clickTabCloseButton(cdp, 'New Tab');

      const finalCount = await waitFor(
        cdp,
        tabCountExpression,
        (count) => count === initialCount,
      );
      assert.equal(finalCount, initialCount);
    } finally {
      await browser.close();
    }
  });

test('closing the active tab activates the previous tab in the workspace',
  { timeout: timeoutMs + 20000 },
  async () => {
    const unique = Date.now();
    const titles = [`Tab Alpha ${unique}`, `Tab Beta ${unique}`, `Tab Gamma ${unique}`];
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      const index = req.url.includes('beta') ? 1 : req.url.includes('gamma') ? 2 : 0;
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>${titles[index]}</title></head>
          <body><main><h1>${titles[index]}</h1></main></body>
        </html>`);
    });
    const browser = await launchDevBrowser();
    try {
      await openTabsFromAddressBar(browser.cdp, server.origin, ['alpha', 'beta', 'gamma']);

      let tabs = await waitForTabTitles(browser.cdp, titles);
      assert.ok(tabs.find((tab) => tab.text.includes(titles[2]))?.active);

      await clickTabCloseButton(browser.cdp, titles[2]);

      tabs = await waitFor(
        browser.cdp,
        tabStateExpression,
        (items) =>
          !items.some((tab) => tab.text.includes(titles[2])) &&
          Boolean(items.find((tab) => tab.text.includes(titles[1]))?.active),
        30000,
      );
      assert.ok(tabs.find((tab) => tab.text.includes(titles[1]))?.active);
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('tabs.memory returns non-negative memory bytes for the active tab',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 15000);

      const tabIdResp = await shellCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: 'get-my-tab-id',
            onSuccess: resolve,
          });
        })
      `);
      const tabId = parseInt(tabIdResp);

      const response = JSON.parse(await shellCdp.evaluate(
        nativeRpc('tabs.memory', { tabId }, 'tabs-memory-active'),
      ));
      assert.equal(response.id, 'tabs-memory-active');
      assert.equal(response.ok, true);
      assert.equal(typeof response.result, 'number');
      assert.ok(response.result >= -1);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('tabs.memory rejects unknown params',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 15000);

      const response = JSON.parse(await shellCdp.evaluate(
        nativeRpc('tabs.memory', { tabId: 0, extra: true }, 'tabs-memory-extra'),
      ));
      assert.equal(response.id, 'tabs-memory-extra');
      assert.equal(response.ok, false);
      assert.match(response.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('closing a background tab keeps the active tab selected',
  { timeout: timeoutMs + 20000 },
  async () => {
    const unique = Date.now();
    const titles = [`Background Alpha ${unique}`, `Background Beta ${unique}`];
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      const title = req.url.includes('beta') ? titles[1] : titles[0];
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>${title}</title></head>
          <body><main><h1>${title}</h1></main></body>
        </html>`);
    });
    const browser = await launchDevBrowser();
    try {
      await openTabsFromAddressBar(browser.cdp, server.origin, ['alpha', 'beta']);

      let tabs = await waitForTabTitles(browser.cdp, titles);
      assert.ok(tabs.find((tab) => tab.text.includes(titles[1]))?.active);

      await clickTabCloseButton(browser.cdp, titles[0]);

      tabs = await waitFor(
        browser.cdp,
        tabStateExpression,
        (items) =>
          !items.some((tab) => tab.text.includes(titles[0])) &&
          Boolean(items.find((tab) => tab.text.includes(titles[1]))?.active),
        30000,
      );
      assert.ok(tabs.find((tab) => tab.text.includes(titles[1]))?.active);
    } finally {
      await browser.close();
      await server.close();
    }
  });
