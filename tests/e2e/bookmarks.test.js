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

test('user can bookmark a page and remove it from Bookmarks',
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
        `document.body.innerText`,
        (text) => /Bookmark/i.test(text) || /Remove bookmark/i.test(text),
      );

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

      const clickedDelete = await bookmarksCdp.evaluate(`
        (() => {
          const cards = [...document.querySelectorAll('.group')];
          const card = cards.find((item) => (item.textContent || '').includes(${JSON.stringify(uniqueTitle)}) ||
            (item.textContent || '').includes('/bookmark-e2e'));
          const button = card?.querySelector('button[title="Delete bookmark"]');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedDelete, true);

      await waitFor(
        bookmarksCdp,
        `document.body.innerText`,
        (text) => /Delete Bookmark\?/i.test(text),
      );
      const confirmedDelete = await bookmarksCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('button')]
            .find((item) => (item.textContent || '').trim() === 'Delete');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(confirmedDelete, true);

      await waitFor(
        bookmarksCdp,
        `document.body.innerText`,
        (text) => !text.includes(uniqueTitle) && !text.includes('/bookmark-e2e'),
        15000,
      );
    } finally {
      if (bookmarksCdp) {
        bookmarksCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });
