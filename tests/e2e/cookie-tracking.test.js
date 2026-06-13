import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('native cookie tracking observes server, HttpOnly, and JavaScript cookie writes',
  { timeout: timeoutMs + 30000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      res.writeHead(200, {
        'content-type': 'text/html; charset=utf-8',
        'set-cookie': [
          'otf_server_cookie=server; Path=/; SameSite=Lax',
          'otf_http_only_cookie=secret; Path=/; HttpOnly; SameSite=Lax',
        ],
      });
      res.end(`<!doctype html>
        <title>cookie tracking</title>
        <script>
          document.cookie = 'otf_js_cookie=js; Path=/; SameSite=Lax';
        </script>
        <main>cookie tracking</main>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, `${server.origin}/cookies`);
      pageCdp = await browser.connectToTarget(
        (target) => (target.url || '').includes('/cookies'),
        15000,
      );
      await waitFor(pageCdp, 'document.readyState', (state) => state === 'complete', 15000);

      // Exercise an explicit JS write after page load as well as the inline
      // script write above. Native cookie inspection should see both.
      await pageCdp.evaluate(`
        (() => {
          document.cookie = 'otf_js_late_cookie=late; Path=/; SameSite=Lax';
          return document.cookie;
        })()
      `);

      const result = await waitFor(
        browser.cdp,
        `new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'e2e-cookie-tracking-list',
              method: 'cookieTracking.list',
              params: {},
            }),
            onSuccess: (response) => {
              try {
                const parsed = JSON.parse(response);
                resolve(parsed?.ok
                  ? { ok: true, response: JSON.stringify(parsed.result) }
                  : { ok: false, error: parsed?.error?.message || 'rpc failed' });
              } catch (_) {
                resolve({ ok: false, error: 'bad json' });
              }
            },
            onFailure: (code, message) => resolve({ ok: false, error: message || String(code) }),
          });
        })`,
        (value) => value?.ok && JSON.parse(value.response).some((cookie) => cookie.name === 'otf_server_cookie'),
        15000,
      );
      const tracked = JSON.parse(result.response);
      const byName = new Map(tracked.map((cookie) => [cookie.name, cookie]));

      assert.ok(byName.has('otf_server_cookie'), 'server Set-Cookie should be tracked');
      assert.ok(byName.has('otf_http_only_cookie'), 'HttpOnly Set-Cookie should be tracked');
      assert.equal(byName.get('otf_http_only_cookie').httpOnly, true);
      assert.ok(byName.has('otf_js_cookie'), 'inline document.cookie write should be tracked');
      assert.ok(byName.has('otf_js_late_cookie'), 'post-load document.cookie write should be tracked');
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });
