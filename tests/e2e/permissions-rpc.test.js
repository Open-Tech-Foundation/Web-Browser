import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { connectShell } from './helpers/e2eUtils.js';

const nativeRpc = (method, params = {}, id = `permissions-rpc-${Date.now()}`) => `
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

test('permissions RPC accepts structured download allow decision',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 15000);

      const response = JSON.parse(await shellCdp.evaluate(
        nativeRpc('permissions.download.allow', {
          origin: 'https://example.com:443',
        }, 'permissions-download-allow'),
      ));
      assert.equal(response.id, 'permissions-download-allow');
      assert.equal(response.ok, true);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('permissions RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 15000);

      const response = JSON.parse(await shellCdp.evaluate(
        nativeRpc('permissions.popup.alwaysAllow', {
          id: 1,
          origin: 'https://example.com',
          extra: true,
        }, 'permissions-extra-param'),
      ));
      assert.equal(response.id, 'permissions-extra-param');
      assert.equal(response.ok, false);
      assert.match(response.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });
