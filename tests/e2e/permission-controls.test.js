import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  sleep,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { setSitePermissionFromUi } from './helpers/e2eUtils.js';

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
    let downloadsCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, server.origin);
      await waitFor(
        browser.cdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        // AddressBar strips the http:// prefix for display when unfocused, so
        // match against the host[:port] portion only.
        (value) => value.includes(server.origin.replace(/^https?:\/\//, '')),
        15000,
      );
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Download Permission E2E/i.test(target.title || ''),
      );

      await waitFor(pageCdp, `!!document.querySelector('#blocked-download')`, Boolean, 15000);

      await setSitePermissionFromUi(browser, server.origin, 'downloads', 'block');
      await navigateFromAddressBar(browser.cdp, server.origin);
      if (pageCdp) pageCdp.close();
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Download Permission E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#blocked-download')`, Boolean, 15000);
      await clickSelector(pageCdp, '#blocked-download');
      await sleep(1000);

      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Downloads' ||
        /downloads\.html/i.test(target.url || ''),
      );
      await waitFor(
        downloadsCdp,
        `document.body.innerText`,
        (text) => !text.includes(blockedName),
        15000,
      );
      downloadsCdp.close();
      downloadsCdp = null;

      await setSitePermissionFromUi(browser, server.origin, 'downloads', 'allow');
      await navigateFromAddressBar(browser.cdp, server.origin);
      if (pageCdp) pageCdp.close();
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Download Permission E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#allowed-download')`, Boolean, 15000);
      await clickSelector(pageCdp, '#allowed-download');
      await sleep(1000);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Downloads' ||
        /downloads\.html/i.test(target.url || ''),
      );
      await waitFor(
        downloadsCdp,
        `document.body.innerText`,
        (text) => text.includes(allowedName),
        15000,
      );
    } finally {
      if (downloadsCdp) downloadsCdp.close();
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
        // AddressBar strips the http:// prefix for display when unfocused, so
        // match against the host[:port] portion only.
        (value) => value.includes(server.origin.replace(/^https?:\/\//, '')),
        15000,
      );
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Popup Permission E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#ask-popup')`, Boolean, 15000);

      await setSitePermissionFromUi(browser, server.origin, 'popup', 'ask');
      await navigateFromAddressBar(browser.cdp, server.origin);
      if (pageCdp) pageCdp.close();
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Popup Permission E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#ask-popup')`, Boolean, 15000);
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
      await clickByText(blockedPopupCdp, 'button', 'Allow once');
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

      await setSitePermissionFromUi(browser, server.origin, 'popup', 'block');
      await navigateFromAddressBar(browser.cdp, server.origin);
      if (pageCdp) pageCdp.close();
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Popup Permission E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#blocked-popup')`, Boolean, 15000);
      const beforeBlocked = popupRequestCount;
      await clickSelector(pageCdp, '#blocked-popup');
      await sleep(1000);
      assert.equal(popupRequestCount, beforeBlocked, 'blocked popup should not reach the test server');

      await setSitePermissionFromUi(browser, server.origin, 'popup', 'allow');
      await navigateFromAddressBar(browser.cdp, server.origin);
      if (pageCdp) pageCdp.close();
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Popup Permission E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#allowed-popup')`, Boolean, 15000);
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
