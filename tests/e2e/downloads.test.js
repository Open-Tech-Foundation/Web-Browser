import test from 'node:test';
import assert from 'node:assert/strict';

import {
  allowDownloadOnce,
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('user can download a file and clear finished downloads from the downloads page',
  { timeout: timeoutMs + 20000 },
  async () => {
    const uniqueName = `otf-download-${Date.now()}.txt`;
    const downloadBody = `Downloaded at ${new Date().toISOString()}\n`;
    let downloadRequestCount = 0;

    const staticServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      if (req.url === '/download') {
        downloadRequestCount += 1;
        res.writeHead(200, {
          'content-type': 'text/plain; charset=utf-8',
          'content-disposition': `attachment; filename="${uniqueName}"`,
        });
        res.end(downloadBody);
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>OTF Downloads E2E</title></head>
          <body>
            <main>
              <a id="download-link" href="/download">Download file</a>
            </main>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser({
      settings: {
        downloadsEnabled: true,
      },
    });
    let pageCdp = null;
    let downloadsCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, staticServer.origin);
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(staticServer.origin) ||
        /OTF Downloads E2E/i.test(target.title || ''),
      );
      await waitFor(
        pageCdp,
        `!!document.querySelector('#download-link')`,
        Boolean,
        15000,
      );
      const clickedDownload = await pageCdp.evaluate(`
        (() => {
          const link = document.querySelector('#download-link');
          if (!link) return false;
          link.click();
          return true;
        })()
      `);
      assert.equal(clickedDownload, true);
      await allowDownloadOnce(browser, staticServer.origin);
      await waitFor(
        pageCdp,
        `${downloadRequestCount}`,
        () => downloadRequestCount > 0,
        5000,
      );

      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Downloads' ||
        /downloads\.html/i.test(target.url || ''),
      );

      await waitFor(
        downloadsCdp,
        `document.body.innerText`,
        (text) => text.includes(uniqueName),
        15000,
      );

      const clickedClear = await downloadsCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('button')]
            .find((item) => (item.textContent || '').trim() === 'Clear Finished');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedClear, true);

      await waitFor(
        downloadsCdp,
        `document.body.innerText`,
        (text) => !text.includes(uniqueName),
        15000,
      );
    } finally {
      if (pageCdp) {
        pageCdp.close();
      }
      if (downloadsCdp) {
        downloadsCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });
