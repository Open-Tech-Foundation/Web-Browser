import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  pressKey,
  startStaticServer,
  timeoutMs,
  typeText,
  waitFor,
} from './helpers/browserHarness.js';

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

test('user can create and switch workspaces from the workspace popup',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
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
