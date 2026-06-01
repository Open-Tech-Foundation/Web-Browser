import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { connectShell, pressShortcut } from './helpers/e2eUtils.js';

const isFullscreenExpr = `document.fullscreenElement !== null`;

async function clickElementBySelector(cdp, selector) {
  // Get the element's bounding box via the page's own JS
  const box = await cdp.evaluate(`
    (() => {
      const el = document.querySelector(${JSON.stringify(selector)});
      if (!el) return null;
      const r = el.getBoundingClientRect();
      return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
    })()
  `);
  if (!box) throw new Error(`element not found: ${selector}`);

  // Dispatch a real mouse event so requestFullscreen() sees a user gesture
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mousePressed',
    x: box.x,
    y: box.y,
    button: 'left',
    clickCount: 1,
  });
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mouseReleased',
    x: box.x,
    y: box.y,
    button: 'left',
    clickCount: 1,
  });
}

test('F11 enters browser fullscreen and Escape exits',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);

      await pressShortcut(shellCdp, 'F11', 'F11', 122, 0);
      await new Promise((r) => setTimeout(r, 500));

      await pressShortcut(shellCdp, 'Escape', 'Escape', 27, 0);
      await new Promise((r) => setTimeout(r, 500));

      assert.ok(true);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('content requestFullscreen exits on Escape',
  { timeout: timeoutMs + 25000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
<title>Fullscreen Escape Test</title>
<div id="box" style="width:200px;height:200px;background:red;">box</div>
<button id="go">Fullscreen</button>
<script>
document.getElementById('go').addEventListener('click', () => {
  document.getElementById('box').requestFullscreen();
});
</script>`);
    });
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let pageCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await navigateFromAddressBar(shellCdp, `${server.origin}/`);

      pageCdp = await browser.connectToTarget(
        (target) => target.type === 'page' && /Fullscreen Escape Test/.test(target.title || ''),
        15000,
      );

      await waitFor(pageCdp, `document.getElementById('go') ? 'ready' : 'pending'`, (v) => v === 'ready', 15000);

      // Simulate a real mouse click to trigger user gesture
      await clickElementBySelector(pageCdp, '#go');
      await waitFor(pageCdp, isFullscreenExpr, Boolean, 10000);

      const inFs = await pageCdp.evaluate(isFullscreenExpr);
      assert.equal(inFs, true, 'expected content fullscreen to be active');

      // Press Escape via the toolbar
      await pressShortcut(shellCdp, 'Escape', 'Escape', 27, 0);
      await waitFor(pageCdp, isFullscreenExpr, (v) => v === false, 10000);

      const afterFs = await pageCdp.evaluate(isFullscreenExpr);
      assert.equal(afterFs, false, 'expected content fullscreen to be exited');
    } finally {
      if (pageCdp) pageCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
      await server.close();
    }
  });

test('content requestFullscreen exits on F11',
  { timeout: timeoutMs + 25000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
<title>Fullscreen F11 Test</title>
<div id="box" style="width:200px;height:200px;background:blue;">box</div>
<button id="go">Fullscreen</button>
<script>
document.getElementById('go').addEventListener('click', () => {
  document.getElementById('box').requestFullscreen();
});
</script>`);
    });
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let pageCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await navigateFromAddressBar(shellCdp, `${server.origin}/`);

      pageCdp = await browser.connectToTarget(
        (target) => target.type === 'page' && /Fullscreen F11 Test/.test(target.title || ''),
        15000,
      );

      await waitFor(pageCdp, `document.getElementById('go') ? 'ready' : 'pending'`, (v) => v === 'ready', 15000);

      // Simulate a real mouse click to trigger user gesture
      await clickElementBySelector(pageCdp, '#go');
      await waitFor(pageCdp, isFullscreenExpr, Boolean, 10000);

      const inFs = await pageCdp.evaluate(isFullscreenExpr);
      assert.equal(inFs, true, 'expected content fullscreen to be active');

      // Press F11 via the toolbar
      await pressShortcut(shellCdp, 'F11', 'F11', 122, 0);
      await waitFor(pageCdp, isFullscreenExpr, (v) => v === false, 10000);

      const afterFs = await pageCdp.evaluate(isFullscreenExpr);
      assert.equal(afterFs, false, 'expected content fullscreen to be exited via F11');
    } finally {
      if (pageCdp) pageCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
      await server.close();
    }
  });
