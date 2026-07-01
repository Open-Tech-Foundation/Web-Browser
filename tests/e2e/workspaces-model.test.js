import test from 'node:test';
import assert from 'node:assert/strict';

import { launchDevBrowser, timeoutMs, waitFor } from './helpers/browserHarness.js';

// Drive a bridge RPC over the real window.otf transport and resolve its reply.
// (The workspace popup UI isn't wired yet, so this exercises the backend model
// end-to-end through the actual bridge rather than through synthetic clicks.)
function otfCall(cdp, method, params = {}) {
  const payload = JSON.stringify({ method, params });
  return cdp.evaluate(`
    new Promise((resolve, reject) => {
      const { method, params } = ${JSON.stringify(JSON.parse(payload))};
      const id = 'e2e-' + Math.random().toString(36).slice(2);
      const prev = window.__otfReceive;
      const timer = setTimeout(() => { window.__otfReceive = prev; reject(new Error('rpc timeout')); }, 4000);
      window.__otfReceive = (m) => {
        try {
          const o = JSON.parse(m);
          if (o && o.id === id) {
            clearTimeout(timer);
            window.__otfReceive = prev;
            resolve(o);
            return;
          }
        } catch (e) { /* not ours */ }
        if (prev) prev(m);
      };
      window.otf.postMessage(JSON.stringify({ id, method, params }));
    })
  `);
}

test('workspace model: create scopes tabs, switch restores, duplicate rejected',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    const cdp = browser.cdp;
    try {
      await waitFor(cdp, 'typeof window.otf', (t) => t === 'object');

      // Starts with a single default workspace.
      let list = await otfCall(cdp, 'workspaces.list');
      assert.equal(list.result.length, 1);
      assert.equal(list.result[0].active, true);

      // Create a workspace: it becomes active and gets its own fresh tab.
      const created = await otfCall(cdp, 'workspaces.create', { name: 'QA' });
      assert.equal(created.ok, true);
      const qaId = created.result.id;

      list = await otfCall(cdp, 'workspaces.list');
      assert.equal(list.result.length, 2);
      assert.equal(list.result.find((w) => w.id === qaId).active, true);

      // tabs.list is scoped to the active (QA) workspace.
      const qaTabs = await otfCall(cdp, 'tabs.list');
      assert.equal(qaTabs.result.length, 1, 'QA workspace has its own single tab');

      // Duplicate name is rejected.
      const dup = await otfCall(cdp, 'workspaces.create', { name: 'QA' });
      assert.equal(dup.ok, false);
      assert.equal(dup.error.code, 'duplicate name');

      // Switching back to Default restores its (separate) tab set.
      const switched = await otfCall(cdp, 'workspaces.switch', { id: 1 });
      assert.equal(switched.ok, true);
      const defaultTabs = await otfCall(cdp, 'tabs.list');
      assert.equal(defaultTabs.result.length, 1);
      // The QA tab is not visible from Default.
      assert.ok(!defaultTabs.result.some((t) => qaTabs.result.some((q) => q.id === t.id)),
        'Default and QA tab sets are disjoint');
    } finally {
      await browser.close();
    }
  });
