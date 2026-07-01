import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

import {
  CdpClient, launchDevBrowser, navigateFromAddressBar, sleep, startStaticServer,
  timeoutMs, waitFor, waitForTarget,
} from './helpers/browserHarness.js';

function otf(cdp, method, params = {}) {
  const call = JSON.stringify({ method, params });
  return cdp.evaluate(`
    new Promise((resolve, reject) => {
      const { method, params } = ${call};
      const id='d'+Math.random(); const prev=window.__otfReceive;
      const timer=setTimeout(()=>{window.__otfReceive=prev;reject(new Error('timeout'));},8000);
      window.__otfReceive=(m)=>{try{const o=JSON.parse(m);if(o&&o.id===id){clearTimeout(timer);window.__otfReceive=prev;resolve(o);return;}}catch(e){}if(prev)prev(m);};
      window.otf.postMessage(JSON.stringify({id,method,params}));
    })`).catch((e) => ({ err: String(e) }));
}

// Deleting a workspace tears down its context and marks its data; the directory
// is fully wiped on the next launch (async, restart-completed).
test('deleting a workspace wipes its data on next launch',
  { timeout: timeoutMs + 40000 },
  async () => {
    const root = await mkdtemp(path.join(os.tmpdir(), 'otf-wipe-'));
    const userDataDir = path.join(root, 'ud');
    const web = await startStaticServer((_req, res) => {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.end('<!doctype html><title>x</title><body>x</body>');
    });
    let wsDir = null;
    try {
      // Launch 1: create a workspace, give its tab some storage, then delete it.
      const b1 = await launchDevBrowser({ userDataDir, preserveProfile: true });
      try {
        let created = null;
        for (let i = 0; i < 3 && !(created && created.ok); i++) {
          created = await otf(b1.cdp, 'workspaces.create', { name: `W2-${i}` });
        }
        assert.ok(created && created.ok, 'workspace created');
        const wsId = created.result.id;
        wsDir = path.join(userDataDir, 'workspaces', String(wsId));

        // Navigate the new workspace's tab so its storage directory is written.
        let target = null;
        for (let i = 0; i < 5 && !target; i++) {
          await navigateFromAddressBar(b1.cdp, `${web.origin}/`);
          target = await waitForTarget((t) => (t.url || '').startsWith(web.origin), 3000).catch(() => null);
        }
        assert.ok(target, 'workspace tab navigated');
        const page = new CdpClient(target.webSocketDebuggerUrl);
        await page.open();
        await page.send('Runtime.enable');
        await page.evaluate(`document.cookie = 'x=1; path=/'`);
        page.close();
        await sleep(500);
        assert.ok(existsSync(wsDir), 'workspace data dir should exist after use');

        // Delete it, then let the async marker land.
        const del = await otf(b1.cdp, 'workspaces.delete', { id: wsId });
        assert.equal(del.ok, true);
        await sleep(1500);
        assert.ok(existsSync(path.join(wsDir, '.otf-deleted')), 'dir should be marked for deletion');
      } finally {
        await b1.close();
      }
      // Still present after close (wipe is deferred to next launch).
      assert.ok(existsSync(wsDir), 'data survives until restart');

      // Launch 2 (same profile): startup wipes the marked directory.
      const b2 = await launchDevBrowser({ userDataDir, preserveProfile: true });
      try {
        await waitFor(b2.cdp, 'typeof window.otf', (t) => t === 'object');
      } finally {
        await b2.close();
      }
      assert.ok(!existsSync(wsDir), 'deleted workspace data is wiped on next launch');
    } finally {
      await web.close();
      await rm(root, { recursive: true, force: true });
    }
  });
