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
import { connectShell } from './helpers/e2eUtils.js';

const nativeRpc = (method, params = {}, id = `lazy-rpc-${Date.now()}`) => `
  new Promise((resolve, reject) => {
    window.cefQuery({
      request: JSON.stringify({
        id: ${JSON.stringify(id)},
        method: ${JSON.stringify(method)},
        params: ${JSON.stringify(params)},
      }),
      onSuccess: (json) => {
        try {
          const envelope = JSON.parse(json);
          if (envelope && envelope.ok) {
            resolve(envelope.result);
          } else {
            reject(new Error(envelope?.error?.message || 'RPC failed'));
          }
        } catch (err) {
          resolve(json);
        }
      },
      onFailure: (code, message) => reject(new Error(message || 'cefQuery failed')),
    });
  })
`;

const tabStateExpression = `(() => [...document.querySelectorAll('a[href^="tab-context-menu:"]:not([href$="newtab"])')]
  .map((tab) => ({
    text: tab.textContent || '',
    active: tab.className.includes('bg-bar-light') || tab.className.includes('dark:bg-bar-dark'),
  })))()`;

async function openNewTab(shell) {
  const clicked = await shell.evaluate(`
    (() => {
      const button = document.querySelector('button[aria-label="New tab"], button[title="New tab"], a[aria-label="New tab"], a[title="New tab"]');
      if (!button) return false;
      button.click();
      return true;
    })()
  `);
  assert.equal(clicked, true);
}

function updateWorkspaceTabs(profileRoot, url1, url2) {
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
    db.query('UPDATE workspace_tabs SET was_active = 1 WHERE url = ?').run(url1);
    db.query('UPDATE workspace_tabs SET was_active = 0 WHERE url = ?').run(url2);
  } finally {
    db.close();
  }
}

test('lazy tabs are only created and loaded on demand when switched to',
  { timeout: timeoutMs + 45000, concurrency: false },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-lazy-tabs-'));
    const unique = Date.now();
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      const title = req.url.includes('second') ? 'Lazy Second' : 'Lazy First';
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html><html><head><title>${title}</title></head><body><h1>${title}</h1></body></html>`);
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

    const url1 = `${server.origin}/first`;
    const url2 = `${server.origin}/second`;

    let browser = null;
    let shellCdp = null;

    try {
      // 1. First launch to initialize DB and create tabs
      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      
      // Navigate first tab
      await navigateFromAddressBar(browser.cdp, url1);
      await waitFor(browser.cdp, tabStateExpression, (tabs) => tabs.some((t) => t.text.toLowerCase().includes('first')), 3000);
      
      // Open second tab
      await openNewTab(browser.cdp);
      await navigateFromAddressBar(browser.cdp, url2);
      await waitFor(browser.cdp, tabStateExpression, (tabs) => tabs.length >= 2 && tabs.some((t) => t.text.toLowerCase().includes('second')), 3000);
      
      // Close browser to persist them to the database
      await browser.close();
      browser = null;
      await sleep(1000);

      // 2. Modify database states: mark first tab active, second tab inactive/background
      updateWorkspaceTabs(profileRoot, url1, url2);

      // 3. Second launch (lazy loading restoration check!)
      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 3000);

      // Verify that both tabs show up in the UI tab strip
      const tabs = await waitFor(browser.cdp, tabStateExpression, (tabs) => tabs.length >= 2, 3000);
      assert.ok(tabs.some((t) => t.text.toLowerCase().includes('first')), 'First tab should be in the tab strip');
      assert.ok(tabs.some((t) => t.text.toLowerCase().includes('second')), 'Second tab should be in the tab strip');

      // Verify that the active tab (First tab) is connected and loaded successfully
      const tab1Cdp = await browser.connectToTarget((t) => (t.url || '').includes('first'), 3000);
      assert.ok(tab1Cdp, 'Active tab target should exist and be connectable');
      tab1Cdp.close();

      // Verify that the background tab (Second tab) is NOT realized or loaded yet (its CDP target should NOT exist)
      await assert.rejects(
        browser.connectToTarget((t) => (t.url || '').includes('second'), 1500),
        /timed out waiting for target/i,
        'Background tab should be lazy and not have a CDP target before switching'
      );

      // Get tab list from RPC and find the ID of the lazy background tab
      const tabsList = await shellCdp.evaluate(nativeRpc('tabs.list'));
      const tab2Id = tabsList.find((t) => (t.url || '').includes('second')).id;

      // Switch to the background tab
      await shellCdp.evaluate(nativeRpc('tabs.switch', { tabId: tab2Id }));

      // Verify that the background tab has now been realized and loaded (CDP target exists and is connectable!)
      const tab2Cdp = await browser.connectToTarget((t) => (t.url || '').includes('second'), 3000);
      assert.ok(tab2Cdp, 'Background tab target should exist and be connectable after switching to it');
      tab2Cdp.close();

    } finally {
      if (shellCdp) shellCdp.close();
      if (browser) await browser.close();
      await server.close();
      await rm(profileRoot, { recursive: true, force: true });
    }
  }
);

test('realized tabs are discarded when exceeding the threshold and re-realized on switch',
  { timeout: timeoutMs + 90000, concurrency: false },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-discard-'));
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      const num = req.url.replace('/', '');
      const title = `Page ${num}`;
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html><html><head><title>${title}</title></head><body><h1>${title}</h1></body></html>`);
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
    let shellCdp = null;

    try {
      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 3000);

      // Open 9 tabs in total to exceed the max limit of 8 realized tabs
      const urls = [];
      for (let i = 0; i < 9; i++) {
        console.log(`[E2E] Iteration ${i} starting`);
        const url = `${server.origin}/${i}`;
        urls.push(url);
        if (i === 0) {
          // Navigate the initial tab
          await navigateFromAddressBar(shellCdp, url);
        } else {
          // Open new tab and navigate
          await openNewTab(shellCdp);
          await navigateFromAddressBar(shellCdp, url);
        }
        console.log(`[E2E] Iteration ${i} navigated, waiting for UI update`);
        await waitFor(shellCdp, tabStateExpression, (tabs) => tabs.length === i + 1, 3000);
        console.log(`[E2E] Iteration ${i} UI updated, sleeping`);
        await sleep(500); // Give it a moment to realize/record access
      }

      console.log(`[E2E] Loop finished, checking 9 tabs in UI`);
      // Check that all 9 tabs are shown in the UI tab strip
      const tabs = await waitFor(shellCdp, tabStateExpression, (tabs) => tabs.length === 9, 3000);
      assert.equal(tabs.length, 9, 'Should have 9 tabs in total');

      console.log(`[E2E] Checking active tab 8 is realized`);
      // The last created tab (urls[8], page 8) is active, so it must be realized
      const activeTabCdp = await browser.connectToTarget((t) => (t.url || '').includes('/8'), 3000);
      assert.ok(activeTabCdp, 'Active tab 8 should be connectable');
      activeTabCdp.close();

      console.log(`[E2E] Checking oldest tab 0 is discarded`);
      // The first tab (urls[0], page 0) is the oldest background tab, and since the limit is 8, it should have been discarded!
      await assert.rejects(
        browser.connectToTarget((t) => (t.url || '').includes('/0'), 1500),
        /timed out waiting for target/i,
        'Oldest background tab 0 should be discarded and not have a CDP target'
      );

      console.log(`[E2E] Fetching tabs list via RPC`);
      // Get tab list from RPC and find the ID of tab 0
      const tabsList = await shellCdp.evaluate(nativeRpc('tabs.list'));
      const tab0 = tabsList.find((t) => (t.url || '').includes('/0'));
      assert.ok(tab0, 'Tab 0 should be in the tabs list');

      console.log(`[E2E] Switching to tab 0`);
      // Switch to tab 0
      await shellCdp.evaluate(nativeRpc('tabs.switch', { tabId: tab0.id }));

      // Verify that tab 0 has now been realized and loaded (CDP target exists and is connectable!)
      const tab0Cdp = await browser.connectToTarget((t) => (t.url || '').includes('/0'), 3000);
      assert.ok(tab0Cdp, 'Tab 0 should be connectable after switching to it');
      tab0Cdp.close();

    } finally {
      if (shellCdp) shellCdp.close();
      if (browser) await browser.close();
      await server.close();
      await rm(profileRoot, { recursive: true, force: true });
    }
  }
);
