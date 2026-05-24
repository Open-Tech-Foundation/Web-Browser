import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { connectShell, pressShortcut } from './helpers/e2eUtils.js';

test('zoom overlay can zoom in and reset the current tab',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let zoomCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await clickSelector(shellCdp, 'button[title="Zoom"]');
      zoomCdp = await browser.connectToTarget((target) =>
        /zoombar\.html/i.test(target.url || ''),
        15000,
      );

      await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('100%'), 15000);
      await clickSelector(zoomCdp, 'button[title="Zoom in"]');
      await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('110%'), 15000);
      await clickSelector(zoomCdp, 'button[title="Reset zoom"]');
      await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('100%'), 15000);
    } finally {
      if (zoomCdp) zoomCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('zoom keyboard shortcuts update the visible zoom state',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let zoomCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await clickSelector(shellCdp, 'button[title="Zoom"]');
      zoomCdp = await browser.connectToTarget((target) =>
        /zoombar\.html/i.test(target.url || ''),
        15000,
      );

      await pressShortcut(shellCdp, '+', 'Equal', 187, 2);
      await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('110%'), 15000);
      await pressShortcut(shellCdp, '0', 'Digit0', 48, 2);
      const resetText = await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('100%'), 15000);
      assert.ok(resetText.includes('100%'));
    } finally {
      if (zoomCdp) zoomCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });
