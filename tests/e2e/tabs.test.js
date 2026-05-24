import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

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

async function waitForTabTitles(cdp, titles) {
  return waitFor(
    cdp,
    tabStateExpression,
    (tabs) => titles.every((title) =>
      tabs.some((tab) => tab.text.includes(title))),
    30000,
  );
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

      const clickedNewTab = await cdp.evaluate(`
        (() => {
          const button = document.querySelector('button[aria-label="New tab"], button[title="New tab"]');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedNewTab, true);

      await waitFor(
        cdp,
        tabCountExpression,
        (count) => count === initialCount + 1,
      );

      const closedNewTab = await cdp.evaluate(`
        (() => {
          const tabs = [...document.querySelectorAll('a[href^="tab-context-menu:"]')];
          const newest = tabs[tabs.length - 1];
          const close = newest?.querySelector('button[title="Close tab"]');
          if (!close) return false;
          close.click();
          return true;
        })()
      `);
      assert.equal(closedNewTab, true);

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
      for (const slug of ['alpha', 'beta', 'gamma']) {
        const opened = await cefQuery(browser.cdp, `new-tab:${server.origin}/${slug}`);
        assert.equal(opened.ok, true, opened.value);
      }

      let tabs = await waitForTabTitles(browser.cdp, titles);
      assert.ok(tabs.find((tab) => tab.text.includes(titles[2]))?.active);

      const gamma = tabs.find((tab) => tab.text.includes(titles[2]));
      assert.ok(gamma?.id > 0, 'gamma tab id should be available');
      const closed = await cefQuery(browser.cdp, `close-tab:${gamma.id}`);
      assert.equal(closed.ok, true, closed.value);

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
      for (const slug of ['alpha', 'beta']) {
        const opened = await cefQuery(browser.cdp, `new-tab:${server.origin}/${slug}`);
        assert.equal(opened.ok, true, opened.value);
      }

      let tabs = await waitForTabTitles(browser.cdp, titles);
      assert.ok(tabs.find((tab) => tab.text.includes(titles[1]))?.active);

      const alpha = tabs.find((tab) => tab.text.includes(titles[0]));
      assert.ok(alpha?.id > 0, 'alpha tab id should be available');
      const closed = await cefQuery(browser.cdp, `close-tab:${alpha.id}`);
      assert.equal(closed.ok, true, closed.value);

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
