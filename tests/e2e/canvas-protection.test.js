import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

// The test page is served from an inline HTTP server.  It is a two-phase test:
//
//   Phase 1 (no prior localStorage data):
//     Reads canvas hash twice, saves to localStorage, emits phase-1 JSON.
//
//   Phase 2 (localStorage has phase-1 data):
//     Reads canvas hash again, compares, emits phase-2 JSON.
//
// Two separate browser processes share the same profile directory so
// localStorage persists across the simulated restart.

const TEST_PAGE_HTML = `<!doctype html>
<html><head><meta charset="utf-8"><title>Canvas FP Test</title></head>
<body>
<pre id="result">pending</pre>
<script>
(function () {
  const SESSION_KEY = 'otf_canvas_fp_test';
  function simpleHash(data) {
    let h = 0x811c9dc5 >>> 0;
    for (let i = 0; i < data.length; i++) {
      h = Math.imul(h ^ data[i], 0x01000193) >>> 0;
    }
    return h.toString(16);
  }
  function readCanvasHash() {
    const c = document.createElement('canvas');
    c.width = 200; c.height = 100;
    const ctx = c.getContext('2d');
    ctx.fillStyle = '#ff6600'; ctx.fillRect(0, 0, 200, 100);
    ctx.fillStyle = '#0066ff'; ctx.fillRect(50, 20, 100, 60);
    ctx.fillStyle = '#00cc44'; ctx.fillRect(80, 40, 40, 20);
    const h1 = simpleHash(ctx.getImageData(0, 0, 200, 100).data);
    const h2 = simpleHash(ctx.getImageData(0, 0, 200, 100).data);
    return { h1, h2, stable: h1 === h2 };
  }
  const stored = JSON.parse(localStorage.getItem(SESSION_KEY) || '[]');
  const current = readCanvasHash();
  stored.push(current);
  localStorage.setItem(SESSION_KEY, JSON.stringify(stored));
  document.getElementById('result').textContent = JSON.stringify({
    sessionIndex: stored.length - 1,
    current,
    all: stored,
  });
}());
<\/script>
</body></html>`;

test('canvas fingerprint is stable within a session and rotates across browser restarts',
  { timeout: timeoutMs * 3 },
  async () => {
    const server = await startStaticServer((_req, res) => {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.end(TEST_PAGE_HTML);
    });

    const profileRoot = await mkdtemp(
      path.join(os.tmpdir(), 'otf-browser-fp-test-'),
    );

    let session1Result = null;
    let session2Result = null;

    try {
      // ── Session 1 ─────────────────────────────────────────────────────────
      {
        const browser = await launchDevBrowser({ profileRoot });
        let pageCdp = null;
        try {
          await navigateFromAddressBar(browser.cdp, server.origin);
          pageCdp = await browser.connectToTarget(
            (t) => (t.url || '').startsWith(server.origin),
          );
          session1Result = JSON.parse(await waitFor(
            pageCdp,
            `document.getElementById('result')?.textContent || 'pending'`,
            (t) => t !== 'pending',
            15000,
          ));
        } finally {
          if (pageCdp) pageCdp.close();
          await browser.close();
        }
      }

      // ── Session 2 ─────────────────────────────────────────────────────────
      {
        const browser = await launchDevBrowser({ profileRoot });
        let pageCdp = null;
        try {
          await navigateFromAddressBar(browser.cdp, server.origin);
          pageCdp = await browser.connectToTarget(
            (t) => (t.url || '').startsWith(server.origin),
          );
          session2Result = JSON.parse(await waitFor(
            pageCdp,
            `document.getElementById('result')?.textContent || 'pending'`,
            (t) => {
              if (t === 'pending') return false;
              try { return JSON.parse(t).all.length === 2; }
              catch (_) { return false; }
            },
            15000,
          ));
        } finally {
          if (pageCdp) pageCdp.close();
          await browser.close();
        }
      }

      // ── Assertions ────────────────────────────────────────────────────────

      assert.equal(session1Result.current.stable, true,
        `Session 1: two getImageData reads must agree ` +
        `(h1=${session1Result.current.h1}, h2=${session1Result.current.h2})`);

      assert.equal(session2Result.current.stable, true,
        `Session 2: two getImageData reads must agree ` +
        `(h1=${session2Result.current.h1}, h2=${session2Result.current.h2})`);

      assert.equal(session2Result.all.length, 2,
        'localStorage must carry both sessions into session 2');

      const s1 = session2Result.all[0].h1;
      const s2 = session2Result.all[1].h1;
      assert.notEqual(s1, s2,
        `Canvas fingerprint must differ across browser restarts ` +
        `(session1=${s1}, session2=${s2})`);
    } finally {
      await server.close();
      await rm(profileRoot, { recursive: true, force: true });
    }
  },
);
