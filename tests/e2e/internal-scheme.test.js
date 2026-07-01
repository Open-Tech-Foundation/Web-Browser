import test from 'node:test';
import assert from 'node:assert/strict';

import { CdpClient, launchDevBrowser, timeoutMs, waitFor, waitForTarget } from './helpers/browserHarness.js';

// Production path: internal pages are served over the real browser:// scheme
// (OtfInternalURLLoaderFactory) from the built UI assets — no dev server. The
// shell and the new tab page must load, their assets must load over browser://,
// and internal frames get the bridge (the trust gate admits the scheme).
test('browser:// serves the shell and new tab page in production mode',
  { timeout: timeoutMs + 20000 },
  async () => {
    const browser = await launchDevBrowser({ productionUi: true });
    try {
      // The shell itself committed on browser://shell and rendered React with
      // its assets loaded over browser://.
      const shell = await browser.cdp.evaluate(
        `JSON.stringify({ href: location.href, root: !!document.querySelector('#root'), otf: typeof window.otf, scripts: document.scripts.length })`
      );
      const s = JSON.parse(shell);
      assert.match(s.href, /^browser:\/\/shell/);
      assert.equal(s.root, true, 'shell React root mounted');
      assert.equal(s.otf, 'object', 'shell has the bridge over browser://');
      assert.ok(s.scripts >= 1, 'shell loaded a script asset over browser://');

      // The boot tab loads browser://newtab natively (served as newtab.html).
      const target = await waitForTarget((t) => (t.url || '').startsWith('browser://newtab'));
      const newtab = new CdpClient(target.webSocketDebuggerUrl);
      await newtab.open();
      await newtab.send('Runtime.enable');
      const mounted = await waitFor(newtab, `!!document.querySelector('#root')`, Boolean, 8000);
      assert.equal(mounted, true, 'new tab page rendered over browser://');
      newtab.close();
    } finally {
      await browser.close();
    }
  });
