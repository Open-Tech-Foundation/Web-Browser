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

function updateWorkspaceTab(profileRoot, url, updates) {
  const dbPath = path.join(
    profileRoot,
    'home',
    '.local',
    'share',
    'otf-browser-dev',
    'browser.db',
  );
  const db = new Database(dbPath);
  try {
    const assignments = Object.keys(updates).map((key) => `${key} = ?`).join(', ');
    db.query(`UPDATE workspace_tabs SET ${assignments} WHERE url = ?`)
      .run(...Object.values(updates), url);
  } finally {
    db.close();
  }
}

function insertWorkspaceTab(profileRoot, tab) {
  const dbPath = path.join(
    profileRoot,
    'home',
    '.local',
    'share',
    'otf-browser-dev',
    'browser.db',
  );
  const db = new Database(dbPath);
  try {
    db.query(`
      INSERT INTO workspace_tabs(
        workspace_id, position, url, title, was_active, is_image_preview, preview_page, pinned
      ) VALUES (?, ?, ?, ?, ?, 0, 0, ?)
    `).run(
      tab.workspaceId ?? 1,
      tab.position ?? 0,
      tab.url,
      tab.title ?? '',
      tab.wasActive ? 1 : 0,
      tab.pinned ? 1 : 0,
    );
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

test('continue startup restores pinned state on the active first tab',
  { timeout: timeoutMs + 45000, concurrency: false },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-pinned-restore-'));
    const unique = Date.now();
    const titles = [`Pinned Alpha ${unique}`, `Pinned Beta ${unique}`];
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      const title = req.url === '/beta' ? titles[1] : titles[0];
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
      const alphaUrl = `${server.origin}/alpha`;
      const betaUrl = `${server.origin}/beta`;
      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      await navigateFromAddressBar(browser.cdp, alphaUrl);
      await waitForTabs(browser.cdp, [titles[0]]);

      await openNewTab(browser.cdp);
      await navigateFromAddressBar(browser.cdp, betaUrl);
      await waitForTabs(browser.cdp, titles);

      await browser.close();
      browser = null;
      await sleep(1000);

      updateWorkspaceTab(profileRoot, alphaUrl, { pinned: 1, was_active: 1 });
      updateWorkspaceTab(profileRoot, betaUrl, { pinned: 0, was_active: 0 });

      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      const pinnedState = await waitFor(
        browser.cdp,
        `(() => {
          const tabs = [...document.querySelectorAll('a[href^="tab-context-menu:"]')];
          return {
            count: tabs.length,
            firstHasClose: Boolean(tabs[0]?.querySelector('button[title="Close tab"]')),
            secondText: tabs[1]?.textContent || '',
            firstActive: tabs[0]?.className.includes('bg-bar-light') ||
              tabs[0]?.className.includes('dark:bg-bar-dark') || false,
          };
        })()`,
        (state) => state.count === 2 && !state.firstHasClose &&
          state.secondText.includes(titles[1]) && state.firstActive,
        30000,
      );
      assert.equal(pinnedState.firstHasClose, false);
      assert.equal(pinnedState.firstActive, true);
    } finally {
      if (browser) {
        await browser.close();
      }
      await server.close();
      await rm(profileRoot, { recursive: true, force: true });
    }
  });

test('continue startup ignores stale dev-ui internal restore rows',
  { timeout: timeoutMs + 30000, concurrency: false },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-stale-devui-'));
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
      const staleSettingsUrl = `${browser.devUrl}/settings.html`;
      await browser.close();
      browser = null;
      await sleep(1000);

      insertWorkspaceTab(profileRoot, {
        url: staleSettingsUrl,
        title: 'Settings',
        wasActive: true,
      });

      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      const state = await waitFor(
        browser.cdp,
        `(() => ({
          address: document.querySelector(${JSON.stringify(addressSelector)})?.value || '',
          tabs: [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
            .map((tab) => tab.textContent || ''),
        }))()`,
        (value) => value.tabs.length === 1 && !value.address.includes('/settings.html'),
        30000,
      );
      assert.equal(state.tabs.length, 1);
      assert.ok(!state.address.includes('/settings.html'));
    } finally {
      if (browser) {
        await browser.close();
      }
      await rm(profileRoot, { recursive: true, force: true });
    }
  });

test('browser-created internal tabs are not persisted as dev-ui urls',
  { timeout: timeoutMs + 30000, concurrency: false },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-created-internal-'));
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
      await browser.cdp.evaluate(`
        new Promise((resolve, reject) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'session-restore-settings-tab',
              method: 'navigation.newTab',
              params: { url: 'browser://settings' },
            }),
            onSuccess: (json) => {
              try {
                const envelope = JSON.parse(json);
                if (!envelope?.ok) {
                  reject(new Error(envelope?.error?.message || 'navigation.newTab failed'));
                  return;
                }
                resolve(envelope.result);
              } catch (err) {
                reject(err);
              }
            },
            onFailure: (_code, message) => reject(new Error(message || 'cefQuery failed')),
          });
        })
      `);
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
        [],
        'browser-created internal pages must not be persisted as dev-ui URLs',
      );
    } finally {
      if (browser) {
        await browser.close();
      }
      await rm(profileRoot, { recursive: true, force: true });
    }
  });
