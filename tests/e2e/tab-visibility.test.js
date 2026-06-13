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
import { connectShell } from './helpers/e2eUtils.js';

const nativeRpc = (method, params = {}, id = `vis-rpc-${Date.now()}`) => `
  new Promise((resolve, reject) => {
    window.cefQuery({
      request: JSON.stringify({
        id: ${JSON.stringify(id)},
        method: ${JSON.stringify(method)},
        params: ${JSON.stringify(params)},
      }),
      onSuccess: (json) => {
        try {
          const envelope = JSON.parse(json);
          if (envelope && envelope.ok) {
            resolve(envelope.result);
          } else {
            reject(new Error(envelope?.error?.message || 'RPC failed'));
          }
        } catch (err) {
          resolve(json); // fallback if not wrapped in standard envelope
        }
      },
      onFailure: (code, message) => reject(new Error(message || 'cefQuery failed')),
    });
  })
`;

const tabCountExpression = `document.querySelectorAll('a[href^="tab-context-menu:"]').length`;

test('tab visibility states transition correctly to hidden and visible',
  { timeout: timeoutMs + 30000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      const pageName = req.url.includes('second') ? 'Second Page' : 'First Page';
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>${pageName}</title></head>
          <body>
            <h1>${pageName}</h1>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let shellCdp = null;
    let tab1Cdp = null;
    let tab2Cdp = null;

    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 3000);

      // 1. Navigate active tab to first page
      await navigateFromAddressBar(browser.cdp, `${server.origin}/first`);
      tab1Cdp = await browser.connectToTarget((t) => (t.url || '').startsWith(`${server.origin}/first`), 3000);

      // Verify first page is initially visible
      let state1 = await tab1Cdp.evaluate('document.visibilityState');
      let hidden1 = await tab1Cdp.evaluate('document.hidden');
      assert.equal(state1, 'visible');
      assert.equal(hidden1, false);

      // Get tab 1 context id
      const tab1Id = Number(await shellCdp.evaluate(nativeRpc('tabs.active')));

      // 2. Open second tab
      await clickSelector(browser.cdp, 'a[title="New tab"]');
      await waitFor(browser.cdp, tabCountExpression, (count) => count === 2, 3000);
      await navigateFromAddressBar(browser.cdp, `${server.origin}/second`);
      tab2Cdp = await browser.connectToTarget((t) => (t.url || '').startsWith(`${server.origin}/second`), 3000);

      // Verify second page (active) is visible
      let state2 = await tab2Cdp.evaluate('document.visibilityState');
      let hidden2 = await tab2Cdp.evaluate('document.hidden');
      assert.equal(state2, 'visible');
      assert.equal(hidden2, false);

      // Verify first page (backgrounded) becomes hidden
      await waitFor(tab1Cdp, 'document.visibilityState', (s) => s === 'hidden', 3000);
      assert.equal(await tab1Cdp.evaluate('document.hidden'), true);

      // 3. Switch back to first tab
      await shellCdp.evaluate(nativeRpc('tabs.switch', { tabId: tab1Id }));
      
      // Verify first page is visible again
      await waitFor(tab1Cdp, 'document.visibilityState', (s) => s === 'visible', 3000);
      assert.equal(await tab1Cdp.evaluate('document.hidden'), false);

      // Verify second page is now hidden
      await waitFor(tab2Cdp, 'document.visibilityState', (s) => s === 'hidden', 3000);
      assert.equal(await tab2Cdp.evaluate('document.hidden'), true);

      // 4. Get tab 2 ID from the tab list
      const tabsList = await shellCdp.evaluate(nativeRpc('tabs.list'));
      const tab2Id = tabsList.find(t => (t.url || '').includes('second')).id;

      // 5. Enable split view by clicking/splitting current (Tab 1 is left)
      await shellCdp.evaluate(nativeRpc('split.current'));
      
      // Add Tab 2 to the right split pane
      await shellCdp.evaluate(nativeRpc('split.addTab', { tabId: tab2Id }));
      
      // Wait and verify both tabs are now visible
      await waitFor(tab1Cdp, 'document.visibilityState', (s) => s === 'visible', 3000);
      await waitFor(tab2Cdp, 'document.visibilityState', (s) => s === 'visible', 3000);
      assert.equal(await tab1Cdp.evaluate('document.hidden'), false);
      assert.equal(await tab2Cdp.evaluate('document.hidden'), false);

      // 6. Disable split view and verify backgrounded one gets hidden
      await shellCdp.evaluate(nativeRpc('split.close'));
      
      // tab2 is active, so tab1 should become hidden
      await waitFor(tab1Cdp, 'document.visibilityState', (s) => s === 'hidden', 3000);
      assert.equal(await tab1Cdp.evaluate('document.hidden'), true);
      assert.equal(await tab2Cdp.evaluate('document.visibilityState'), 'visible');

    } finally {
      if (tab1Cdp) tab1Cdp.close();
      if (tab2Cdp) tab2Cdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
      await server.close();
    }
  }
);
