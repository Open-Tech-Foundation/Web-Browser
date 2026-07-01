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
      const id='p'+Math.random(); const prev=window.__otfReceive;
      const timer=setTimeout(()=>{window.__otfReceive=prev;reject(new Error('timeout'));},8000);
      window.__otfReceive=(m)=>{try{const o=JSON.parse(m);if(o&&o.id===id){clearTimeout(timer);window.__otfReceive=prev;resolve(o);return;}}catch(e){}if(prev)prev(m);};
      window.otf.postMessage(JSON.stringify({id,method,params}));
    })`).catch((e) => ({ err: String(e) }));
}

async function connectPage(match) {
  const target = await waitForTarget(match);
  const cdp = new CdpClient(target.webSocketDebuggerUrl);
  await cdp.open();
  await cdp.send('Runtime.enable');
  return cdp;
}

// Private tabs use the workspace's shared in-memory context: isolated from the
// workspace's persistent data, but sharing storage with each other.
test('private tabs are isolated from persistent data and share with each other',
  { timeout: timeoutMs + 25000 },
  async () => {
    const browser = await launchDevBrowser();
    let normal = null;
    let priv1 = null;
    let priv2 = null;
    try {
      const web = await startStaticServer((_req, res) => {
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end('<!doctype html><title>pt</title><body>pt</body>');
      });

      // A normal (persistent) tab sets a cookie on the origin.
      let target = null;
      for (let i = 0; i < 5 && !target; i++) {
        await navigateFromAddressBar(browser.cdp, `${web.origin}/n`);
        target = await waitForTarget((t) => (t.url || '').includes('/n'), 3000).catch(() => null);
      }
      assert.ok(target, 'normal tab navigated');
      normal = new CdpClient(target.webSocketDebuggerUrl);
      await normal.open();
      await normal.send('Runtime.enable');
      await normal.evaluate(`document.cookie = 'persist=1; path=/'`);

      // A private tab in the same workspace, same origin: must NOT see the
      // persistent cookie; it is flagged private.
      const p1 = await otf(browser.cdp, 'navigation.newPrivateTab', { url: `${web.origin}/p` });
      assert.equal(p1.ok, true);
      const list = await otf(browser.cdp, 'tabs.list');
      assert.ok(list.result.some((t) => t.id === p1.result.tabId && t.private === true),
        'new tab is marked private');
      priv1 = await connectPage((t) => (t.url || '').includes('/p'));
      const seenInP1 = await waitFor(priv1, 'document.cookie', () => true, 4000);
      assert.doesNotMatch(seenInP1 || '', /persist=1/, 'private tab must not see persistent cookies');
      await priv1.evaluate(`document.cookie = 'shared=1; path=/'`);

      // A second private tab in the same workspace shares the private session.
      const p2 = await otf(browser.cdp, 'navigation.newPrivateTab', { url: `${web.origin}/q` });
      assert.equal(p2.ok, true);
      priv2 = await connectPage((t) => (t.url || '').includes('/q'));
      const seenInP2 = await waitFor(priv2, 'document.cookie',
        (c) => /shared=1/.test(c || ''), 4000).catch(() => '');
      assert.match(seenInP2 || '', /shared=1/, 'private tabs in a workspace share storage');
      assert.doesNotMatch(seenInP2 || '', /persist=1/, 'private session stays isolated from persistent');

      await web.close();
    } finally {
      for (const c of [normal, priv1, priv2]) if (c) c.close();
      await browser.close();
    }
  });
