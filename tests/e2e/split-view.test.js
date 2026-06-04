import test from 'node:test';
import assert from 'node:assert/strict';
import { Database } from 'bun:sqlite';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

const addressInput = 'input[placeholder="Search or enter address..."]';
async function cefQuery(cdp, request) {
  return await cdp.evaluate(`
    new Promise((resolve, reject) => {
      if (typeof window.cefQuery !== 'function') {
        reject(new Error('cefQuery is not available'));
        return;
      }
      window.cefQuery({
        request: ${JSON.stringify(request)},
        onSuccess: resolve,
        onFailure: (_code, message) => reject(new Error(message || 'cefQuery failed')),
      });
    })
  `);
}

async function getTabs(cdp) {
  return JSON.parse(await cefQuery(cdp, 'get-tabs'));
}

async function getSplitState(cdp) {
  return JSON.parse(await cefQuery(cdp, 'get-split-state'));
}

async function waitForSplitState(cdp, predicate, deadlineMs = 15000) {
  return await waitFor(
    cdp,
    `new Promise((resolve) => {
      window.cefQuery({
        request: 'get-split-state',
        onSuccess: (json) => { try { resolve(JSON.parse(json)); } catch (_) { resolve({}); } },
        onFailure: () => resolve({}),
      });
    })`,
    predicate,
    deadlineMs,
  );
}

async function waitForTabs(cdp, predicate, deadlineMs = 15000) {
  return await waitFor(
    cdp,
    `new Promise((resolve) => {
      window.cefQuery({
        request: 'get-tabs',
        onSuccess: (json) => { try { resolve(JSON.parse(json)); } catch (_) { resolve([]); } },
        onFailure: () => resolve([]),
      });
    })`,
    predicate,
    deadlineMs,
  );
}

async function waitForActiveAddress(cdp, needle, deadlineMs = 15000) {
  return await waitFor(
    cdp,
    `document.querySelector(${JSON.stringify(addressInput)})?.value || ''`,
    (value) => value.includes(needle),
    deadlineMs,
  );
}

async function openUrlInNewTab(cdp, url) {
  const id = Number(await cefQuery(cdp, `new-tab:${url}`));
  assert.ok(Number.isInteger(id) && id >= 0, `new-tab returned invalid id: ${id}`);
  await waitForTabs(cdp, (tabs) => tabs.some((tab) => tab.id === id && (tab.url || '').includes(url)), 20000);
  return id;
}

async function createSplitWithPlaceholder(browser, leftUrl) {
  await navigateFromAddressBar(browser.cdp, leftUrl);
  const left = new URL(leftUrl);
  await waitForActiveAddress(browser.cdp, `${left.host}${left.pathname}`, 20000);

  const leftTabId = Number(await cefQuery(browser.cdp, 'get-active-tab'));
  assert.ok(leftTabId >= 0, 'left tab should be active before splitting');

  assert.equal(
    await browser.cdp.evaluate(`document.querySelectorAll('button[title="Open in split view"]').length`),
    0,
    'tab-level add-to-split button must not be rendered',
  );

  await clickSelector(browser.cdp, 'button[title="Split current tab"]');
  const state = await waitForSplitState(
    browser.cdp,
    (item) => item.enabled === true && item.leftTabId === leftTabId && item.rightTabId >= 0,
    20000,
  );
  assert.equal(
    await browser.cdp.evaluate(`document.querySelectorAll('button[title="Open in split view"]').length`),
    0,
    'tab-level add-to-split button must remain hidden when split view exists',
  );

  const tabs = await getTabs(browser.cdp);
  const placeholder = tabs.find((tab) => tab.id === state.rightTabId);
  assert.ok(placeholder, 'placeholder tab must exist in backend tabs while the right pane is empty');
  assert.match(`${placeholder.title || ''} ${placeholder.url || ''}`, /split-placeholder|add a tab to split view/i);
  assert.equal(state.activeTabId, leftTabId);
  return state;
}

function readPersistedSplitState(profileRoot) {
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
    const row = db.query(`SELECT value FROM workspace_state WHERE key = 'ws:1:split'`).get();
    return row?.value || '';
  } finally {
    db.close();
  }
}

function createPageServer(unique) {
  return startStaticServer((req, res) => {
    if (req.url === '/favicon.ico') {
      res.writeHead(204);
      res.end();
      return;
    }
    const pathname = new URL(req.url, 'http://127.0.0.1').pathname;
    const slug = pathname.includes('right') ? 'right' :
      pathname.includes('third') ? 'third' :
        pathname.includes('overflow') ? pathname.replace(/^\//, '') : 'left';
    const title = `Split ${slug} ${unique}`;
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
    res.end(`<!doctype html>
      <html>
        <head><title>${title}</title></head>
        <body>
          <main tabindex="0">
            <h1>${title}</h1>
            <a href="/third?case=${unique}">third page</a>
          </main>
        </body>
      </html>`);
  });
}

function tabStripVisibilityExpression(activeNeedle, splitNeedle) {
  return `(() => {
    const viewport = [...document.querySelectorAll('div')]
      .find((el) => el.querySelector('a[title="New tab"]') && el.scrollWidth > el.clientWidth);
    if (!viewport) return { ready: false, reason: 'no viewport' };
    const viewportRect = viewport.getBoundingClientRect();
    const isVisibleInViewport = (el) => {
      if (!el) return false;
      const rect = el.getBoundingClientRect();
      return rect.width > 0 &&
        rect.right > viewportRect.left + 4 &&
        rect.left < viewportRect.right - 40;
    };
    const activeNeedle = ${JSON.stringify(activeNeedle)};
    const splitNeedle = ${JSON.stringify(splitNeedle)};
    const activeTab = [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
      .find((el) => (el.textContent || '').includes(activeNeedle));
    const splitGroup = [...viewport.children]
      .find((el) => (el.textContent || '').includes(splitNeedle));
    return {
      ready: true,
      scrollLeft: viewport.scrollLeft,
      maxScrollLeft: Math.max(0, viewport.scrollWidth - viewport.clientWidth),
      activeVisible: isVisibleInViewport(activeTab),
      splitVisible: isVisibleInViewport(splitGroup),
      activeText: activeTab?.textContent || '',
      splitText: splitGroup?.textContent || '',
    };
  })()`;
}

test('address-bar split creates a backend-owned split with an instructional placeholder',
  { timeout: timeoutMs + 25000 },
  async () => {
    const unique = Date.now();
    const server = await createPageServer(unique);
    const browser = await launchDevBrowser();
    try {
      const leftUrl = `${server.origin}/left?case=${unique}`;
      const initial = await createSplitWithPlaceholder(browser, leftUrl);

      await waitFor(
        browser.cdp,
        `document.querySelector('button[title="Split view options"]')?.className || ''`,
        (className) => className.includes('text-brand-orange'),
        15000,
      );
      await waitFor(
        browser.cdp,
        `document.body?.innerText || ''`,
        (text) => /Split left/i.test(text) && /Split Placeholder|Add a tab to split view/i.test(text),
        15000,
      );

      const placeholderTarget = await browser.connectToTarget((target) =>
        /splitplaceholder\.html/i.test(target.url || '') ||
        /browser:\/\/split-placeholder/i.test(target.url || ''),
        15000,
      );
      try {
        await waitFor(
          placeholderTarget,
          `document.body?.innerText || ''`,
          (text) => /choose a tab for this pane/i.test(text) && /no other tabs available|select an existing workspace tab/i.test(text),
          15000,
        );
      } finally {
        placeholderTarget.close();
      }
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('placeholder tab list adds an existing workspace tab and destroys the placeholder',
  { timeout: timeoutMs + 30000 },
  async () => {
    const unique = Date.now();
    const server = await createPageServer(unique);
    const browser = await launchDevBrowser();
    try {
      const leftUrl = `${server.origin}/left?case=${unique}`;
      const rightUrl = `${server.origin}/right?case=${unique}`;
      const initial = await createSplitWithPlaceholder(browser, leftUrl);

      const rightTabId = await openUrlInNewTab(browser.cdp, rightUrl);
      assert.equal(
        await browser.cdp.evaluate(`document.querySelectorAll('button[title="Open in split view"]').length`),
        0,
        'tab-level add-to-split button must not appear after opening another tab',
      );
      await cefQuery(browser.cdp, `switch-tab:${initial.rightTabId}`);
      await waitForSplitState(
        browser.cdp,
        (item) => item.enabled === true && item.activeTabId === initial.rightTabId,
        20000,
      );

      const placeholderTarget = await browser.connectToTarget((target) =>
        /splitplaceholder\.html/i.test(target.url || '') ||
        /browser:\/\/split-placeholder/i.test(target.url || ''),
        15000,
      );
      try {
        await waitFor(
          placeholderTarget,
          `document.body?.innerText || ''`,
          (text) => text.includes(`Split right ${unique}`),
          15000,
        );
        await clickByText(placeholderTarget, 'button', `Split right ${unique}`);
      } finally {
        placeholderTarget.close();
      }
      const state = await waitForSplitState(
        browser.cdp,
        (item) =>
          item.enabled === true &&
          item.leftTabId === initial.leftTabId &&
          item.rightTabId === rightTabId &&
          item.activeTabId === rightTabId,
        20000,
      );

      assert.deepEqual(
        {
          left: state.leftTabId,
          right: state.rightTabId,
          active: state.activeTabId,
        },
        {
          left: initial.leftTabId,
          right: rightTabId,
          active: rightTabId,
        },
      );
      await waitForActiveAddress(browser.cdp, '/right', 20000);

      const tabs = await waitForTabs(
        browser.cdp,
        (items) =>
          items.some((tab) => tab.id === initial.leftTabId) &&
          items.some((tab) => tab.id === rightTabId) &&
          !items.some((tab) => /add a tab to split view/i.test(tab.title || '')),
        20000,
      );
      assert.equal(tabs.some((tab) => tab.id === initial.rightTabId), false,
        'placeholder tab must be removed from backend tabs after a real pane is added');

      await waitFor(
        browser.cdp,
        `document.body?.innerText || ''`,
        (text) => /Split left/i.test(text) && /Split right/i.test(text),
        15000,
      );
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('split state survives ordinary tab switching and address state follows the active split pane',
  { timeout: timeoutMs + 35000 },
  async () => {
    const unique = Date.now();
    const server = await createPageServer(unique);
    const browser = await launchDevBrowser();
    try {
      const leftUrl = `${server.origin}/left?case=${unique}`;
      const rightUrl = `${server.origin}/right?case=${unique}`;
      const thirdUrl = `${server.origin}/third?case=${unique}`;
      const initial = await createSplitWithPlaceholder(browser, leftUrl);
      const rightTabId = await openUrlInNewTab(browser.cdp, rightUrl);
      await cefQuery(browser.cdp, `add-tab-to-split:${rightTabId}`);
      await waitForSplitState(browser.cdp, (item) => item.rightTabId === rightTabId && item.activeTabId === rightTabId, 20000);

      const thirdTabId = await openUrlInNewTab(browser.cdp, thirdUrl);
      await waitForActiveAddress(browser.cdp, '/third', 20000);
      let state = await getSplitState(browser.cdp);
      assert.equal(state.enabled, true, 'opening an ordinary tab must not clear backend split state');
      assert.equal(state.leftTabId, initial.leftTabId);
      assert.equal(state.rightTabId, rightTabId);

      await cefQuery(browser.cdp, `switch-tab:${initial.leftTabId}`);
      await waitForActiveAddress(browser.cdp, '/left', 20000);
      state = await waitForSplitState(
        browser.cdp,
        (item) => item.enabled === true && item.activeTabId === initial.leftTabId,
        15000,
      );
      assert.equal(state.leftTabId, initial.leftTabId);
      assert.equal(state.rightTabId, rightTabId);

      await cefQuery(browser.cdp, `switch-tab:${rightTabId}`);
      await waitForActiveAddress(browser.cdp, '/right', 20000);
      state = await waitForSplitState(
        browser.cdp,
        (item) => item.enabled === true && item.activeTabId === rightTabId,
        15000,
      );
      assert.equal(state.leftTabId, initial.leftTabId);
      assert.equal(state.rightTabId, rightTabId);

      await cefQuery(browser.cdp, `switch-tab:${thirdTabId}`);
      await waitForActiveAddress(browser.cdp, '/third', 20000);
      state = await getSplitState(browser.cdp);
      assert.equal(state.enabled, true, 'switching back to a normal tab must preserve split state in backend');
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('tab strip keeps a later active tab in view instead of snapping back to the split group',
  { timeout: timeoutMs + 50000 },
  async () => {
    const unique = Date.now();
    const server = await createPageServer(unique);
    const browser = await launchDevBrowser();
    try {
      const leftUrl = `${server.origin}/left?case=${unique}`;
      const rightUrl = `${server.origin}/right?case=${unique}`;
      const initial = await createSplitWithPlaceholder(browser, leftUrl);
      const rightTabId = await openUrlInNewTab(browser.cdp, rightUrl);
      await cefQuery(browser.cdp, `add-tab-to-split:${rightTabId}`);
      await waitForSplitState(
        browser.cdp,
        (item) =>
          item.enabled === true &&
          item.leftTabId === initial.leftTabId &&
          item.rightTabId === rightTabId,
        20000,
      );

      const overflowCount = 14;
      for (let i = 0; i < overflowCount; i += 1) {
        await openUrlInNewTab(browser.cdp, `${server.origin}/overflow-${i}?case=${unique}`);
      }
      const activeNeedle = `Split overflow-${overflowCount - 1} ${unique}`;
      await waitForActiveAddress(browser.cdp, `/overflow-${overflowCount - 1}`, 20000);

      const visible = await waitFor(
        browser.cdp,
        tabStripVisibilityExpression(activeNeedle, `Split left ${unique}`),
        (metrics) =>
          metrics.ready === true &&
          metrics.maxScrollLeft > 0 &&
          metrics.activeVisible === true &&
          metrics.splitVisible === false,
        20000,
      );
      assert.ok(visible.scrollLeft > 0, `expected tab strip to remain scrolled right, got ${JSON.stringify(visible)}`);

      await cefQuery(browser.cdp, `switch-tab:${rightTabId}`);
      await waitForActiveAddress(browser.cdp, '/right', 20000);
      const splitVisible = await waitFor(
        browser.cdp,
        tabStripVisibilityExpression(activeNeedle, `Split left ${unique}`),
        (metrics) =>
          metrics.ready === true &&
          metrics.splitVisible === true &&
          metrics.activeVisible === false,
        20000,
      );
      assert.ok(splitVisible.splitText.includes(`Split left ${unique}`));
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('split popup exposes native-frame actions for swap and pane close',
  { timeout: timeoutMs + 35000 },
  async () => {
    const unique = Date.now();
    const server = await createPageServer(unique);
    const browser = await launchDevBrowser();
    try {
      const leftUrl = `${server.origin}/left?case=${unique}`;
      const rightUrl = `${server.origin}/right?case=${unique}`;
      const initial = await createSplitWithPlaceholder(browser, leftUrl);
      const rightTabId = await openUrlInNewTab(browser.cdp, rightUrl);
      await cefQuery(browser.cdp, `add-tab-to-split:${rightTabId}`);
      await waitForSplitState(browser.cdp, (item) => item.rightTabId === rightTabId, 20000);
      await waitFor(
        browser.cdp,
        `!!document.querySelector('button[title="Split view options"]')`,
        Boolean,
        15000,
      );

      await clickSelector(browser.cdp, 'button[title="Split view options"]');
      const popup = await browser.connectToTarget((target) =>
        (target.title || '') === 'Split View' ||
        /splitmenu\.html/i.test(target.url || '') ||
        /browser:\/\/splitmenu/i.test(target.url || ''),
        15000,
      );
      try {
        await waitFor(
          popup,
          `document.body?.innerText || ''`,
          (text) =>
            text.includes('Split View') &&
            text.includes('Exit split view') &&
            text.includes('Swap panes') &&
            text.includes('Close left pane') &&
            text.includes('Close right pane'),
          15000,
        );
        assert.equal(
          await popup.evaluate(`!!document.querySelector('.popup-container button[aria-label="Close"]')`),
          true,
          'split popup must use the shared native popup frame with close button',
        );

        await clickByText(popup, 'button', 'Swap panes');
      } finally {
        popup.close();
      }

      let state = await waitForSplitState(
        browser.cdp,
        (item) => item.enabled === true && item.leftTabId === rightTabId && item.rightTabId === initial.leftTabId,
        15000,
      );
      assert.equal(state.leftTabId, rightTabId);
      assert.equal(state.rightTabId, initial.leftTabId);

      await cefQuery(browser.cdp, 'close-split-pane:right');
      state = await waitForSplitState(browser.cdp, (item) => item.enabled === false, 20000);
      assert.equal(state.enabled, false);
      const tabs = await getTabs(browser.cdp);
      assert.equal(tabs.some((tab) => tab.id === initial.leftTabId), false,
        'closing the right split pane after swap should close the original left tab');
      assert.equal(tabs.some((tab) => tab.id === rightTabId), true,
        'the surviving split pane should remain as a normal tab');
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('real split panes are persisted and restored after restart, placeholders are not persisted',
  { timeout: timeoutMs + 60000 },
  async () => {
    const unique = Date.now();
    const server = await createPageServer(unique);
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-split-view-'));
    let browser = null;
    try {
      const leftUrl = `${server.origin}/left?case=${unique}`;
      const rightUrl = `${server.origin}/right?case=${unique}`;

      browser = await launchDevBrowser({
        profileRoot,
        preserveProfile: true,
        settings: { startupBehavior: 'continue' },
      });
      const placeholderState = await createSplitWithPlaceholder(browser, leftUrl);
      assert.equal(placeholderState.enabled, true);
      assert.equal(
        readPersistedSplitState(profileRoot),
        '',
        'placeholder-only split state must stay in memory and not be written as a restorable split',
      );

      const rightTabId = await openUrlInNewTab(browser.cdp, rightUrl);
      await cefQuery(browser.cdp, `add-tab-to-split:${rightTabId}`);
      await waitForSplitState(browser.cdp, (item) => item.rightTabId === rightTabId && item.activeTabId === rightTabId, 20000);
      const persistedBeforeRestart = readPersistedSplitState(profileRoot);
      assert.match(persistedBeforeRestart, /"enabled":true/);
      assert.doesNotMatch(persistedBeforeRestart, /split-placeholder/i);
      await browser.close();

      browser = await launchDevBrowser({
        profileRoot,
        preserveProfile: true,
        settings: { startupBehavior: 'continue' },
      });
      await waitFor(
        browser.cdp,
        `document.querySelector('button[title="Split view options"]')?.className || ''`,
        (className) => className.includes('text-brand-orange'),
        30000,
      );
      const restoredState = await waitForSplitState(
        browser.cdp,
        (item) => item.enabled === true && item.leftTabId >= 0 && item.rightTabId >= 0 && item.leftTabId !== item.rightTabId,
        30000,
      );
      assert.equal(restoredState.enabled, true);

      const restoredTabs = await getTabs(browser.cdp);
      const restoredLeft = restoredTabs.find((tab) => tab.id === restoredState.leftTabId);
      const restoredRight = restoredTabs.find((tab) => tab.id === restoredState.rightTabId);
      assert.ok(restoredLeft?.url?.includes('/left'), `expected restored left split tab, got ${JSON.stringify(restoredLeft)}`);
      assert.ok(restoredRight?.url?.includes('/right'), `expected restored right split tab, got ${JSON.stringify(restoredRight)}`);
      assert.equal(restoredTabs.some((tab) => /split-placeholder/i.test(`${tab.url} ${tab.title}`)), false);
    } finally {
      if (browser) await browser.close();
      await rm(profileRoot, { recursive: true, force: true });
      await server.close();
    }
  });
