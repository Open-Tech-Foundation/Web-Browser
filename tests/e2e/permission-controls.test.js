import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  sleep,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

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

async function setSitePermission(cdp, origin, permission, setting) {
  const result = await cefQuery(cdp, `set-permission-for-site:${origin}:${permission}:${setting}`);
  assert.equal(result.ok, true, `set ${permission}=${setting} should succeed: ${result.value}`);
}

async function getDownloads(cdp) {
  const result = await cefQuery(cdp, 'get-downloads');
  assert.equal(result.ok, true, `get-downloads should succeed: ${result.value}`);
  return JSON.parse(result.value || '[]');
}

async function clickByText(cdp, text) {
  const clicked = await cdp.evaluate(`
    (() => {
      const button = [...document.querySelectorAll('button')]
        .find((item) => (item.textContent || '').trim() === ${JSON.stringify(text)});
      if (!button) return false;
      button.click();
      return true;
    })()
  `);
  assert.equal(clicked, true, `expected to click button: ${text}`);
}

test('download site permission block denies downloads and allow permits them',
  { timeout: timeoutMs + 20000 },
  async () => {
    const blockedName = `blocked-download-${Date.now()}.txt`;
    const allowedName = `allowed-download-${Date.now()}.txt`;
    let downloadRequestCount = 0;

    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      const url = new URL(req.url, 'http://127.0.0.1');
      if (url.pathname === '/download') {
        downloadRequestCount += 1;
        const name = url.searchParams.get('name') || 'download.txt';
        res.writeHead(200, {
          'content-type': 'text/plain; charset=utf-8',
          'content-disposition': `attachment; filename="${name}"`,
        });
        res.end(`download ${name}`);
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>Download Permission E2E</title></head>
          <body>
            <a id="blocked-download" href="/download?name=${blockedName}">blocked</a>
            <a id="allowed-download" href="/download?name=${allowedName}">allowed</a>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, server.origin);
      await waitFor(
        browser.cdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        (value) => value.includes(server.origin),
        15000,
      );
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Download Permission E2E/i.test(target.title || ''),
      );

      await waitFor(pageCdp, `!!document.querySelector('#blocked-download')`, Boolean, 15000);

      await setSitePermission(browser.cdp, server.origin, 'downloads', 'block');
      await clickSelector(pageCdp, '#blocked-download');
      await sleep(1000);

      let downloads = await getDownloads(browser.cdp);
      assert.equal(downloads.some((item) => String(item.suggestedName || '').includes(blockedName)), false);

      await setSitePermission(browser.cdp, server.origin, 'downloads', 'allow');
      await clickSelector(pageCdp, '#allowed-download');
      await waitFor(
        browser.cdp,
        `new Promise((resolve) => window.cefQuery?.({
          request: 'get-downloads',
          onSuccess: (json) => {
            try {
              resolve(JSON.parse(json).some((item) =>
                String(item.suggestedName || '').includes(${JSON.stringify(allowedName)}) &&
                !item.isInProgress
              ));
            } catch {
              resolve(false);
            }
          },
          onFailure: () => resolve(false),
        }))`,
        Boolean,
        15000,
      );

      downloads = await getDownloads(browser.cdp);
      assert.equal(downloads.some((item) => String(item.suggestedName || '').includes(allowedName)), true);
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

test('popup site permission prompts once, blocks, and allows by site setting',
  { timeout: timeoutMs + 20000 },
  async () => {
    let popupRequestCount = 0;
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      const url = new URL(req.url, 'http://127.0.0.1');
      if (url.pathname === '/popup') {
        popupRequestCount += 1;
        res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
        res.end(`<!doctype html>
          <html>
            <head><title>Popup Target ${url.searchParams.get('case') || ''}</title></head>
            <body><main>Popup target ${url.searchParams.get('case') || ''}</main></body>
          </html>`);
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>Popup Permission E2E</title></head>
          <body>
            <button id="ask-popup" onclick="window.open('/popup?case=ask')">ask</button>
            <button id="blocked-popup" onclick="window.open('/popup?case=blocked')">blocked</button>
            <button id="allowed-popup" onclick="window.open('/popup?case=allowed')">allowed</button>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    let blockedPopupCdp = null;
    let popupCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, server.origin);
      await waitFor(
        browser.cdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        (value) => value.includes(server.origin),
        15000,
      );
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Popup Permission E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#ask-popup')`, Boolean, 15000);

      await setSitePermission(browser.cdp, server.origin, 'popup', 'ask');
      await clickSelector(pageCdp, '#ask-popup');
      await waitFor(browser.cdp, `!!document.querySelector('button[title="Pop-up blocked"]')`, Boolean, 15000);
      await clickSelector(browser.cdp, 'button[title="Pop-up blocked"]');
      blockedPopupCdp = await browser.connectToTarget((target) =>
        /blockedpopup\.html/i.test(target.url || '') ||
        /Pop-up blocked/i.test(target.title || ''),
        15000,
      );
      await waitFor(
        blockedPopupCdp,
        `document.body?.innerText || ""`,
        (text) => text.includes(server.origin) && text.includes('Allow once'),
        15000,
      );
      await clickByText(blockedPopupCdp, 'Allow once');
      popupCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/popup?case=ask`) ||
        /Popup Target ask/i.test(target.title || ''),
        15000,
      );
      await waitFor(popupCdp, `document.body?.innerText || ""`, (text) => text.includes('Popup target ask'), 15000);
      popupCdp.close();
      popupCdp = null;
      blockedPopupCdp.close();
      blockedPopupCdp = null;

      await setSitePermission(browser.cdp, server.origin, 'popup', 'block');
      const beforeBlocked = popupRequestCount;
      await clickSelector(pageCdp, '#blocked-popup');
      await sleep(1000);
      assert.equal(popupRequestCount, beforeBlocked, 'blocked popup should not reach the test server');

      await setSitePermission(browser.cdp, server.origin, 'popup', 'allow');
      await clickSelector(pageCdp, '#allowed-popup');
      popupCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/popup?case=allowed`) ||
        /Popup Target allowed/i.test(target.title || ''),
        15000,
      );
      await waitFor(popupCdp, `document.body?.innerText || ""`, (text) => text.includes('Popup target allowed'), 15000);
    } finally {
      if (popupCdp) popupCdp.close();
      if (blockedPopupCdp) blockedPopupCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });
