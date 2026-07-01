import test from 'node:test';
import assert from 'node:assert/strict';

import {
  CdpClient, launchDevBrowser, navigateFromAddressBar, startStaticServer,
  timeoutMs, waitFor, waitForTarget,
} from './helpers/browserHarness.js';

function otf(cdp, method, params = {}) {
  const call = JSON.stringify({ method, params });
  return cdp.evaluate(`
    new Promise((resolve, reject) => {
      const { method, params } = ${call};
      const id='w'+Math.random(); const prev=window.__otfReceive;
      const timer=setTimeout(()=>{window.__otfReceive=prev;reject(new Error('timeout'));},8000);
      window.__otfReceive=(m)=>{try{const o=JSON.parse(m);if(o&&o.id===id){clearTimeout(timer);window.__otfReceive=prev;resolve(o);return;}}catch(e){}if(prev)prev(m);};
      window.otf.postMessage(JSON.stringify({id,method,params}));
    })`).catch((e) => ({ err: String(e) }));
}

async function navToTarget(browser, url, match) {
  let target = null;
  for (let i = 0; i < 5 && !target; i++) {
    await navigateFromAddressBar(browser.cdp, url);
    target = await waitForTarget(match, 3000).catch(() => null);
  }
  assert.ok(target, `should navigate to ${url}`);
  const cdp = new CdpClient(target.webSocketDebuggerUrl);
  await cdp.open();
  await cdp.send('Runtime.enable');
  return cdp;
}

// Each workspace is its own storage context: a cookie set by a page in one
// workspace must be invisible to the same origin loaded in another workspace.
test('cookies are isolated between workspaces',
  { timeout: timeoutMs + 25000 },
  async () => {
    const browser = await launchDevBrowser();
    let web = null;
    let page1 = null;
    let page2 = null;
    try {
      web = await startStaticServer((_req, res) => {
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end('<!doctype html><title>iso</title><body>iso</body>');
      });

      // Workspace 1 (default): set a cookie on the origin.
      page1 = await navToTarget(browser, `${web.origin}/w1`, (t) => (t.url || '').includes('/w1'));
      const set = await page1.evaluate(`document.cookie = 'iso=ws1; path=/'; document.cookie`);
      assert.match(set, /iso=ws1/, 'cookie set in workspace 1');

      // New workspace → it becomes active with its own fresh tab.
      let created = null;
      for (let i = 0; i < 3 && !(created && created.ok); i++) {
        created = await otf(browser.cdp, 'workspaces.create', { name: `W2-${i}` });
      }
      assert.ok(created && created.ok, 'workspace created');

      // Same origin, different workspace: the cookie must not be visible.
      page2 = await navToTarget(browser, `${web.origin}/w2`, (t) => (t.url || '').includes('/w2'));
      const seen = await waitFor(page2, 'document.cookie', () => true, 4000);
      assert.doesNotMatch(seen || '', /iso=ws1/, 'workspace 2 must not see workspace 1 cookies');
    } finally {
      if (page1) page1.close();
      if (page2) page2.close();
      if (web) await web.close();
      await browser.close();
    }
  });
