import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
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

test('private tabs sync origin zoom with private tabs only',
  { timeout: timeoutMs + 25000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><title>Zoom Isolation</title><main>zoom isolation</main>');
    });
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let zoomCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await navigateFromAddressBar(shellCdp, `${server.origin}/normal`);
      await waitFor(
        shellCdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        (value) => value.includes('/normal'),
        15000,
      );

      await clickSelector(shellCdp, 'button[title="Zoom"]');
      zoomCdp = await browser.connectToTarget((target) =>
        /zoombar\.html/i.test(target.url || ''),
        15000,
      );
      await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('100%'), 15000);
      await clickSelector(zoomCdp, 'button[title="Zoom in"]');
      await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('110%'), 15000);

      await pressShortcut(shellCdp, 'N', 'KeyN', 78, 10);
      await navigateFromAddressBar(shellCdp, `${server.origin}/private`);
      await waitFor(
        shellCdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        (value) => value.includes('/private'),
        15000,
      );
      await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('100%'), 15000);

      await clickSelector(zoomCdp, 'button[title="Zoom in"]');
      await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('110%'), 15000);

      await pressShortcut(shellCdp, 'N', 'KeyN', 78, 10);
      await navigateFromAddressBar(shellCdp, `${server.origin}/private-second`);
      await waitFor(
        shellCdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        (value) => value.includes('/private-second'),
        15000,
      );
      await waitFor(zoomCdp, `document.body.innerText`, (text) => text.includes('110%'), 15000);
    } finally {
      if (zoomCdp) zoomCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
      await server.close();
    }
  });
