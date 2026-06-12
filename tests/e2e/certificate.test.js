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
import { connectShell, waitForAddress } from './helpers/e2eUtils.js';

const nativeRpc = (method, params = {}, id = `certificate-rpc-${Date.now()}`) => `
  new Promise((resolve) => {
    window.cefQuery({
      request: JSON.stringify({
        id: ${JSON.stringify(id)},
        method: ${JSON.stringify(method)},
        params: ${JSON.stringify(params)},
      }),
      onSuccess: resolve,
      onFailure: (code, message) => resolve(JSON.stringify({
        id: ${JSON.stringify(id)},
        ok: false,
        error: { code: String(code), message },
      })),
    });
  })
`;

test('certificate viewer reports no certificate for an HTTP page',
  { timeout: timeoutMs + 15000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><title>HTTP Certificate E2E</title><main>HTTP Certificate E2E</main>');
    });
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let certCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await navigateFromAddressBar(shellCdp, `${server.origin}/cert`);
      await waitForAddress(shellCdp, (value) => value.includes('/cert'));
      await waitFor(shellCdp, `!!document.querySelector('button[title="Security warning"]')`, Boolean, 15000);
      await clickSelector(shellCdp, 'button[title="Security warning"]');

      certCdp = await browser.connectToTarget((target) =>
        /certificate\.html/i.test(target.url || '') ||
        /SSL Certificate Viewer/i.test(target.title || ''),
        15000,
      );
      const text = await waitFor(
        certCdp,
        `document.body.innerText`,
        (value) => value.includes('No certificate available for current tab'),
        15000,
      );
      assert.ok(text.includes('No certificate available'));
    } finally {
      if (certCdp) certCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
      await server.close();
    }
  });

test('certificate RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 15000);

      const response = JSON.parse(await shellCdp.evaluate(
        nativeRpc('ui.certificate.get', { tabId: 0, extra: true }, 'certificate-extra-param'),
      ));
      assert.equal(response.id, 'certificate-extra-param');
      assert.equal(response.ok, false);
      assert.match(response.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });
