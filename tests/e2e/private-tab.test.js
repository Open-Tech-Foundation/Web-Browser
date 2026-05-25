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
import { connectShell, pressShortcut, setSitePermissionFromUi } from './helpers/e2eUtils.js';

// Synthetic left-click at an element's center inside a given (content) target.
async function clickElementInTarget(cdp, selector) {
  const rect = await cdp.evaluate(`
    (() => {
      const el = document.querySelector(${JSON.stringify(selector)});
      if (!el) return null;
      const r = el.getBoundingClientRect();
      return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
    })()
  `);
  if (!rect) throw new Error(`element not found: ${selector}`);
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: rect.x, y: rect.y });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: rect.x, y: rect.y, button: 'left', clickCount: 1 });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: rect.x, y: rect.y, button: 'left', clickCount: 1 });
}

// Each tab carries a violet ring class only when the backend flags it private
// (BuildTabJson -> normalizeTab -> TabStrip). Counting those elements exercises
// the full stack: in-memory request context + private flag in C++, the tab
// JSON, and the React rendering.
const tabCountExpression = `document.querySelectorAll('a[href^="tab-context-menu:"]').length`;
const privateTabCountExpression = `[...document.querySelectorAll('a[href^="tab-context-menu:"]')]
  .filter((tab) => (tab.className || '').includes('ring-violet-500/40')).length`;
const tabCountExpr = `document.querySelectorAll('a[href^="tab-context-menu:"]').length`;
const allTabTextExpr = `[...document.querySelectorAll('a[href^="tab-context-menu:"]')]
  .map((tab) => tab.textContent || '').join(' | ')`;

test('Ctrl+Shift+N opens a private tab marked distinctly',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const initialCount = await waitFor(
        browser.cdp,
        tabCountExpression,
        (count) => count >= 1,
      );
      const initialPrivate = await browser.cdp.evaluate(privateTabCountExpression);
      assert.equal(initialPrivate, 0, 'no private tabs should exist at startup');

      // Ctrl+Shift+N. CDP modifier bits: Ctrl=2, Shift=8.
      await pressShortcut(browser.cdp, 'N', 'KeyN', 78, 10);

      await waitFor(
        browser.cdp,
        tabCountExpression,
        (count) => count === initialCount + 1,
        15000,
      );
      const privateCount = await waitFor(
        browser.cdp,
        privateTabCountExpression,
        (count) => count === 1,
        15000,
      );
      assert.equal(privateCount, 1);
    } finally {
      await browser.close();
    }
  });

test('app menu "Private Tab" opens a private tab',
  { timeout: timeoutMs + 20000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let menuCdp = null;
    try {
      shellCdp = await connectShell(browser);
      const initialCount = await waitFor(shellCdp, tabCountExpression, (count) => count >= 1);

      await waitFor(shellCdp, `!!document.querySelector('button[title="Menu"]')`, Boolean);
      await clickSelector(shellCdp, 'button[title="Menu"]');

      menuCdp = await browser.connectToTarget((target) =>
        /appmenu\.html/i.test(target.url || '') ||
        /App Menu/i.test(target.title || ''),
        15000,
      );
      await waitFor(
        menuCdp,
        `document.body.innerText`,
        (text) => /private tab/i.test(text),
        15000,
      );

      await clickByText(menuCdp, 'button', 'Private Tab');

      await waitFor(shellCdp, tabCountExpression, (count) => count === initialCount + 1, 15000);
      const privateCount = await waitFor(
        shellCdp,
        privateTabCountExpression,
        (count) => count === 1,
        15000,
      );
      assert.equal(privateCount, 1);
    } finally {
      if (menuCdp) menuCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('private tab web storage is wiped when the last private tab closes',
  { timeout: timeoutMs + 30000 },
  async () => {
    // Same origin for both private tabs, distinct paths so each gets its own
    // CDP target. Storage (localStorage/cookies) is keyed by origin, so if the
    // private session leaked, the second tab would still see the first's data.
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><html><head><title>Private Storage E2E</title></head><body><main>private storage e2e</main></body></html>');
    });
    const browser = await launchDevBrowser();
    try {
      const initialCount = await waitFor(browser.cdp, tabCountExpression, (count) => count >= 1);

      // First private tab: write localStorage + a cookie.
      await pressShortcut(browser.cdp, 'N', 'KeyN', 78, 10);
      await waitFor(browser.cdp, privateTabCountExpression, (count) => count === 1, 15000);
      await navigateFromAddressBar(browser.cdp, `${server.origin}/first`);

      const firstCdp = await browser.connectToTarget(
        (target) => (target.url || '').includes('/first'),
        15000,
      );
      try {
        await waitFor(firstCdp, `document.readyState`, (state) => state === 'complete', 15000);
        const written = await firstCdp.evaluate(`
          (() => {
            localStorage.setItem('otf-priv', 'secret');
            document.cookie = 'otfpriv=secret; path=/';
            return { ls: localStorage.getItem('otf-priv'), cookie: document.cookie };
          })()
        `);
        assert.equal(written.ls, 'secret', 'localStorage should be set in the first private tab');
        assert.ok(written.cookie.includes('otfpriv=secret'), 'cookie should be set in the first private tab');
      } finally {
        firstCdp.close();
      }

      // Close the only private tab (Ctrl+W on the active tab). This releases the
      // shared in-memory private context, discarding its storage.
      await pressShortcut(browser.cdp, 'w', 'KeyW', 87, 2);
      await waitFor(browser.cdp, privateTabCountExpression, (count) => count === 0, 15000);
      await waitFor(browser.cdp, tabCountExpression, (count) => count === initialCount, 15000);

      // Second private tab on the same origin must start with empty storage.
      await pressShortcut(browser.cdp, 'N', 'KeyN', 78, 10);
      await waitFor(browser.cdp, privateTabCountExpression, (count) => count === 1, 15000);
      await navigateFromAddressBar(browser.cdp, `${server.origin}/second`);

      const secondCdp = await browser.connectToTarget(
        (target) => (target.url || '').includes('/second'),
        15000,
      );
      try {
        await waitFor(secondCdp, `document.readyState`, (state) => state === 'complete', 15000);
        const read = await secondCdp.evaluate(`
          (() => ({ ls: localStorage.getItem('otf-priv'), cookie: document.cookie }))()
        `);
        assert.equal(read.ls, null, 'localStorage from the closed private tab must be gone');
        assert.ok(!read.cookie.includes('otfpriv'), 'cookie from the closed private tab must be gone');
      } finally {
        secondCdp.close();
      }
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('site-data page opened from a private tab inspects the private context, not the global profile',
  { timeout: timeoutMs + 40000 },
  async () => {
    // Set-Cookie per path so the regular and private tabs land different
    // cookies in their respective contexts for the same origin.
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
      const headers = { 'content-type': 'text/html; charset=utf-8' };
      if (req.url.startsWith('/regular')) headers['set-cookie'] = 'regcookie=present; path=/';
      if (req.url.startsWith('/private')) headers['set-cookie'] = 'privcookie=present; path=/';
      res.writeHead(200, headers);
      res.end('<!doctype html><title>sitedata-context</title><main>sitedata context</main>');
    });
    const browser = await launchDevBrowser();
    try {
      // Regular tab gets regcookie in the global profile.
      await navigateFromAddressBar(browser.cdp, `${server.origin}/regular`);
      await waitFor(
        browser.cdp,
        `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
        (value) => value.includes('/regular'),
        15000,
      );

      // Private tab gets privcookie in its ephemeral context.
      await pressShortcut(browser.cdp, 'N', 'KeyN', 78, 10);
      await waitFor(browser.cdp, privateTabCountExpression, (count) => count === 1, 15000);
      await navigateFromAddressBar(browser.cdp, `${server.origin}/private`);
      // Wait for the navigation to actually commit (content loaded), so the
      // active tab's URL is in React state before we open the popup — the
      // address-bar text alone reflects typed input, not the committed URL.
      const privatePageCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes('/private'),
        15000,
      );
      await waitFor(privatePageCdp, `document.readyState`, (s) => s === 'complete', 15000);
      privatePageCdp.close();

      // Gate on the active tab reflecting the committed page (its title shows in
      // the strip) — proves the URL is in React state, so the toolbar button's
      // handler reads a real origin instead of an empty one.
      await waitFor(
        browser.cdp,
        `(() => {
          const active = [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
            .find((t) => t.className.includes('bg-bar-light') || t.className.includes('dark:bg-bar-dark'));
          return active ? (active.textContent || '') : '';
        })()`,
        (text) => /sitedata-context/.test(text),
        15000,
      );

      // Open the site-data flow from the private tab via the toolbar button.
      await waitFor(browser.cdp, `!!document.querySelector('button[title="Clear site data"]')`, Boolean, 15000);
      await clickSelector(browser.cdp, 'button[title="Clear site data"]');

      const clearCdp = await browser.connectToTarget((target) =>
        /cleardata\.html/i.test(target.url || '') ||
        /Clear site data/i.test(target.title || ''),
        15000,
      );
      let siteDataCdp = null;
      try {
        // Wait until the popup has hydrated the origin (it renders the host),
        // otherwise its "Manage" handler bails out on an empty origin.
        await waitFor(
          clearCdp,
          `document.body.innerText`,
          (text) => /manage/i.test(text) && /127\.0\.0\.1/.test(text),
          15000,
        );
        await clickByText(clearCdp, 'button', 'Manage');

        siteDataCdp = await browser.connectToTarget((target) =>
          /sitedata\.html/i.test(target.url || '') ||
          /browser:\/\/sitedata/i.test(target.url || ''),
          15000,
        );
        // The cookies tab is the default view, so cookie names render in the body.
        const text = await waitFor(
          siteDataCdp,
          `document.body.innerText`,
          (body) => /privcookie/.test(body) || /No cookies set/.test(body),
          15000,
        );
        assert.match(text, /privcookie/, 'private context cookie should be listed');
        assert.doesNotMatch(text, /regcookie/, 'global profile cookie must NOT leak into the private site-data view');
      } finally {
        if (siteDataCdp) siteDataCdp.close();
        clearCdp.close();
      }
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('a link opened in a new tab from a private tab is itself private',
  { timeout: timeoutMs + 40000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      if (req.url.startsWith('/linkpage')) {
        res.end('<!doctype html><title>linkpage</title><body><a id="lnk" href="/opened" target="_blank" style="display:inline-block;margin:60px;font-size:40px">open</a></body>');
      } else {
        res.end('<!doctype html><title>opened</title><main>opened</main>');
      }
    });
    const browser = await launchDevBrowser();
    try {
      // The popup gate covers target=_blank/ctrl/middle-click new tabs, so the
      // origin must be allowed to open new tabs for the click to land one.
      await setSitePermissionFromUi(browser, server.origin, 'popup', 'allow');

      // Private tab with the link page.
      await pressShortcut(browser.cdp, 'N', 'KeyN', 78, 10);
      await waitFor(browser.cdp, privateTabCountExpression, (count) => count === 1, 15000);
      await navigateFromAddressBar(browser.cdp, `${server.origin}/linkpage`);

      const pageCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes('/linkpage'),
        15000,
      );
      try {
        await waitFor(pageCdp, `document.readyState`, (s) => s === 'complete', 15000);
        await waitFor(pageCdp, `!!document.querySelector('#lnk')`, Boolean, 15000);
        await clickElementInTarget(pageCdp, '#lnk');

        // The opened link tab must inherit the private flag: now two private tabs.
        const privateCount = await waitFor(
          browser.cdp,
          privateTabCountExpression,
          (count) => count === 2,
          15000,
        );
        assert.equal(privateCount, 2, 'link opened from a private tab must also be private');
      } finally {
        pageCdp.close();
      }
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('reopen-closed-tab (Ctrl+Shift+T) does not resurrect a closed private tab',
  { timeout: timeoutMs + 30000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><title>reopen-secret</title><main>reopen secret</main>');
    });
    const browser = await launchDevBrowser();
    try {
      const initialCount = await waitFor(browser.cdp, tabCountExpr, (c) => c >= 1);

      // Open a private tab and load a real page into it.
      await pressShortcut(browser.cdp, 'N', 'KeyN', 78, 10);
      await waitFor(browser.cdp, privateTabCountExpression, (c) => c === 1, 15000);
      await navigateFromAddressBar(browser.cdp, `${server.origin}/secret`);
      await waitFor(browser.cdp, allTabTextExpr, (text) => /reopen-secret/.test(text), 15000);

      // Close it (Ctrl+W) — private count back to zero.
      await pressShortcut(browser.cdp, 'w', 'KeyW', 87, 2);
      await waitFor(browser.cdp, privateTabCountExpression, (c) => c === 0, 15000);
      await waitFor(browser.cdp, tabCountExpr, (c) => c === initialCount, 15000);

      // Reopen-closed-tab must NOT bring it back. Fire Ctrl+Shift+T, then open a
      // plain tab (Ctrl+T) as a barrier the event loop must process after it.
      await pressShortcut(browser.cdp, 'T', 'KeyT', 84, 10);
      await pressShortcut(browser.cdp, 't', 'KeyT', 84, 2);
      await waitFor(browser.cdp, tabCountExpr, (c) => c === initialCount + 1, 15000);

      const privateCount = await browser.cdp.evaluate(privateTabCountExpression);
      assert.equal(privateCount, 0, 'no private tab should be resurrected');
      const tabText = await browser.cdp.evaluate(allTabTextExpr);
      assert.doesNotMatch(tabText, /reopen-secret/, 'closed private tab must not be restored');
    } finally {
      await browser.close();
      await server.close();
    }
  });
