import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

function makeHandler() {
  return (req, res) => {
    if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
    res.end(`<!doctype html><html><body>
      <pre id="result">pending</pre>
      <script>
        (async () => {
          const results = {};

          // javascript: scheme via window.open — must be blocked.
          const jsWin = window.open('javascript:void(0)');
          results.jsOpenBlocked = (jsWin === null || jsWin === undefined);
          if (jsWin) try { jsWin.close(); } catch (_) {}

          // data: scheme via window.open — must be blocked.
          const dataWin = window.open('data:text/html,<h1>x</h1>');
          results.dataOpenBlocked = (dataWin === null || dataWin === undefined);
          if (dataWin) try { dataWin.close(); } catch (_) {}

          // javascript: scheme via iframe.src — must not cause navigation.
          await new Promise((resolve) => {
            const iframe = document.createElement('iframe');
            iframe.hidden = true;
            iframe.src = 'javascript:void(parent.__iframeJsRan=true)';
            document.body.appendChild(iframe);
            setTimeout(() => { iframe.remove(); resolve(); }, 500);
          });
          results.iframeJsBlocked = !window.__iframeJsRan;

          document.getElementById('result').textContent = JSON.stringify(results);
        })();
      </script>
    </body></html>`);
  };
}

test('window.open with dangerous schemes is blocked at the CEF level',
  { timeout: timeoutMs + 10000 },
  async () => {
    const staticServer = await startStaticServer(makeHandler());
    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, staticServer.origin);
      pageCdp = await browser.connectToTarget(
        (t) => (t.url || '').startsWith(staticServer.origin),
      );

      const resultText = await waitFor(
        pageCdp,
        `document.getElementById('result')?.textContent || 'pending'`,
        (t) => t !== 'pending',
        15000,
      );

      const r = JSON.parse(resultText);
      assert.equal(r.jsOpenBlocked, true,
        'window.open(javascript:) must be blocked');
      assert.equal(r.dataOpenBlocked, true,
        'window.open(data:) must be blocked');
      assert.equal(r.iframeJsBlocked, true,
        'iframe.src = javascript: must not execute');
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
      await staticServer.close();
    }
  });
