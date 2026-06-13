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
import { setSitePermissionFromUi } from './helpers/e2eUtils.js';

const PIXEL_PNG = Buffer.from(
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNgYPgPAAEDAQAIicLsAAAAAElFTkSuQmCC',
  'base64',
);

async function openNewTabUrl(cdp, url) {
  await clickSelector(cdp, 'a[title="New tab"]');
  await navigateFromAddressBar(cdp, url);
}

test(
  'images site permission blocks images in new tabs',
  { timeout: timeoutMs + 20000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      if (req.url === '/pixel.png') {
        res.writeHead(200, { 'content-type': 'image/png' });
        res.end(PIXEL_PNG);
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>Image Permission E2E</title></head>
          <body>
            <img id="test-image" src="/pixel.png" />
            <div id="result">waiting</div>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let allowedCdp = null;
    let blockedCdp = null;
    try {
      const tagA = 'img-allow-' + String(Date.now());
      const tagB = 'img-block-' + String(Date.now());

      // 1. Open a new tab through the visible UI — images allowed by default.
      await openNewTabUrl(browser.cdp, `${server.origin}/?tag=${tagA}`);
      allowedCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes(tagA),
        15000,
      );
      await waitFor(
        allowedCdp,
        '!!document.querySelector("#test-image")',
        Boolean,
        15000,
      );
      const allowedLoads = await allowedCdp.evaluate(`
        (() => {
          const img = document.getElementById('test-image');
          return img ? (img.naturalWidth > 0) : false;
        })()
      `);
      assert.equal(allowedLoads, true, 'image should load by default');

      // 2. Set images to "block" for the origin.
      await setSitePermissionFromUi(browser, server.origin, 'images', 'block');

      // 3. Open another new tab through the visible UI. Images should be blocked.
      await openNewTabUrl(browser.cdp, `${server.origin}/?tag=${tagB}`);
      blockedCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes(tagB),
        15000,
      );
      await waitFor(
        blockedCdp,
        '!!document.querySelector("#test-image")',
        Boolean,
        15000,
      );

      await new Promise((r) => setTimeout(r, 1000));

      const blockedLoads = await blockedCdp.evaluate(`
        (() => {
          const img = document.getElementById('test-image');
          return img ? (img.naturalWidth > 0) : false;
        })()
      `);
      assert.equal(blockedLoads, false, 'image should be blocked');
    } finally {
      if (blockedCdp) blockedCdp.close();
      if (allowedCdp) allowedCdp.close();
      await browser.close();
      await server.close();
    }
  },
);

test(
  'images site permission allows images when set to allow',
  { timeout: timeoutMs + 20000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      if (req.url === '/pixel.png') {
        res.writeHead(200, { 'content-type': 'image/png' });
        res.end(PIXEL_PNG);
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>Image Allow E2E</title></head>
          <body>
            <img id="test-image" src="/pixel.png" />
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let newTabCdp = null;
    try {
      await setSitePermissionFromUi(browser, server.origin, 'images', 'allow');

      const tag = 'img-allow-' + String(Date.now());
      await openNewTabUrl(browser.cdp, `${server.origin}/?tag=${tag}`);

      newTabCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes(tag),
        15000,
      );
      await waitFor(
        newTabCdp,
        '!!document.querySelector("#test-image")',
        Boolean,
        15000,
      );

      await new Promise((r) => setTimeout(r, 1000));
      const loads = await newTabCdp.evaluate(`
        (() => {
          const img = document.getElementById('test-image');
          return img ? (img.naturalWidth > 0) : false;
        })()
      `);
      assert.equal(loads, true, 'image should load when permission is allow');
    } finally {
      if (newTabCdp) newTabCdp.close();
      await browser.close();
      await server.close();
    }
  },
);
