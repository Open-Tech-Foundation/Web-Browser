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

test('user can clear cookies and storage for a site from the clear site data popup',
  { timeout: timeoutMs + 20000 },
  async () => {
    const uniqueTitle = `OTF Clear Site Data ${Date.now()}`;
    const staticServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      res.writeHead(200, {
        'content-type': 'text/html; charset=utf-8',
        'cache-control': 'no-store',
      });
      res.end(`<!doctype html>
        <html>
          <head>
            <title>${uniqueTitle}</title>
          </head>
          <body>
            <main>
              <h1>${uniqueTitle}</h1>
            </main>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    let popupCdp = null;
    let siteDataCdp = null;
    try {
      const pageUrl = `${staticServer.origin}/clear-site-data-e2e`;
      await navigateFromAddressBar(browser.cdp, pageUrl);
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(staticServer.origin) ||
        /OTF Clear Site Data/i.test(target.title || ''),
      );

      await waitFor(
        browser.cdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        (value) => value.includes(pageUrl) || value.includes(staticServer.origin),
        15000,
      );

      await clickSelector(browser.cdp, 'button[title="Clear site data"]');

      popupCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Clear site data' ||
        /cleardata\.html/i.test(target.url || ''),
      );

      const popupState = await waitFor(
        popupCdp,
        `(() => {
          const text = document.body.innerText || '';
          return { text };
        })()`,
        (value) => value.text.includes(staticServer.origin) &&
          value.text.includes('Clear Selected Data'),
        15000,
      );
      assert.ok(popupState.text.includes(staticServer.origin));

      const cleared = await popupCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('button')]
            .find((item) => (item.textContent || '').includes('Clear Selected Data'));
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(cleared, true);

      await waitFor(
        popupCdp,
        `document.body.innerText`,
        (text) => /Cleared/i.test(text),
        15000,
      );

      const openedDetails = await popupCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('button')]
            .find((item) => (item.textContent || '').includes('Manage'));
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(openedDetails, true);

      siteDataCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Site data' ||
        /sitedata\.html/i.test(target.url || '') ||
        /browser:\/\/sitedata/i.test(target.url || ''),
      );

      const clearedState = await waitFor(
        siteDataCdp,
        `(() => {
          const text = document.body.innerText || '';
          return { text };
        })()`,
        (value) =>
          /Cookies\s+0/.test(value.text) &&
          /Storage\s+0 B/.test(value.text),
        15000,
      );

      assert.match(clearedState.text, /Cookies\s+0/);
      assert.match(clearedState.text, /Storage\s+0 B/);
    } finally {
      if (siteDataCdp) {
        siteDataCdp.close();
      }
      if (popupCdp) {
        popupCdp.close();
      }
      if (pageCdp) {
        pageCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });
