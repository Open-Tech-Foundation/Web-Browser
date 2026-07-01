import test from 'node:test';
import assert from 'node:assert/strict';

import {
  CdpClient, launchDevBrowser, timeoutMs, waitFor, waitForTarget,
} from './helpers/browserHarness.js';

// Fire a bridge RPC over the real window.otf transport and resolve its reply.
function otfCall(cdp, method, params = {}) {
  const call = JSON.stringify({ method, params });
  return cdp.evaluate(`
    new Promise((resolve, reject) => {
      const { method, params } = ${JSON.stringify(JSON.parse(call))};
      const id = 'e2e-' + Math.random().toString(36).slice(2);
      const prev = window.__otfReceive;
      const timer = setTimeout(() => { window.__otfReceive = prev; reject(new Error('rpc timeout')); }, 4000);
      window.__otfReceive = (m) => {
        try { const o = JSON.parse(m); if (o && o.id === id) { clearTimeout(timer); window.__otfReceive = prev; resolve(o); return; } } catch (e) {}
        if (prev) prev(m);
      };
      window.otf.postMessage(JSON.stringify({ id, method, params }));
    })
  `);
}

// The popup overlay subsystem: ui.popup.show opens a transparent overlay
// WebContents ("<name>.html") layered over the window; it is a trusted internal
// frame (so it has the bridge and renders the real UI), and ui.popup.hide/toggle
// track its open state.
test('ui.popup opens a transparent overlay that has the bridge',
  { timeout: timeoutMs + 20000 },
  async () => {
    const browser = await launchDevBrowser();
    const shell = browser.cdp;
    let overlay = null;
    try {
      await waitFor(shell, 'typeof window.otf', (t) => t === 'object');

      const shown = await otfCall(shell, 'ui.popup.show', { name: 'workspace' });
      assert.equal(shown.ok, true);

      // The overlay WebContents loads workspace.html.
      const target = await waitForTarget((t) => (t.url || '').includes('workspace.html'));
      overlay = new CdpClient(target.webSocketDebuggerUrl);
      await overlay.open();
      await overlay.send('Runtime.enable');

      // It is a trusted internal frame (same origin as the UI): it has the bridge
      // and renders the real workspace switcher.
      await waitFor(overlay, 'typeof window.otf', (t) => t === 'object');
      const text = await waitFor(overlay, 'document.body && document.body.innerText || ""',
        (t) => /workspace/i.test(t), 8000);
      assert.match(text, /workspace/i);

      // Its page background is transparent so it floats over the content behind it.
      const bg = await overlay.evaluate(
        'getComputedStyle(document.documentElement).backgroundColor'
      );
      assert.match(bg, /rgba\(0, 0, 0, 0\)|transparent/);

      // Hiding it succeeds and clears open state (toggle then re-opens).
      const hidden = await otfCall(shell, 'ui.popup.hide', { name: 'workspace' });
      assert.equal(hidden.ok, true);
      const toggled = await otfCall(shell, 'ui.popup.toggle', { name: 'workspace' });
      assert.equal(toggled.ok, true);
    } finally {
      if (overlay) overlay.close();
      await browser.close();
    }
  });
