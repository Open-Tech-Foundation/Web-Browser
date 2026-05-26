import test from 'node:test';
import assert from 'node:assert/strict';

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
      if (url.pathname === '/redirect-http') {
        res.writeHead(302, { location: '/popup?case=http-redirect' });
        res.end();
        return;
      }
      if (url.pathname === '/redirect-js') {
        popupRequestCount += 1;
        res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
        res.end(`<!doctype html>
          <html>
            <head><title>JS Redirect Start</title></head>
            <body><script>location.href = '/popup?case=js-redirect';</script></body>
          </html>`);
        return;
      }
      if (url.pathname === '/storage-target') {
        popupRequestCount += 1;
        res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
        res.end(`<!doctype html>
          <html>
            <head><title>Storage Target</title></head>
            <body>
              <main id="storage"></main>
              <script>
                document.getElementById('storage').textContent =
                  document.cookie + '|' + localStorage.getItem('popup-storage');
              </script>
            </body>
          </html>`);
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>Popup Permission E2E</title></head>
          <body>
            <input id="key-popup" aria-label="key popup">
            <button id="ask-popup" onclick="window.open('/popup?case=ask', '_blank', 'popup=yes,width=600,height=700')">ask</button>
            <button id="blocked-popup" onclick="window.open('/popup?case=blocked', '_blank', 'popup=yes,width=600,height=700')">blocked</button>
            <button id="expired-popup" onclick="setTimeout(() => window.open('/popup?case=expired', '_blank', 'popup=yes,width=600,height=700'), 5500)">expired</button>
            <button id="small-popup" onclick="window.open('/popup?case=small', '_blank', 'popup=yes,width=100,height=100')">small</button>
            <button id="wide-popup" onclick="window.open('/popup?case=wide', '_blank', 'popup=yes,width=99999,height=700')">wide</button>
            <button id="tall-popup" onclick="window.open('/popup?case=tall', '_blank', 'popup=yes,width=600,height=99999')">tall</button>
            <button id="unknown-feature-popup" onclick="window.open('/popup?case=unknown-feature', '_blank', 'popup=yes,width=600,height=700,unknownfeature=yes')">unknown</button>
            <button id="named-one" onclick="window.open('/popup?case=named-one', 'named-popup', 'popup=yes,width=600,height=700')">named one</button>
            <button id="named-two" onclick="window.open('/popup?case=named-two', 'named-popup', 'popup=yes,width=600,height=700')">named two</button>
            <button id="http-redirect-popup" onclick="window.open('/redirect-http', '_blank', 'popup=yes,width=600,height=700')">http redirect</button>
            <button id="js-redirect-popup" onclick="window.open('/redirect-js', '_blank', 'popup=yes,width=600,height=700')">js redirect</button>
            <button id="storage-popup" onclick="document.cookie = 'popup_cookie=parent'; localStorage.setItem('popup-storage', 'parent-storage'); window.open('/storage-target', '_blank', 'popup=yes,width=600,height=700')">storage</button>
            <button id="allowed-popup" onclick="window.open('/popup?case=allowed', '_blank', 'popup=yes,width=600,height=700')">allowed</button>
            <script>
              document.getElementById('key-popup').addEventListener('keydown', (event) => {
                if (event.key === 'Enter') window.open('/popup?case=key', '_blank', 'popup=yes,width=600,height=700');
              });
            </script>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    let blockedPopupCdp = null;
    let popupCdp = null;
    try {
      const isPermissionPageTarget = (target) => {
        const targetUrl = target.url || '';
        return targetUrl === server.origin ||
          targetUrl === `${server.origin}/` ||
          /Popup Permission E2E/i.test(target.title || '');
      };

      await navigateFromAddressBar(browser.cdp, server.origin);
      await waitFor(
        browser.cdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        // AddressBar strips the http:// prefix for display when unfocused, so
        // match against the host[:port] portion only.
        (value) => value.includes(server.origin.replace(/^https?:\/\//, '')),
        15000,
      );
      pageCdp = await browser.connectToTarget(isPermissionPageTarget);
      await waitFor(pageCdp, `!!document.querySelector('#ask-popup')`, Boolean, 15000);

      await setSitePermissionFromUi(browser, server.origin, 'popup', 'ask');
      await navigateFromAddressBar(browser.cdp, server.origin);
      if (pageCdp) pageCdp.close();
      pageCdp = await browser.connectToTarget(isPermissionPageTarget);
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
      pageCdp = await browser.connectToTarget(isPermissionPageTarget);
      await waitFor(pageCdp, `!!document.querySelector('#blocked-popup')`, Boolean, 15000);
      const beforeBlocked = popupRequestCount;
      await clickSelector(pageCdp, '#blocked-popup');
      await sleep(1000);
      assert.equal(popupRequestCount, beforeBlocked, 'blocked popup should not reach the test server');

      await setSitePermissionFromUi(browser, server.origin, 'popup', 'allow');
      await navigateFromAddressBar(browser.cdp, server.origin);
      if (pageCdp) pageCdp.close();
      pageCdp = await browser.connectToTarget(isPermissionPageTarget);
      await waitFor(
        pageCdp,
        `!!document.querySelector('#key-popup') && !!document.querySelector('#expired-popup') && !!document.querySelector('#small-popup') && !!document.querySelector('#wide-popup') && !!document.querySelector('#tall-popup') && !!document.querySelector('#unknown-feature-popup') && !!document.querySelector('#named-one') && !!document.querySelector('#named-two') && !!document.querySelector('#http-redirect-popup') && !!document.querySelector('#js-redirect-popup') && !!document.querySelector('#storage-popup') && !!document.querySelector('#allowed-popup')`,
        Boolean,
        15000,
      );

      await clickSelector(pageCdp, '#key-popup');
      await pressKey(pageCdp, 'Enter');
      popupCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/popup?case=key`) ||
        /Popup Target key/i.test(target.title || ''),
        15000,
      );
      await waitFor(popupCdp, `document.body?.innerText || ""`, (text) => text.includes('Popup target key'), 15000);
      popupCdp.close();
      popupCdp = null;

      const beforeExpired = popupRequestCount;
      await clickSelector(pageCdp, '#expired-popup');
      await sleep(6500);
      assert.equal(popupRequestCount, beforeExpired, 'expired activation popup should not reach the test server');

      const beforeSmall = popupRequestCount;
      await clickSelector(pageCdp, '#small-popup');
      await sleep(1000);
      assert.equal(popupRequestCount, beforeSmall, 'too-small popup should not reach the test server');

      const beforeWide = popupRequestCount;
      await clickSelector(pageCdp, '#wide-popup');
      await sleep(1000);
      assert.equal(popupRequestCount, beforeWide, 'too-wide popup should not reach the test server');

      const beforeTall = popupRequestCount;
      await clickSelector(pageCdp, '#tall-popup');
      await sleep(1000);
      assert.equal(popupRequestCount, beforeTall, 'too-tall popup should not reach the test server');

      await clickSelector(pageCdp, '#unknown-feature-popup');
      popupCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/popup?case=unknown-feature`) ||
        /Popup Target unknown-feature/i.test(target.title || ''),
        15000,
      );
      await waitFor(popupCdp, `document.body?.innerText || ""`, (text) => text.includes('Popup target unknown-feature'), 15000);
      popupCdp.close();
      popupCdp = null;

      await clickSelector(pageCdp, '#named-one');
      const namedOneCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/popup?case=named-one`) ||
        /Popup Target named-one/i.test(target.title || ''),
        15000,
      );
      await waitFor(namedOneCdp, `document.body?.innerText || ""`, (text) => text.includes('Popup target named-one'), 15000);
      await clickSelector(pageCdp, '#named-two');
      const namedTwoCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/popup?case=named-two`) ||
        /Popup Target named-two/i.test(target.title || ''),
        15000,
      );
      await waitFor(namedTwoCdp, `document.body?.innerText || ""`, (text) => text.includes('Popup target named-two'), 15000);
      await waitFor(namedOneCdp, `document.body?.innerText || ""`, (text) => text.includes('Popup target named-one'), 15000);
      namedOneCdp.close();
      namedTwoCdp.close();

      await clickSelector(pageCdp, '#http-redirect-popup');
      popupCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/popup?case=http-redirect`) ||
        /Popup Target http-redirect/i.test(target.title || ''),
        15000,
      );
      await waitFor(popupCdp, `document.body?.innerText || ""`, (text) => text.includes('Popup target http-redirect'), 15000);
      popupCdp.close();
      popupCdp = null;

      await clickSelector(pageCdp, '#js-redirect-popup');
      popupCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/popup?case=js-redirect`) ||
        /Popup Target js-redirect/i.test(target.title || ''),
        15000,
      );
      await waitFor(popupCdp, `document.body?.innerText || ""`, (text) => text.includes('Popup target js-redirect'), 15000);
      popupCdp.close();
      popupCdp = null;

      await clickSelector(pageCdp, '#storage-popup');
      popupCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/storage-target`) ||
        /Storage Target/i.test(target.title || ''),
        15000,
      );
      await waitFor(
        popupCdp,
        `document.querySelector('#storage')?.textContent || ""`,
        (text) => text.includes('popup_cookie=parent') && text.includes('parent-storage'),
        15000,
      );
      popupCdp.close();
      popupCdp = null;

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
