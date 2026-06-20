import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { setSitePermissionFromUi } from './helpers/e2eUtils.js';

test('automatic picture-in-picture requests block by default and can be allowed by site setting',
  { timeout: timeoutMs + 20000 },
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
          <head><title>Auto PiP Permission E2E</title></head>
          <body>
            <main id="state">booting</main>
            <canvas id="canvas" width="320" height="180"></canvas>
            <video id="video" muted playsinline autoplay></video>
            <script>
              const state = document.getElementById('state');
              const canvas = document.getElementById('canvas');
              const ctx = canvas.getContext('2d');
              let tick = 0;
              setInterval(() => {
                ctx.fillStyle = tick % 2 === 0 ? '#0f172a' : '#0891b2';
                ctx.fillRect(0, 0, canvas.width, canvas.height);
                ctx.fillStyle = '#ffffff';
                ctx.font = '24px sans-serif';
                ctx.fillText('auto pip ' + tick, 20, 60);
                tick += 1;
              }, 100);

              const video = document.getElementById('video');
              video.srcObject = canvas.captureStream(20);
              video.play().then(() => {
                state.textContent = 'playing';
                setTimeout(async () => {
                  state.textContent = 'requesting';
                  try {
                    await video.requestPictureInPicture();
                    state.textContent = 'allowed';
                  } catch (error) {
                    state.textContent = 'blocked:' + (error && error.name ? error.name : 'Error');
                  }
                }, 300);
              }).catch((error) => {
                state.textContent = 'play-failed:' + (error && error.name ? error.name : 'Error');
              });
            </script>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, server.origin);
      await waitFor(
        browser.cdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        (value) => value.includes(server.origin.replace(/^https?:\/\//, '')),
        15000,
      );
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(server.origin) ||
        /Auto PiP Permission E2E/i.test(target.title || ''),
      );

      await waitFor(pageCdp, `document.getElementById('state')?.textContent || ''`,
        (text) => text === 'requesting' || text.startsWith('blocked:'), 15000);
      await waitFor(
        pageCdp,
        `document.getElementById('state')?.textContent || ''`,
        (text) => text.includes('blocked:SecurityError'),
        15000,
      );

      await setSitePermissionFromUi(
        browser, server.origin, 'autoPictureInPicture', 'allow');
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });
