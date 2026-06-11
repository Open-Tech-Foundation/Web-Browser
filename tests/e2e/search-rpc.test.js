import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { connectShell } from './helpers/e2eUtils.js';

const nativeRpc = (method, params = {}, id = `search-rpc-${Date.now()}`) => `
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

test('search RPC saves history and returns suggestions',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 15000);

      const query = `otf-search-rpc-${Date.now()}`;
      const save = JSON.parse(await shellCdp.evaluate(
        nativeRpc('search.history.add', { query }, 'search-save'),
      ));
      assert.equal(save.id, 'search-save');
      assert.equal(save.ok, true);

      const suggestions = JSON.parse(await shellCdp.evaluate(
        nativeRpc('search.suggestions', {
          prefix: query.slice(0, 'otf-search-rpc-'.length),
          limit: 10,
        }, 'search-suggest'),
      ));
      assert.equal(suggestions.id, 'search-suggest');
      assert.equal(suggestions.ok, true);
      assert.ok(suggestions.result.includes(query));
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('search RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 15000);

      const response = JSON.parse(await shellCdp.evaluate(
        nativeRpc('search.suggestions', {
          prefix: 'otf',
          limit: 10,
          extra: true,
        }, 'search-extra-param'),
      ));
      assert.equal(response.id, 'search-extra-param');
      assert.equal(response.ok, false);
      assert.match(response.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });
