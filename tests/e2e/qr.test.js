import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  sleep,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

const addressSelector = 'input[placeholder="Search or enter address..."]';

test('user can open QR share popup for the current page',
  { timeout: timeoutMs + 15000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>QR Share E2E</title></head>
          <body><main><h1>QR Share E2E</h1></main></body>
        </html>`);
    });

    const pageUrl = `${server.origin}/qr-share?keep=1&utm_source=e2e&utm_campaign=test#section`;
    const expectedUrl = `${server.origin}/qr-share?keep=1`;
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let qrCdp = null;
    try {
      shellCdp = await browser.connectToTarget((target) =>
        target.url === browser.devUrl || target.url === `${browser.devUrl}/`,
        15000,
      );
      await navigateFromAddressBar(shellCdp, pageUrl);
      await waitFor(
        shellCdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('/qr-share'),
        15000,
      );

      await sleep(500);
      await waitFor(shellCdp, `!!document.querySelector('button[title="Share via QR Code"]')`, Boolean);
      await clickSelector(shellCdp, 'button[title="Share via QR Code"]');

      qrCdp = await browser.connectToTarget((target) =>
        /qr\.html/i.test(target.url || '') ||
        /Share via QR Code/i.test(target.title || ''),
        15000,
      );

      const firstRestore = await waitFor(
        qrCdp,
        `document.querySelector('input[placeholder="Enter URL..."]')?.value || ''`,
        (value) => value === expectedUrl || value === '',
        5000,
      );
      if (firstRestore === '') {
        await clickSelector(shellCdp, 'button[title="Share via QR Code"]');
      }

      const popupState = await waitFor(
        qrCdp,
        `(() => {
          const input = document.querySelector('input[placeholder="Enter URL..."]');
          const canvas = document.querySelector('canvas');
          return {
            text: document.body.innerText || '',
            inputValue: input?.value || '',
            canvasWidth: canvas?.width || 0,
            canvasHeight: canvas?.height || 0,
            hasPngData: (canvas?.toDataURL?.('image/png') || '').length > 100,
          };
        })()`,
        (value) =>
          value.text.includes('Share via QR Code') &&
          value.inputValue === expectedUrl &&
          value.canvasWidth > 0 &&
          value.canvasHeight > 0 &&
          value.hasPngData,
        15000,
      );

      assert.equal(popupState.inputValue, expectedUrl);
    } finally {
      if (qrCdp) {
        qrCdp.close();
      }
      if (shellCdp) {
        shellCdp.close();
      }
      await browser.close();
      await server.close();
    }
  });

test('QR share strips non-utm tracking params like fbclid and gclid',
  { timeout: timeoutMs + 15000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>QR Track Params</title></head>
          <body><main><h1>QR Track Params</h1></main></body>
        </html>`);
    });

    const pageUrl = `${server.origin}/share?q=1&fbclid=abc123&gclid=xyz789&_ga=GA1.2&_gl=GL1&utm_source=test&utm_medium=cpc&page=2#top`;
    const expectedUrl = `${server.origin}/share?q=1&page=2`;
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let qrCdp = null;
    try {
      shellCdp = await browser.connectToTarget((target) =>
        target.url === browser.devUrl || target.url === `${browser.devUrl}/`,
        15000,
      );
      await navigateFromAddressBar(shellCdp, pageUrl);
      await waitFor(
        shellCdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('/share'),
        15000,
      );

      await sleep(500);
      await waitFor(shellCdp, `!!document.querySelector('button[title="Share via QR Code"]')`, Boolean);
      await clickSelector(shellCdp, 'button[title="Share via QR Code"]');

      qrCdp = await browser.connectToTarget((target) =>
        /qr\.html/i.test(target.url || '') ||
        /Share via QR Code/i.test(target.title || ''),
        15000,
      );

      const popupState = await waitFor(
        qrCdp,
        `(() => {
          const input = document.querySelector('input[placeholder="Enter URL..."]');
          return input?.value || '';
        })()`,
        (value) => value === expectedUrl || value === '',
        15000,
      );

      assert.equal(popupState, expectedUrl,
        `QR should strip fbclid, gclid, _ga, _gl, utm_* params and fragment`);
    } finally {
      if (qrCdp) qrCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
      await server.close();
    }
  });
