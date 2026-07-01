import test from 'node:test';
import assert from 'node:assert/strict';

import {
  CdpClient, launchDevBrowser, navigateFromAddressBar, startStaticServer, sleep,
  timeoutMs, waitFor, waitForTarget,
} from './helpers/browserHarness.js';

// The bridge (window.otf) must be exposed only to otf's own internal UI frames.
// Web content — anything not on the internal browser:// scheme or the trusted dev
// UI origin — must never see it. Here the UI is served from the harness's dev
// origin; a second static server on a different origin stands in for web content.
test('window.otf is exposed to the UI but denied to web content',
  { timeout: timeoutMs + 20000 },
  async () => {
    const browser = await launchDevBrowser();
    let web = null;
    let webPage = null;
    try {
      // The UI frame is a trusted internal frame: it has the bridge.
      const uiBridge = await browser.cdp.evaluate(
        '({ hasOtf: typeof window.otf, hasPost: typeof (window.otf && window.otf.postMessage) })'
      );
      assert.equal(uiBridge.hasOtf, 'object', 'UI frame should have window.otf');
      assert.equal(uiBridge.hasPost, 'function', 'UI frame should have window.otf.postMessage');

      // Stand up a distinct origin that plays the role of an untrusted web page.
      const hits = [];
      web = await startStaticServer((_req, res) => {
        hits.push(1);
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end('<!doctype html><title>web content</title><h1>web content</h1>');
      });

      // Synthetic address-bar navigation can occasionally miss the keystrokes, so
      // retry until the untrusted origin has actually been requested.
      for (let attempt = 0; attempt < 5 && hits.length === 0; attempt++) {
        await navigateFromAddressBar(browser.cdp, `${web.origin}/`);
        for (let i = 0; i < 12 && hits.length === 0; i++) {
          await sleep(250);
        }
      }
      assert.ok(hits.length > 0, 'the untrusted page should have been navigated to');

      const target = await waitForTarget((t) => (t.url || '').startsWith(web.origin));
      webPage = new CdpClient(target.webSocketDebuggerUrl);
      await webPage.open();
      await webPage.send('Runtime.enable');

      // The document has committed on the untrusted origin; the bridge must be absent.
      await waitFor(webPage, 'document.readyState',
        (s) => s === 'interactive' || s === 'complete');
      const hasOtf = await webPage.evaluate('typeof window.otf');
      assert.equal(hasOtf, 'undefined', 'web content must not see window.otf');
    } finally {
      if (webPage) webPage.close();
      if (web) await web.close();
      await browser.close();
    }
  });
