import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  pressKey,
  sleep,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

const addressSelector = 'input[placeholder="Search or enter address..."]';

test('history and bookmark RPCs reject unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let bookmarksCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'browser://bookmarks');
      bookmarksCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Bookmarks' ||
        /bookmarks\.html/i.test(target.url || ''),
        15000,
      );

      await waitFor(
        bookmarksCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /Bookmarks/i.test(text),
        15000,
      );

      const response = await bookmarksCdp.evaluate(`
        Promise.all([
          new Promise((resolve) => {
            window.cefQuery({
              request: JSON.stringify({
                id: 'history-extra-param',
                method: 'history.list',
                params: { extra: true },
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
                id: 'bookmarks-extra-param',
                method: 'bookmarks.list',
                params: { extra: true },
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
                id: 'bookmarks-update-extra-param',
                method: 'bookmarks.update',
                params: { id: 1, url: 'https://example.test/', title: 'Example', extra: true },
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
      assert.equal(parsed[0].id, 'history-extra-param');
      assert.equal(parsed[0].ok, false);
      assert.match(parsed[0].error.message, /unexpected param: extra/);
      assert.equal(parsed[1].id, 'bookmarks-extra-param');
      assert.equal(parsed[1].ok, false);
      assert.match(parsed[1].error.message, /unexpected param: extra/);
      assert.equal(parsed[2].id, 'bookmarks-update-extra-param');
      assert.equal(parsed[2].ok, false);
      assert.match(parsed[2].error.message, /unexpected param: extra/);
    } finally {
      if (bookmarksCdp) bookmarksCdp.close();
      await browser.close();
    }
  });

test('user can bookmark a page, reopen the bookmark popup, and remove it',
  { timeout: timeoutMs + 15000 },
  async () => {
    const uniqueTitle = `OTF Bookmark E2E ${Date.now()}`;
    const staticServer = await startStaticServer((req, res) => {
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
    const visitedUrl = `${staticServer.origin}/bookmark-e2e`;

    const browser = await launchDevBrowser();
    let bookmarkbarCdp = null;
    let bookmarksCdp = null;
    try {
      const cdp = browser.cdp;
      await navigateFromAddressBar(cdp, visitedUrl);
      await waitFor(
        cdp,
        `(() => {
          const address = document.querySelector(${JSON.stringify(addressSelector)})?.value || '';
          const text = document.body.innerText || '';
          return { address, text };
        })()`,
        (value) => value.address.includes('/bookmark-e2e') &&
          (value.text.includes(uniqueTitle) || value.text.includes('/bookmark-e2e')),
        15000,
      );

      await waitFor(cdp, `!!document.querySelector('button[title="Bookmark this page"]')`, Boolean);
      await clickSelector(cdp, 'button[title="Bookmark this page"]');
      await waitFor(
        cdp,
        `!!document.querySelector('button[title="Remove bookmark"]')`,
        Boolean,
        15000,
      );

      bookmarkbarCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Bookmark' ||
        /bookmarkbar\.html/i.test(target.url || ''),
      );
      await waitFor(
        bookmarkbarCdp,
        `document.body.innerText`,
        (text) => text.includes('Bookmark') && text.includes('Remove'),
        15000,
      );

      await pressKey(bookmarkbarCdp, 'Escape');
      bookmarkbarCdp.close();
      bookmarkbarCdp = null;

      await clickSelector(cdp, 'button[title="Remove bookmark"]');
      await waitFor(
        cdp,
        `!!document.querySelector('button[title="Remove bookmark"]')`,
        Boolean,
        15000,
      );

      bookmarkbarCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Bookmark' ||
        /bookmarkbar\.html/i.test(target.url || ''),
      );
      await waitFor(
        bookmarkbarCdp,
        `document.body.innerText`,
        (text) => text.includes('Bookmark') && text.includes('Remove'),
        15000,
      );

      await clickByText(bookmarkbarCdp, 'button', 'Remove');

      await waitFor(
        cdp,
        `!!document.querySelector('button[title="Bookmark this page"]')`,
        Boolean,
        15000,
      );

      bookmarkbarCdp.close();
      bookmarkbarCdp = null;

      await navigateFromAddressBar(cdp, 'browser://bookmarks');
      bookmarksCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Bookmarks' ||
        /bookmarks\.html/i.test(target.url || ''),
      );

      await waitFor(
        bookmarksCdp,
        `document.body.innerText`,
        (text) => !text.includes(uniqueTitle) && !text.includes('/bookmark-e2e'),
        15000,
      );

      await navigateFromAddressBar(cdp, visitedUrl);
      await waitFor(cdp, `!!document.querySelector('button[title="Bookmark this page"]')`, Boolean);
      await clickSelector(cdp, 'button[title="Bookmark this page"]');
      await waitFor(cdp, `!!document.querySelector('button[title="Remove bookmark"]')`, Boolean);

      if (bookmarksCdp) {
        bookmarksCdp.close();
        bookmarksCdp = null;
      }
      await navigateFromAddressBar(cdp, 'browser://bookmarks');
      bookmarksCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Bookmarks' ||
        /bookmarks\.html/i.test(target.url || ''),
      );

      const bookmarksText = await waitFor(
        bookmarksCdp,
        `document.body.innerText`,
        (text) => text.includes(uniqueTitle) || text.includes('/bookmark-e2e'),
        15000,
      );
      assert.ok(
        bookmarksText.includes(uniqueTitle) || bookmarksText.includes('/bookmark-e2e'),
        `expected bookmark in list, got ${bookmarksText}`,
      );
    } finally {
      if (bookmarkbarCdp) {
        bookmarkbarCdp.close();
      }
      if (bookmarksCdp) {
        bookmarksCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });

test('bookmarks persist across restart and open from the bookmarks page',
  { timeout: timeoutMs + 45000 },
  async () => {
    const unique = Date.now();
    const uniqueTitle = `OTF Bookmark Persist ${unique}`;
    const bookmarkPath = `/bookmark-persist-${unique}`;
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-bookmark-persist-'));
    const staticServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>${uniqueTitle}</title></head>
          <body><main><h1>${uniqueTitle}</h1><p>${bookmarkPath}</p></main></body>
        </html>`);
    });
    const visitedUrl = `${staticServer.origin}${bookmarkPath}`;

    let browser = null;
    let bookmarksCdp = null;
    try {
      browser = await launchDevBrowser({ profileRoot, preserveProfile: true });
      await navigateFromAddressBar(browser.cdp, visitedUrl);
      await waitFor(
        browser.cdp,
        `(() => {
          const address = document.querySelector(${JSON.stringify(addressSelector)})?.value || '';
          const text = document.body.innerText || '';
          return { address, text };
        })()`,
        (value) => value.address.includes(bookmarkPath) &&
          (value.text.includes(uniqueTitle) || value.text.includes(bookmarkPath)),
        15000,
      );

      await waitFor(browser.cdp, `!!document.querySelector('button[title="Bookmark this page"]')`, Boolean);
      await clickSelector(browser.cdp, 'button[title="Bookmark this page"]');
      await waitFor(
        browser.cdp,
        `!!document.querySelector('button[title="Remove bookmark"]')`,
        Boolean,
        15000,
      );

      await browser.close();
      browser = null;
      await sleep(1000);

      browser = await launchDevBrowser({ profileRoot, preserveProfile: true });
      await navigateFromAddressBar(browser.cdp, 'browser://bookmarks');
      bookmarksCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Bookmarks' ||
        /bookmarks\.html/i.test(target.url || ''),
      );

      const bookmarksText = await waitFor(
        bookmarksCdp,
        `document.body.innerText`,
        (text) => text.includes(uniqueTitle) || text.includes(bookmarkPath),
        15000,
      );
      assert.ok(
        bookmarksText.includes(uniqueTitle) || bookmarksText.includes(bookmarkPath),
        `expected persisted bookmark in list, got ${bookmarksText}`,
      );

      await clickSelector(bookmarksCdp, 'button[title="Open in current tab"]');

      await waitFor(
        browser.cdp,
        `(() => {
          const address = document.querySelector(${JSON.stringify(addressSelector)})?.value || '';
          const text = document.body.innerText || '';
          return { address, text };
        })()`,
        (value) => value.address.includes(bookmarkPath) &&
          (value.text.includes(uniqueTitle) || value.text.includes(bookmarkPath)),
        15000,
      );
    } finally {
      if (bookmarksCdp) {
        bookmarksCdp.close();
      }
      if (browser) {
        await browser.close();
      }
      await staticServer.close();
      await rm(profileRoot, { recursive: true, force: true });
    }
  });
