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

      await bookmarkbarCdp.evaluate(`window.cefQuery?.({ request: 'hide-bookmarkbar' })`);
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

      const clickedRemove = await bookmarkbarCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('button')]
            .find((item) => (item.textContent || '').trim() === 'Remove');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedRemove, true);

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
