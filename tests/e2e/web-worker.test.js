import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

// Serves a page + an inline worker script via query param so a single
// http.createServer handles everything without a filesystem worker file.
function makeHandler() {
  return (req, res) => {
    if (req.url === '/favicon.ico') {
      res.writeHead(204);
      res.end();
      return;
    }

    if (req.url === '/worker.js') {
      // Classic worker: receives a message, does arithmetic, posts result back.
      res.writeHead(200, { 'content-type': 'application/javascript' });
      res.end(`
        self.onmessage = (e) => {
          const { op, a, b } = e.data;
          let result;
          if (op === 'add')      result = a + b;
          else if (op === 'mul') result = a * b;
          else result = null;
          self.postMessage({ op, result });
        };
      `);
      return;
    }

    // Main page: spawns a classic Worker, runs a few operations, writes
    // results as JSON into <pre id="result">.
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
    res.end(`<!doctype html>
      <html>
        <head><title>Web Worker Test</title></head>
        <body>
          <pre id="result">pending</pre>
          <script>
            (async () => {
              const results = {};

              // Test 1: Worker constructor exists.
              results.workerCtorPresent = typeof Worker === 'function';

              if (!results.workerCtorPresent) {
                document.getElementById('result').textContent = JSON.stringify(results);
                return;
              }

              // Test 2: Worker spawns and responds correctly.
              const workerResult = await new Promise((resolve) => {
                const w = new Worker('/worker.js');
                const received = {};

                w.onerror = (e) => {
                  results.workerSpawnError = e.message || String(e);
                  w.terminate();
                  resolve(null);
                };

                w.onmessage = (e) => {
                  const { op, result } = e.data;
                  received[op] = result;
                  if ('add' in received && 'mul' in received) {
                    w.terminate();
                    resolve(received);
                  }
                };

                w.postMessage({ op: 'add', a: 3, b: 4 });
                w.postMessage({ op: 'mul', a: 6, b: 7 });

                setTimeout(() => {
                  results.workerTimeout = true;
                  w.terminate();
                  resolve(null);
                }, 5000);
              });

              if (workerResult) {
                results.addResult = workerResult.add;
                results.mulResult = workerResult.mul;
              }

              document.getElementById('result').textContent = JSON.stringify(results);
            })();
          </script>
        </body>
      </html>`);
  };
}

test('classic Web Worker spawns and communicates correctly',
  { timeout: timeoutMs + 15000 },
  async () => {
    const staticServer = await startStaticServer(makeHandler());
    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, `${staticServer.origin}/worker-test`);
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(staticServer.origin) ||
        /Web Worker Test/i.test(target.title || ''),
      );

      const resultText = await waitFor(
        pageCdp,
        `document.getElementById('result')?.textContent || 'pending'`,
        (text) => text !== 'pending',
        20000,
      );

      const r = JSON.parse(resultText);

      assert.equal(r.workerCtorPresent, true,
        'Worker constructor should be present');
      assert.equal(r.workerSpawnError, undefined,
        `Worker should spawn without error, got: ${r.workerSpawnError}`);
      assert.equal(r.workerTimeout, undefined,
        'Worker should respond within timeout');
      assert.equal(r.addResult, 7,
        `Worker add(3,4) should return 7, got: ${r.addResult}`);
      assert.equal(r.mulResult, 42,
        `Worker mul(6,7) should return 42, got: ${r.mulResult}`);
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
      await staticServer.close();
    }
  });
