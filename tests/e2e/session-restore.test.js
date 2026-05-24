import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  sleep,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

const tabStateExpression = `(() => [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
  .map((tab) => ({
    text: tab.textContent || '',
    active: tab.className.includes('bg-bar-light') || tab.className.includes('dark:bg-bar-dark'),
  })))()`;

async function waitForTabs(shell, expectedTitles) {
  return waitFor(
    shell,
    tabStateExpression,
    (tabs) => expectedTitles.every((title) =>
      tabs.some((tab) => tab.text.includes(title))),
    30000,
  );
}

async function openNewTab(shell) {
  const clicked = await shell.evaluate(`
    (() => {
      const button = document.querySelector('button[aria-label="New tab"], button[title="New tab"]');
      if (!button) return false;
      button.click();
      return true;
    })()
  `);
  assert.equal(clicked, true);
}

test('continue startup restores previous tabs and active tab after restart',
  { timeout: timeoutMs + 45000 },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-session-restore-'));
    const unique = Date.now();
    const titles = [`Restore Alpha ${unique}`, `Restore Beta ${unique}`];
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      const title = req.url === '/beta' ? titles[1] : titles[0];
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head><title>${title}</title></head>
          <body><main><h1>${title}</h1></main></body>
        </html>`);
    });

    const settings = {
      searchEngine: 'google',
      historyEnabled: true,
      downloadsEnabled: true,
      startupBehavior: 'continue',
      startupUrls: [],
      httpsOnly: false,
      blockInsecure: false,
      appearanceMode: 'auto',
    };

    let browser = null;
    try {
      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      await navigateFromAddressBar(browser.cdp, `${server.origin}/alpha`);
      await waitFor(browser.cdp, tabStateExpression, (tabs) =>
        tabs.some((tab) => tab.text.includes(titles[0])), 30000);

      await openNewTab(browser.cdp);
      await navigateFromAddressBar(browser.cdp, `${server.origin}/beta`);
      const firstRunTabs = await waitForTabs(browser.cdp, titles);
      assert.ok(
        firstRunTabs.find((tab) => tab.text.includes(titles[1]))?.active,
        'second tab should be active before restart',
      );

      await browser.close();
      browser = null;
      await sleep(1000);

      browser = await launchDevBrowser({ profileRoot, preserveProfile: true });
      const restoredTabs = await waitFor(
        browser.cdp,
        tabStateExpression,
        (tabs) =>
          titles.every((title) => tabs.some((tab) => tab.text.includes(title))) &&
          Boolean(tabs.find((tab) => tab.text.includes(titles[1]))?.active),
        30000,
      );
      assert.ok(
        restoredTabs.find((tab) => tab.text.includes(titles[1]))?.active,
        'active tab should restore to the tab that was active before restart',
      );
      assert.equal(
        restoredTabs.filter((tab) =>
          titles.some((title) => tab.text.includes(title))).length,
        2,
      );
    } finally {
      if (browser) {
        await browser.close();
      }
      await server.close();
      await rm(profileRoot, { recursive: true, force: true });
    }
  });
