import test from 'node:test';
import assert from 'node:assert/strict';
import { execSync } from 'node:child_process';

import {
  CdpClient, devUrl, launchDevBrowser, navigateFromAddressBar, timeoutMs,
  waitFor, waitForTarget,
} from './helpers/browserHarness.js';

// Height of otf's own chrome reserved above the page (otf_window_aura.cc:kChromeHeight).
const CHROME_HEIGHT = 65;

function hasXdotool() {
  if (!process.env.DISPLAY) return false;
  try {
    execSync('command -v xdotool', { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}

function resizeOtfWindow(width, height) {
  const id = execSync('xdotool search --name "^otf$" | head -1').toString().trim() ||
    execSync('xdotool search --name otf | head -1').toString().trim();
  assert.ok(id, 'otf top-level window not found via xdotool');
  execSync(`xdotool windowsize ${id} ${width} ${height}`);
  return id;
}

// A page tab is a child aura window layered over the content region, so it must
// be reflowed by hand when the top-level window resizes (OnWidgetBoundsChanged).
// This asserts the page's viewport tracks the window's content region on resize.
test('page tab viewport tracks the window on resize',
  { timeout: timeoutMs + 15000, skip: hasXdotool() ? false : 'needs xdotool + DISPLAY' },
  async () => {
    const browser = await launchDevBrowser();
    let page = null;
    try {
      // Synthetic address-bar navigation can occasionally miss keystrokes; retry
      // until the page tab target actually appears.
      const probeUrl = `${devUrl}/resize-probe`;
      let target = null;
      for (let attempt = 0; attempt < 5 && !target; attempt++) {
        await navigateFromAddressBar(browser.cdp, probeUrl);
        target = await waitForTarget((t) => (t.url || '').includes('resize-probe'), 3000)
          .catch(() => null);
      }
      assert.ok(target, 'the page tab should have navigated to the probe URL');
      page = new CdpClient(target.webSocketDebuggerUrl);
      await page.open();
      await page.send('Runtime.enable');

      const size = 'window.innerWidth + "x" + window.innerHeight';
      const before = await page.evaluate(size);

      const newW = 1024;
      const newH = 720;
      resizeOtfWindow(newW, newH);

      // The page's inner width should track the window width after reflow.
      const after = await waitFor(page, size, (v) => {
        const w = Number(String(v).split('x')[0]);
        return Math.abs(w - newW) < 40 && String(v) !== before;
      }, 10000);

      const [w, h] = after.split('x').map(Number);
      assert.ok(Math.abs(w - newW) < 40, `width ${w} should track window ${newW}`);
      assert.ok(Math.abs(h - (newH - CHROME_HEIGHT)) < 70,
        `height ${h} should track content region ${newH - CHROME_HEIGHT}`);
    } finally {
      if (page) page.close();
      await browser.close();
    }
  });
