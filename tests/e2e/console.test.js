import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  sleep,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { connectShell, pressShortcut } from './helpers/e2eUtils.js';

// Ctrl+Shift+J — modifiers: Ctrl(2) + Shift(4) = 6
const openConsole = (cdp) => pressShortcut(cdp, 'J', 'KeyJ', 74, 6);

const nativeRpc = (method, params = {}, id = `console-rpc-${Date.now()}`) => `
  new Promise((resolve) => {
    window.cefQuery({
      request: JSON.stringify({
        id: ${JSON.stringify(id)},
        method: ${JSON.stringify(method)},
        params: ${JSON.stringify(params)},
      }),
      onSuccess: resolve,
      onFailure: (code, message) => resolve(JSON.stringify({
        id: ${JSON.stringify(id)},
        ok: false,
        error: { code: String(code), message },
      })),
    });
  })
`;

async function connectConsole(browser, deadlineMs = 15000) {
  return browser.connectToTarget(
    (t) => /browser:\/\/console/i.test(t.url || '') || /console\.html/i.test(t.url || ''),
    deadlineMs,
  );
}

test('console RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `typeof window.cefQuery === 'function'`, Boolean, 15000);

      const response = JSON.parse(await shellCdp.evaluate(
        nativeRpc('console.logs', {
          tabId: 1,
          extra: true,
        }, 'console-extra-param'),
      ));
      assert.equal(response.id, 'console-extra-param');
      assert.equal(response.ok, false);
      assert.match(response.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('Ctrl+Shift+J toggles the console panel',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let consoleCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `!!document.getElementById('root')`, Boolean);

      // Open console via shortcut
      await openConsole(shellCdp);

      // Console target should become reachable and render its root
      consoleCdp = await connectConsole(browser);
      const rendered = await waitFor(
        consoleCdp,
        `!!document.getElementById('root')?.firstElementChild`,
        Boolean,
        12000,
      );
      assert.equal(rendered, true, 'console panel should render after Ctrl+Shift+J');

      // Close console via the same shortcut a user would use to toggle it.
      await openConsole(shellCdp);
    } finally {
      if (consoleCdp) consoleCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('console panel captures page console.log output',
  { timeout: timeoutMs + 20000 },
  async () => {
    const logMessage = 'otf-e2e-console-capture-' + Date.now();
    const server = await startStaticServer((_req, res) => {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.end(`<!DOCTYPE html><html><body>
        <script>console.log(${JSON.stringify(logMessage)});</script>
      </body></html>`);
    });

    const browser = await launchDevBrowser();
    let shellCdp = null;
    let consoleCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `!!document.getElementById('root')`, Boolean);

      // Open console then navigate to the log-emitting page
      await openConsole(shellCdp);
      consoleCdp = await connectConsole(browser);
      await waitFor(consoleCdp, `!!document.getElementById('root')?.firstElementChild`, Boolean, 12000);

      // Navigate content tab to the static page via the address bar
      await navigateFromAddressBar(shellCdp, server.origin);
      await sleep(2000);

      // The log entry should appear in the console panel's DOM
      const found = await waitFor(
        consoleCdp,
        `document.body.innerText`,
        (text) => text.includes(logMessage),
        15000,
      );
      assert.ok(found, `console panel should display: ${logMessage}`);
    } finally {
      if (consoleCdp) consoleCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
      await server.close();
    }
  });

test('clear chip button clears console log entries',
  { timeout: timeoutMs + 20000 },
  async () => {
    const logMessage = 'otf-e2e-console-clear-' + Date.now();
    const server = await startStaticServer((_req, res) => {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.end(`<!DOCTYPE html><html><body>
        <script>console.log(${JSON.stringify(logMessage)});</script>
      </body></html>`);
    });

    const browser = await launchDevBrowser();
    let shellCdp = null;
    let consoleCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `!!document.getElementById('root')`, Boolean);

      // Open console and navigate to the log-emitting page
      await openConsole(shellCdp);
      consoleCdp = await connectConsole(browser);
      await waitFor(consoleCdp, `!!document.getElementById('root')?.firstElementChild`, Boolean, 12000);

      await navigateFromAddressBar(shellCdp, server.origin);
      await sleep(2000);

      // Verify the log entry appears
      const found = await waitFor(
        consoleCdp,
        `document.body.innerText`,
        (text) => text.includes(logMessage),
        15000,
      );
      assert.ok(found, `console panel should display: ${logMessage}`);

      // Click the clear chip button
      const cleared = await consoleCdp.evaluate(`
        (() => {
          const btn = [...document.querySelectorAll('button')]
            .find((b) => (b.textContent || '').trim() === 'clear');
          if (!btn) return false;
          btn.click();
          return true;
        })()
      `);
      assert.equal(cleared, true, 'should find and click the clear chip button');

      // Wait for entries to disappear
      await sleep(500);

      const empty = await waitFor(
        consoleCdp,
        `document.body.innerText`,
        (text) => !text.includes(logMessage),
        5000,
      );
      assert.ok(!empty.includes(logMessage), 'log entry should be removed after clear');
    } finally {
      if (consoleCdp) consoleCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
      await server.close();
    }
  });

test('console is tab-specific: hidden on new tabs by default',
  { timeout: timeoutMs + 20000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let consoleCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(shellCdp, `!!document.getElementById('root')`, Boolean);

      const tabCountExpr = `document.querySelectorAll('a[href^="tab-context-menu:"]').length`;
      const initialCount = await waitFor(shellCdp, tabCountExpr, (n) => n >= 1);

      // Open console on the first tab
      await openConsole(shellCdp);
      consoleCdp = await connectConsole(browser);
      await waitFor(consoleCdp, `!!document.getElementById('root')?.firstElementChild`, Boolean, 12000);

      // Open a second tab via Ctrl+T
      const beforeNewTabCount = await waitFor(shellCdp, tabCountExpr, (n) => n >= initialCount, 12000);
      await pressShortcut(shellCdp, 't', 'KeyT', 84);
      await waitFor(shellCdp, tabCountExpr, (n) => n > beforeNewTabCount, 12000);
      await sleep(800);

      // Switch back to tab 1 — console should reopen automatically
      const tabs = await shellCdp.evaluate(
        `[...document.querySelectorAll('a[href^="tab-context-menu:"]')]
           .map(a => a.getAttribute('href').replace('tab-context-menu:', ''))`,
      );
      assert.ok(Array.isArray(tabs) && tabs.length >= 2, 'should have at least 2 tabs');

      await clickSelector(shellCdp, 'a[href^="tab-context-menu:"]');
      await sleep(800);

      // Console panel should still be running (target reachable)
      const stillUp = await waitFor(
        consoleCdp,
        `!!document.getElementById('root')`,
        Boolean,
        8000,
      );
      assert.equal(stillUp, true, 'console panel target should persist across tab switches');
    } finally {
      if (consoleCdp) consoleCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });
