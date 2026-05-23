import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  typeText,
  waitFor,
} from './helpers/browserHarness.js';

test('user can add and persist startup pages in Settings',
  { timeout: timeoutMs + 20000 },
  async () => {
    const uniqueTitle = `OTF Startup URLs ${Date.now()}`;
    const staticServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>${uniqueTitle}</title></head>
          <body><main><h1>${uniqueTitle}</h1></main></body>
        </html>`);
    });
    const startupUrl = `${staticServer.origin}/startup-urls-e2e`;

    const browser = await launchDevBrowser();
    let shellCdp = null;
    let settingsCdp = null;
    let reopenedSettingsCdp = null;
    try {
      const devUrl = browser.devUrl;
      shellCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'OTF Browser Shell' ||
        target.url === devUrl ||
        target.url === `${devUrl}/`,
      );

      await waitFor(
        shellCdp,
        `!!document.querySelector('button[title="Settings"]')`,
        Boolean,
      );
      await clickSelector(shellCdp, 'button[title="Settings"]');

      settingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
      );

      await waitFor(
        settingsCdp,
        `document.body.innerText`,
        (text) => /Search Engine/i.test(text) && /On Startup/i.test(text),
      );

      await clickSelector(settingsCdp, 'aside nav button:nth-of-type(2)');
      await waitFor(
        settingsCdp,
        `document.body.innerText`,
        (text) => /Open a specific page or set of pages/i.test(text),
        15000,
      );

      const clickedSpecific = await settingsCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('main button')]
            .find((item) => (item.textContent || '').includes('Open a specific page or set of pages'));
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedSpecific, true);
      await waitFor(
        settingsCdp,
        `!!document.querySelector('input[placeholder="Enter URL (e.g., https://google.com)"]')`,
        Boolean,
        15000,
      );

      await clickSelector(settingsCdp, 'input[placeholder="Enter URL (e.g., https://google.com)"]');
      await typeText(settingsCdp, startupUrl);
      await waitFor(
        settingsCdp,
        `document.querySelector('input[placeholder="Enter URL (e.g., https://google.com)"]')?.value || ''`,
        (value) => value === startupUrl,
        15000,
      );
      const clickedAdd = await settingsCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('button')]
            .find((item) => (item.textContent || '').trim() === 'Add');
          if (!button || button.disabled) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedAdd, true);

      await waitFor(
        settingsCdp,
        `document.body.innerText`,
        (text) => text.includes(startupUrl),
        15000,
      );

      await clickSelector(shellCdp, 'button[title="Settings"]');
      reopenedSettingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
      );

      await waitFor(
        reopenedSettingsCdp,
        `document.body.innerText`,
        (text) => /Search Engine/i.test(text) && /On Startup/i.test(text),
        15000,
      );

      await clickSelector(reopenedSettingsCdp, 'aside nav button:nth-of-type(2)');
      await waitFor(
        reopenedSettingsCdp,
        `document.body.innerText`,
        (text) => text.includes(startupUrl),
        15000,
      );

      const removed = await reopenedSettingsCdp.evaluate(`
        (() => {
          const row = [...document.querySelectorAll('div')]
            .find((item) => (item.textContent || '').includes(${JSON.stringify(startupUrl)}));
          const button = row?.querySelector('button');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(removed, true);

      await waitFor(
        reopenedSettingsCdp,
        `document.body.innerText`,
        (text) => !text.includes(startupUrl),
        15000,
      );
    } finally {
      if (reopenedSettingsCdp) {
        reopenedSettingsCdp.close();
      }
      if (settingsCdp) {
        settingsCdp.close();
      }
      if (shellCdp) {
        shellCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });
