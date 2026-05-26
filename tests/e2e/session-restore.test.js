import test from 'node:test';
import assert from 'node:assert/strict';
import { Database } from 'bun:sqlite';
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

const addressSelector = 'input[placeholder="Search or enter address..."]';
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

function readPersistedWorkspaceTabUrls(profileRoot) {
  const dbPath = path.join(
    profileRoot,
    'home',
    '.local',
    'share',
    'otf-browser-dev',
    'browser.db',
  );
  const db = new Database(dbPath, { readonly: true });
  try {
    return db.query('SELECT url FROM workspace_tabs ORDER BY position ASC')
      .all()
      .map((row) => row.url);
  } finally {
    db.close();
  }
}

test('continue startup restores previous tabs and active tab after restart',
  { timeout: timeoutMs + 45000, concurrency: false },
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

      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
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

test('continue startup skips internal browser pages during workspace restore',
  { timeout: timeoutMs + 45000, concurrency: false },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-session-filter-'));
    const unique = Date.now();
    const title = `Restore External ${unique}`;
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html><title>${title}</title><main>${title}</main>`);
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
      await navigateFromAddressBar(browser.cdp, `${server.origin}/external`);
      await waitForTabs(browser.cdp, [title]);

      await openNewTab(browser.cdp);
      await navigateFromAddressBar(browser.cdp, 'browser://settings');
      await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('browser://settings'),
        15000,
      );

      await browser.close();
      browser = null;
      await sleep(1000);

      assert.deepEqual(
        readPersistedWorkspaceTabUrls(profileRoot),
        [`${server.origin}/external`],
        'workspace persistence should contain only real browsing tabs',
      );

      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      const restoredTabs = await waitForTabs(browser.cdp, [title]);
      assert.equal(
        restoredTabs.filter((tab) => /settings/i.test(tab.text)).length,
        0,
        'internal Settings tab should not be restored',
      );
    } finally {
      if (browser) {
        await browser.close();
      }
      await server.close();
      await rm(profileRoot, { recursive: true, force: true });
    }
  });

test('continue startup opens a fresh new tab when only internal pages were open',
  { timeout: timeoutMs + 30000, concurrency: false },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-internal-only-'));
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
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ''`,
        (value) => value.includes('browser://downloads'),
        15000,
      );

      await browser.close();
      browser = null;
      await sleep(1000);

      assert.deepEqual(
        readPersistedWorkspaceTabUrls(profileRoot),
        [],
        'workspace persistence should not store internal-only browser pages',
      );

      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      const state = await waitFor(
        browser.cdp,
        `(() => ({
          address: document.querySelector(${JSON.stringify(addressSelector)})?.value || '',
          tabs: [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
            .map((tab) => tab.textContent || ''),
        }))()`,
        (value) => !value.address.includes('browser://downloads') && value.tabs.length === 1,
        30000,
      );
      assert.equal(state.tabs.length, 1);
      assert.ok(!state.address.includes('browser://downloads'));
    } finally {
      if (browser) {
        await browser.close();
      }
      await rm(profileRoot, { recursive: true, force: true });
    }
  });
