import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
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
  const result = await cefQuery(
    cdp,
    `set-permission-for-site:${origin}:${permission}:${setting}`,
  );
  assert.equal(
    result.ok,
    true,
    `set ${permission}=${setting} should succeed: ${result.value}`,
  );
}

test(
  'javascript site permission blocks JS in new tabs',
  { timeout: timeoutMs + 20000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>JS Permission E2E</title></head>
          <body>
            <div id="js-output">No JS</div>
            <script>
              document.getElementById('js-output').textContent = 'JavaScript ran';
            </script>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let allowedCdp = null;
    let blockedCdp = null;
    try {
      const tagA = 'js-allow-' + String(Date.now());
      const tagB = 'js-block-' + String(Date.now());

      // 1. Open a new tab with JS allowed (default).
      const resA = await cefQuery(browser.cdp, `new-tab:${server.origin}/?tag=${tagA}`);
      assert.equal(resA.ok, true);
      allowedCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes(tagA),
        15000,
      );
      await waitFor(
        allowedCdp,
        '!!document.querySelector("#js-output")',
        Boolean,
        15000,
      );
      const jsRan = await allowedCdp.evaluate(
        'document.getElementById("js-output").textContent',
      );
      assert.equal(jsRan, 'JavaScript ran', 'JS should work by default');

      // 2. Set JS to "block" for the origin.
      await setSitePermission(browser.cdp, server.origin, 'javascript', 'block');

      // 3. Open another new tab — JS should be blocked.
      const resB = await cefQuery(browser.cdp, `new-tab:${server.origin}/?tag=${tagB}`);
      assert.equal(resB.ok, true);
      blockedCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes(tagB),
        15000,
      );
      await waitFor(
        blockedCdp,
        '!!document.querySelector("#js-output")',
        Boolean,
        15000,
      );
      const jsBlocked = await blockedCdp.evaluate(
        'document.getElementById("js-output").textContent',
      );
      assert.equal(jsBlocked, 'No JS', 'JS should be blocked in new tab');
    } finally {
      if (blockedCdp) blockedCdp.close();
      if (allowedCdp) allowedCdp.close();
      await browser.close();
      await server.close();
    }
  },
);

test(
  'javascript site permission allows JS when set to allow',
  { timeout: timeoutMs + 20000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>JS Allow E2E</title></head>
          <body>
            <div id="js-output">No JS</div>
            <script>
              document.getElementById('js-output').textContent = 'JavaScript ran';
            </script>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let newTabCdp = null;
    try {
      await setSitePermission(browser.cdp, server.origin, 'javascript', 'allow');

      const tag = 'js-allow-' + String(Date.now());
      const res = await cefQuery(browser.cdp, `new-tab:${server.origin}/?tag=${tag}`);
      assert.equal(res.ok, true);

      newTabCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes(tag),
        15000,
      );
      await waitFor(
        newTabCdp,
        '!!document.querySelector("#js-output")',
        Boolean,
        15000,
      );

      const jsRan = await newTabCdp.evaluate(
        'document.getElementById("js-output").textContent',
      );
      assert.equal(
        jsRan,
        'JavaScript ran',
        'JS should run when permission is allow',
      );
    } finally {
      if (newTabCdp) newTabCdp.close();
      await browser.close();
      await server.close();
    }
  },
);
