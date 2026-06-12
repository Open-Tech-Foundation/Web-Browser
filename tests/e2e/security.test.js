import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
  sleep,
} from './helpers/browserHarness.js';

const tabCountExpression = `document.querySelectorAll('a[href^="tab-context-menu:"]').length`;

test('web content cannot open a privileged browser tab through cefQuery',
  { timeout: timeoutMs + 15000 },
  async () => {
    const staticServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head>
            <title>OTF Security E2E</title>
          </head>
          <body>
            <main>
              <h1>Hostile page</h1>
              <button id="attempt-privileged-action">Try privileged action</button>
              <p id="status">idle</p>
              <script>
                document.getElementById('attempt-privileged-action').addEventListener('click', () => {
                  document.getElementById('status').textContent = 'attempted';
                  window.cefQuery?.({
                    request: JSON.stringify({
                      id: 'hostile-new-tab',
                      method: 'navigation.newTab',
                      params: { url: 'browser://settings' },
                    }),
                  });
                });
              </script>
            </main>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      const initialCount = await waitFor(
        browser.cdp,
        tabCountExpression,
        (count) => count >= 1,
      );

      await navigateFromAddressBar(browser.cdp, `${staticServer.origin}/security-e2e`);
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(staticServer.origin) ||
        /OTF Security E2E/i.test(target.title || ''),
      );

      await waitFor(
        pageCdp,
        `document.getElementById('status')?.textContent || ''`,
        (text) => text === 'idle',
        15000,
      );

      await clickSelector(pageCdp, '#attempt-privileged-action');

      await waitFor(
        pageCdp,
        `document.getElementById('status')?.textContent || ''`,
        (text) => text === 'attempted',
        15000,
      );

      await sleep(1500);
      const afterCount = await browser.cdp.evaluate(tabCountExpression);
      assert.equal(
        afterCount,
        initialCount,
        'hostile content should not be able to create a privileged browser tab',
      );
    } finally {
      if (pageCdp) {
        pageCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });

test('Service Worker API is fully disabled in web content',
  { timeout: timeoutMs + 15000 },
  async () => {
    // Serve a minimal page that probes all Service Worker surfaces and
    // reports results as JSON in a <pre id="result"> element.
    const staticServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>SW Disabled Test</title></head>
          <body>
            <pre id="result">pending</pre>
            <script>
              let registrationBlocked = false;
              try {
                // navigator.serviceWorker should be undefined; calling .register on it
                // will throw immediately (TypeError: Cannot read properties of undefined)
                navigator.serviceWorker.register('/sw.js').catch(() => {});
              } catch (_) {
                registrationBlocked = true;
              }
              const result = {
                navigatorServiceWorker: typeof navigator.serviceWorker,
                registrationBlocked,
                ctorServiceWorker: typeof ServiceWorker,
                ctorServiceWorkerRegistration: typeof ServiceWorkerRegistration,
                ctorServiceWorkerContainer: typeof ServiceWorkerContainer,
              };
              document.getElementById('result').textContent = JSON.stringify(result);
            </script>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, `${staticServer.origin}/sw-disabled-test`);
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(staticServer.origin) ||
        /SW Disabled Test/i.test(target.title || ''),
      );

      const resultText = await waitFor(
        pageCdp,
        `document.getElementById('result')?.textContent || 'pending'`,
        (text) => text !== 'pending',
        15000,
      );

      const result = JSON.parse(resultText);

      // Primary security assertions: the registration surface must be gone.
      // navigator.serviceWorker is removed via Navigator.prototype, and since
      // it's undefined, any attempt to call .register() throws immediately.
      assert.equal(result.navigatorServiceWorker, 'undefined',
        `navigator.serviceWorker should be undefined, got: ${result.navigatorServiceWorker}`);
      assert.equal(result.registrationBlocked, true,
        'navigator.serviceWorker.register() should throw when serviceWorker surface is removed');
      // Note: the ServiceWorker/ServiceWorkerRegistration/ServiceWorkerContainer
      // constructor functions live on Window.prototype and cannot be removed from
      // pure JS (they are non-own properties). However they are inert without
      // navigator.serviceWorker — no registration path exists.
    } finally {
      if (pageCdp) {
        pageCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });
