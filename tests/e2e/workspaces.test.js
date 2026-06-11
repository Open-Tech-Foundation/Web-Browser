import test from 'node:test';
import assert from 'node:assert/strict';
import { rm } from 'node:fs/promises';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  pressKey,
  sleep,
  startStaticServer,
  timeoutMs,
  typeText,
  waitFor,
} from './helpers/browserHarness.js';

// Delete a workspace by name via the popup UI.
// Clicks the (normally opacity-0) trash icon in the workspace row — pointer-events
// are still active regardless of opacity — then confirms the deletion dialog.
// Returns once the workspace name is gone from the popup list.
async function deleteWorkspace(workspaceCdp, name) {
  // Find the trash button inside the workspace row (it has no text — just an SVG icon).
  const rect = await workspaceCdp.evaluate(`
    (() => {
      const nameBtn = [...document.querySelectorAll('button')]
        .find((b) => (b.textContent || '').trim() === ${JSON.stringify(name)});
      if (!nameBtn) return null;
      const row = nameBtn.closest('[class*="group"]');
      if (!row) return null;
      const del = row.querySelector('button[title="Delete"]');
      if (!del) return null;
      del.scrollIntoView({ block: 'center', inline: 'center' });
      const r = del.getBoundingClientRect();
      return { x: r.left + r.width / 2, y: r.top + r.height / 2, width: r.width, height: r.height };
    })()
  `);
  assert.ok(rect, `delete button not found for workspace: ${name}`);
  await workspaceCdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: rect.x, y: rect.y, buttons: 0 });
  await workspaceCdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: rect.x, y: rect.y, button: 'left', buttons: 1, clickCount: 1 });
  await workspaceCdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: rect.x, y: rect.y, button: 'left', buttons: 0, clickCount: 1 });

  // Wait for confirmation row, then click the text "Delete" button (the icon buttons have no text).
  await waitFor(workspaceCdp, `document.body.innerText`, (t) => t.includes('Delete “'), 5000);
  await clickByText(workspaceCdp, 'button', 'Delete');

  // Wait until the workspace is gone from the list.
  await waitFor(
    workspaceCdp,
    `!![...document.querySelectorAll('button')].find((b) => (b.textContent || '').trim() === ${JSON.stringify(name)})`,
    (found) => !found,
    15000,
  );
}

// Create a new workspace via the popup UI and switch to it.
// Returns once the shell toolbar reflects the new workspace name.
async function createAndSwitchWorkspace(browser, name) {
  const shell = browser.cdp;
  await waitFor(shell, `!!document.querySelector('button[title="Workspaces"]')`, Boolean);
  await clickSelector(shell, 'button[title="Workspaces"]');

  const workspaceCdp = await browser.connectToTarget((target) =>
    (target.title || '') === 'Workspaces' ||
    /workspace\.html/i.test(target.url || ''),
    15000,
  );
  try {
    await waitFor(
      workspaceCdp,
      `!![...document.querySelectorAll('button')].find((b) => (b.textContent || '').includes('New workspace'))`,
      Boolean,
    );
    await clickByText(workspaceCdp, 'button', 'New workspace');

    await waitFor(workspaceCdp, `!!document.querySelector('input[placeholder="Workspace name"]')`, Boolean);
    await clickSelector(workspaceCdp, 'input[placeholder="Workspace name"]');
    await typeText(workspaceCdp, name);
    await pressKey(workspaceCdp, 'Enter');

    await waitFor(
      workspaceCdp,
      `!![...document.querySelectorAll('button')].find((b) => (b.textContent || '').trim() === ${JSON.stringify(name)})`,
      Boolean,
      15000,
    );
    await clickByText(workspaceCdp, 'button', name);
  } finally {
    workspaceCdp.close();
  }

  await waitFor(
    shell,
    `document.querySelector('button[title="Workspaces"]')?.textContent || ''`,
    (text) => text.includes(name),
    15000,
  );
}

async function cefQuery(cdp, request) {
  return await cdp.evaluate(`
    new Promise((resolve, reject) => {
      window.cefQuery({
        request: ${JSON.stringify(request)},
        onSuccess: resolve,
        onFailure: (_code, message) => reject(new Error(message || 'cefQuery failed')),
      });
    })
  `);
}

async function nativeRpc(cdp, method, params = {}) {
  return await cdp.evaluate(`
    new Promise((resolve, reject) => {
      const id = 'workspace-rpc-' + Date.now() + '-' + Math.random().toString(16).slice(2);
      window.cefQuery({
        request: JSON.stringify({
          id,
          method: ${JSON.stringify(method)},
          params: ${JSON.stringify(params)},
        }),
        onSuccess: (json) => {
          try {
            const envelope = JSON.parse(json);
            if (!envelope || envelope.id !== id || typeof envelope.ok !== 'boolean') {
              reject(new Error('Malformed native RPC response'));
              return;
            }
            if (!envelope.ok) {
              reject(new Error(envelope.error?.message || 'Native RPC failed'));
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
}

test('workspace RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      await waitFor(
        browser.cdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /New Tab/i.test(text) || /Search/i.test(text),
        15000,
      );

      const response = await browser.cdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'workspaces-extra-param',
              method: 'workspaces.list',
              params: { extra: true },
            }),
            onSuccess: resolve,
            onFailure: (code, message) => resolve(JSON.stringify({
              ok: false,
              error: { code: String(code), message },
            })),
          });
        })
      `);
      const parsed = JSON.parse(response);
      assert.equal(parsed.id, 'workspaces-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      await browser.close();
    }
  });

test('user can create and switch workspaces from the workspace popup',
  { timeout: timeoutMs + 15000 },
  async () => {
    let browser = await launchDevBrowser({ preserveProfile: true });
    const profileRoot = browser.profileRoot;
    try {
      const uniqueName = `QA ${Date.now()}`;
      await createAndSwitchWorkspace(browser, uniqueName);

      const shellText = await browser.cdp.evaluate(
        `document.querySelector('button[title="Workspaces"]')?.textContent || ''`
      );
      assert.ok(shellText.includes(uniqueName), `expected workspace label to change, got ${shellText}`);
    } finally {
      await browser.close();
    }
  });

test('guest session is isolated and discarded when closed',
  { timeout: timeoutMs + 45000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><title>guest-isolation</title><main>guest isolation</main>');
    });
    let browser = await launchDevBrowser({ preserveProfile: true });
    const profileRoot = browser.profileRoot;
    try {
      await cefQuery(browser.cdp, 'create-guest-session');
      assert.equal(await cefQuery(browser.cdp, 'is-guest-session'), 'true');
      await waitFor(
        browser.cdp,
        `document.querySelector('button[title="Guest session"]')?.textContent || ''`,
        (text) => text.includes('Guest'),
        15000,
      );
      assert.deepEqual(JSON.parse(await cefQuery(browser.cdp, 'get-workspaces')), []);
      assert.deepEqual(await nativeRpc(browser.cdp, 'bookmarks.list'), []);
      assert.deepEqual(await nativeRpc(browser.cdp, 'downloads.list'), []);

      await navigateFromAddressBar(browser.cdp, `${server.origin}/guest-one`);
      const firstGuestCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes('/guest-one'),
        15000,
      );
      try {
        await waitFor(firstGuestCdp, `document.readyState`, (s) => s === 'complete', 15000);
        await firstGuestCdp.evaluate(`
          (() => {
            localStorage.setItem('otf-guest-key', 'guest-value');
            document.cookie = 'otfguest=present; path=/';
          })()
        `);
      } finally {
        firstGuestCdp.close();
      }

      const historyWhileGuest = await nativeRpc(browser.cdp, 'history.list');
      assert.equal(historyWhileGuest.some((item) => String(item.url || '').includes('/guest-one')), false);

      const guestTabId = Number(await nativeRpc(browser.cdp, 'tabs.active'));
      await browser.cdp.evaluate(`
        window.cefQuery({
          request: JSON.stringify({
            id: 'workspace-close-guest',
            method: 'tabs.close',
            params: { tabId: ${JSON.stringify(guestTabId)} },
          }),
        });
      `);
      await sleep(1000);
      await browser.close();
      browser = await launchDevBrowser({ profileRoot, preserveProfile: true });
      assert.equal(await cefQuery(browser.cdp, 'is-guest-session'), 'false');
      await waitFor(browser.cdp, `!!document.querySelector('button[title="Workspaces"]')`, Boolean, 15000);

      await navigateFromAddressBar(browser.cdp, `${server.origin}/normal-after-guest`);
      const normalCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes('/normal-after-guest'),
        15000,
      );
      try {
        await waitFor(normalCdp, `document.readyState`, (s) => s === 'complete', 15000);
        const read = await normalCdp.evaluate(`
          (() => ({ ls: localStorage.getItem('otf-guest-key'), cookie: document.cookie }))()
        `);
        assert.equal(read.ls, null, 'guest localStorage must not leak to normal workspace');
        assert.ok(!read.cookie.includes('otfguest'), 'guest cookie must not leak to normal workspace');
      } finally {
        normalCdp.close();
      }

      await cefQuery(browser.cdp, 'create-guest-session');
      assert.equal(await cefQuery(browser.cdp, 'is-guest-session'), 'true');
      await navigateFromAddressBar(browser.cdp, `${server.origin}/guest-two`);
      const secondGuestCdp = await browser.connectToTarget(
        (t) => (t.url || '').includes('/guest-two'),
        15000,
      );
      try {
        await waitFor(secondGuestCdp, `document.readyState`, (s) => s === 'complete', 15000);
        const read = await secondGuestCdp.evaluate(`
          (() => ({ ls: localStorage.getItem('otf-guest-key'), cookie: document.cookie }))()
        `);
        assert.equal(read.ls, null, 'new guest session must start with empty localStorage');
        assert.ok(!read.cookie.includes('otfguest'), 'new guest session must start with empty cookies');
      } finally {
        secondGuestCdp.close();
      }
    } finally {
      if (browser) await browser.close();
      await rm(profileRoot, { recursive: true, force: true });
      await server.close();
    }
  });

test('cookie and localStorage are isolated between workspaces',
  { timeout: timeoutMs + 45000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><title>ws-isolation</title><main>workspace isolation</main>');
    });
    const browser = await launchDevBrowser();
    try {
      // Workspace 1 (default): write localStorage and a cookie.
      await navigateFromAddressBar(browser.cdp, `${server.origin}/ws1`);
      const ws1Cdp = await browser.connectToTarget(
        (t) => (t.url || '').includes('/ws1'),
        15000,
      );
      try {
        await waitFor(ws1Cdp, `document.readyState`, (s) => s === 'complete', 15000);
        const written = await ws1Cdp.evaluate(`
          (() => {
            localStorage.setItem('otf-ws-key', 'ws1-value');
            document.cookie = 'otfws=ws1; path=/';
            return { ls: localStorage.getItem('otf-ws-key'), cookie: document.cookie };
          })()
        `);
        assert.equal(written.ls, 'ws1-value', 'localStorage should be writable in workspace 1');
        assert.ok(written.cookie.includes('otfws=ws1'), 'cookie should be set in workspace 1');
      } finally {
        ws1Cdp.close();
      }

      // Create workspace 2, switch to it, navigate to the same origin.
      const ws2Name = `WS2-${Date.now()}`;
      await createAndSwitchWorkspace(browser, ws2Name);
      await navigateFromAddressBar(browser.cdp, `${server.origin}/ws2`);

      const ws2Cdp = await browser.connectToTarget(
        (t) => (t.url || '').includes('/ws2'),
        15000,
      );
      try {
        await waitFor(ws2Cdp, `document.readyState`, (s) => s === 'complete', 15000);
        const read = await ws2Cdp.evaluate(`
          (() => ({ ls: localStorage.getItem('otf-ws-key'), cookie: document.cookie }))()
        `);
        assert.equal(read.ls, null,
          'localStorage from workspace 1 must not be visible in workspace 2');
        assert.ok(!read.cookie.includes('otfws'),
          'cookie from workspace 1 must not be visible in workspace 2');
      } finally {
        ws2Cdp.close();
      }
    } finally {
      await browser.close();
      await server.close();
    }
  });

test('deleting a workspace removes it from the list and restores the default workspace',
  { timeout: timeoutMs + 45000 },
  async () => {
    const browser = await launchDevBrowser();
    try {
      const ws2Name = `Delete-Me-${Date.now()}`;
      await createAndSwitchWorkspace(browser, ws2Name);

      // Confirm we are now in workspace 2.
      const labelBefore = await browser.cdp.evaluate(
        `document.querySelector('button[title="Workspaces"]')?.textContent || ''`
      );
      assert.ok(labelBefore.includes(ws2Name), `expected active workspace to be ${ws2Name}`);

      // Open the popup and delete workspace 2.
      await clickSelector(browser.cdp, 'button[title="Workspaces"]');
      const workspaceCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Workspaces' ||
        /workspace\.html/i.test(target.url || ''),
        15000,
      );
      try {
        await waitFor(
          workspaceCdp,
          `!![...document.querySelectorAll('button')].find((b) => (b.textContent || '').trim() === ${JSON.stringify(ws2Name)})`,
          Boolean,
        );
        await deleteWorkspace(workspaceCdp, ws2Name);
      } finally {
        workspaceCdp.close();
      }

      // Shell toolbar must revert to the default workspace (workspace 1).
      await waitFor(
        browser.cdp,
        `document.querySelector('button[title="Workspaces"]')?.textContent || ''`,
        (text) => !text.includes(ws2Name),
        15000,
      );
      const labelAfter = await browser.cdp.evaluate(
        `document.querySelector('button[title="Workspaces"]')?.textContent || ''`
      );
      assert.ok(!labelAfter.includes(ws2Name), `deleted workspace must not appear in toolbar, got: ${labelAfter}`);
    } finally {
      await browser.close();
    }
  });
