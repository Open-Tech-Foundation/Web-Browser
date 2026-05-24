import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

const addressSelector = 'input[placeholder="Search or enter address..."]';

test('History records visited web pages but not internal Settings page',
  { timeout: timeoutMs + 15000 },
  async () => {
    const uniqueTitle = `OTF History E2E ${Date.now()}`;
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
    const visitedUrl = `${staticServer.origin}/history-e2e`;

    const browser = await launchDevBrowser();
    let settingsCdp = null;
    let historyCdp = null;
    try {
      await waitFor(
        browser.cdp,
        `!!document.querySelector('button[title="Settings"]')`,
        Boolean,
      );
      await clickSelector(browser.cdp, 'button[title="Settings"]');

      settingsCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Settings' ||
        /settings\.html/i.test(target.url || ''),
      );
      await waitFor(
        settingsCdp,
        `document.body.innerText`,
        (text) => /Privacy/i.test(text) && /Search Engine/i.test(text),
      );
      const clickedPrivacy = await settingsCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('aside nav button')]
            .find((item) => (item.textContent || '').includes('Privacy'));
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedPrivacy, true);
      await waitFor(
        settingsCdp,
        `document.body.innerText`,
        (text) => /Web browsing history/i.test(text),
      );

      const enabledHistory = await settingsCdp.evaluate(`
        (() => {
          const heading = [...document.querySelectorAll('h3')]
            .find((item) => (item.textContent || '').includes('Web browsing history'));
          const card = heading?.closest('.group');
          const button = card?.querySelector('button');
          if (!button) return false;
          const knob = button.querySelector('span');
          const isOn = button.className.includes('bg-orange-500') ||
            knob?.className.includes('translate-x-5');
          if (!isOn) button.click();
          return true;
        })()
      `);
      assert.equal(enabledHistory, true);
      await waitFor(
        settingsCdp,
        `(() => {
          const heading = [...document.querySelectorAll('h3')]
            .find((item) => (item.textContent || '').includes('Web browsing history'));
          const card = heading?.closest('.group');
          const button = card?.querySelector('button');
          const knob = button?.querySelector('span');
          return Boolean(button && (
            button.className.includes('bg-orange-500') ||
            knob?.className.includes('translate-x-5')
          ));
        })()`,
        Boolean,
      );

      settingsCdp.close();
      settingsCdp = null;

      await navigateFromAddressBar(browser.cdp, visitedUrl);
      await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('/history-e2e') || value.includes(staticServer.origin),
        15000,
      );

      await navigateFromAddressBar(browser.cdp, 'browser://history');
      historyCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'History' ||
        /history\.html/i.test(target.url || ''),
      );

      const historyText = await waitFor(
        historyCdp,
        `document.body.innerText`,
        (text) => text.includes(uniqueTitle) || text.includes('/history-e2e'),
        15000,
      );

      assert.ok(
        historyText.includes(uniqueTitle) || historyText.includes('/history-e2e'),
        `expected visited page in history, got ${historyText}`,
      );
      assert.equal(
        historyText.includes('browser://settings'),
        false,
        'internal Settings page should not appear in history',
      );
    } finally {
      if (settingsCdp) {
        settingsCdp.close();
      }
      if (historyCdp) {
        historyCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });

test('user can remove individual history items and clear all history',
  { timeout: timeoutMs + 20000 },
  async () => {
    const unique = Date.now();
    const titles = [`History Remove ${unique}`, `History Clear ${unique}`];
    const removePath = `/remove-${unique}`;
    const clearPath = `/clear-${unique}`;
    const staticServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      const title = req.url.includes('clear') ? titles[1] : titles[0];
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>${title}</title></head>
          <body><main><h1>${title}</h1></main></body>
        </html>`);
    });

    const browser = await launchDevBrowser({
      settings: {
        searchEngine: 'google',
        historyEnabled: true,
        downloadsEnabled: true,
        startupBehavior: 'newtab',
        startupUrls: [],
        httpsOnly: false,
        blockInsecure: false,
        appearanceMode: 'auto',
      },
    });
    let historyCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, `${staticServer.origin}${removePath}`);
      await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes(removePath),
        15000,
      );

      await navigateFromAddressBar(browser.cdp, `${staticServer.origin}${clearPath}`);
      await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes(clearPath),
        15000,
      );

      await navigateFromAddressBar(browser.cdp, 'browser://history');
      historyCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'History' ||
        /history\.html/i.test(target.url || ''),
      );

      await waitFor(
        historyCdp,
        `document.body.innerText`,
        (text) => text.includes(removePath) && text.includes(clearPath),
        15000,
      );

      const removedOne = await historyCdp.evaluate(`
        (() => {
          const row = [...document.querySelectorAll('.group')]
            .find((item) => (item.textContent || '').includes(${JSON.stringify(removePath)}));
          const button = row?.querySelector('button[title="Remove from history"]');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(removedOne, true);

      await waitFor(
        historyCdp,
        `document.body.innerText`,
        (text) => !text.includes(removePath) && text.includes(clearPath),
        15000,
      );

      const clearedAll = await historyCdp.evaluate(`
        (() => {
          window.confirm = () => true;
          const button = [...document.querySelectorAll('button')]
            .find((item) => (item.textContent || '').includes('Clear History'));
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clearedAll, true);

      await waitFor(
        historyCdp,
        `document.body.innerText`,
        (text) => text.includes('No History Yet') && !text.includes(clearPath),
        15000,
      );
    } finally {
      if (historyCdp) {
        historyCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });
