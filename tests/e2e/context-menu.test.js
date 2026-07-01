import test from 'node:test';
import assert from 'node:assert/strict';

import {
  CdpClient, launchDevBrowser, navigateFromAddressBar, startStaticServer,
  timeoutMs, waitFor, waitForTarget,
} from './helpers/browserHarness.js';

async function rightClick(cdp, x, y) {
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y, buttons: 0 });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'right', buttons: 2, clickCount: 1 });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'right', buttons: 0, clickCount: 1 });
}

// Right-clicking a page shows otf's own context menu overlay, built from the
// engine's hit-test: link items when over a link, plus otf's own additions.
test('right-click shows otf context menu with link + own items',
  { timeout: timeoutMs + 20000 },
  async () => {
    const browser = await launchDevBrowser();
    let web = null;
    let page = null;
    let menu = null;
    try {
      web = await startStaticServer((_req, res) => {
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end('<!doctype html><body style="margin:0"><a href="https://example.org/target" ' +
          'style="position:absolute;left:0;top:0;width:300px;height:120px;font-size:40px">A link</a></body>');
      });

      // Navigate the active tab to the test page and grab its target.
      let target = null;
      for (let i = 0; i < 5 && !target; i++) {
        await navigateFromAddressBar(browser.cdp, `${web.origin}/`);
        target = await waitForTarget((t) => (t.url || '').startsWith(web.origin), 3000).catch(() => null);
      }
      assert.ok(target, 'test page should load in the tab');
      page = new CdpClient(target.webSocketDebuggerUrl);
      await page.open();
      await page.send('Runtime.enable');
      await waitFor(page, `!!document.querySelector('a')`, Boolean);

      // Right-click over the link (retry: a single synthetic press can be
      // dropped before the renderer is ready).
      let menuTarget = null;
      for (let i = 0; i < 5 && !menuTarget; i++) {
        await rightClick(page, 40, 40);
        menuTarget = await waitForTarget((t) => (t.url || '').includes('contextmenu.html'), 2500)
          .catch(() => null);
      }
      assert.ok(menuTarget, "otf's context menu overlay should open");
      menu = new CdpClient(menuTarget.webSocketDebuggerUrl);
      await menu.open();
      await menu.send('Runtime.enable');
      const text = await waitFor(menu, `document.body && document.body.innerText || ''`,
        (t) => /Open in new tab/i.test(t), 8000);
      assert.match(text, /Open in new tab/i, 'link items present');
      assert.match(text, /Copy link address/i);
      // Policy: no view-source / dev-tools items are ever offered.
      assert.doesNotMatch(text, /view.?source/i, 'view-source must not appear');
    } finally {
      if (menu) menu.close();
      if (page) page.close();
      if (web) await web.close();
      await browser.close();
    }
  });
