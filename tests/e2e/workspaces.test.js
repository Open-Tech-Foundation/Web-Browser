import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  pressKey,
  typeText,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('user can create and switch workspaces from the workspace popup',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    let workspaceCdp = null;
    try {
      const shell = browser.cdp;
      await waitFor(shell, `!!document.querySelector('button[title="Workspaces"]')`, Boolean);
      await clickSelector(shell, 'button[title="Workspaces"]');

      workspaceCdp = await browser.connectToTarget((target) =>
        (target.title || '') === 'Workspaces' ||
        /workspace\.html/i.test(target.url || ''),
      );

      await waitFor(
        workspaceCdp,
        `!![...document.querySelectorAll('button')].find((item) => (item.textContent || '').includes('New workspace'))`,
        Boolean,
      );

      const uniqueName = `QA ${Date.now()}`;
      const openedCreate = await workspaceCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('button')]
            .find((item) => (item.textContent || '').includes('New workspace'));
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(openedCreate, true);

      await waitFor(
        workspaceCdp,
        `!!document.querySelector('input[placeholder="Workspace name"]')`,
        Boolean,
      );

      await clickSelector(workspaceCdp, 'input[placeholder="Workspace name"]');
      await typeText(workspaceCdp, uniqueName);
      await pressKey(workspaceCdp, 'Enter');

      await waitFor(
        workspaceCdp,
        `!![...document.querySelectorAll('button')].find((item) => (item.textContent || '').trim() === ${JSON.stringify(uniqueName)})`,
        Boolean,
        15000,
      );

      const switched = await workspaceCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('button')]
            .find((item) => (item.textContent || '').trim() === ${JSON.stringify(uniqueName)});
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(switched, true);

      await waitFor(
        shell,
        `document.querySelector('button[title="Workspaces"]')?.textContent || ''`,
        (text) => text.includes(uniqueName),
        15000,
      );

      const shellText = await shell.evaluate(
        `document.querySelector('button[title="Workspaces"]')?.textContent || ''`
      );
      assert.ok(shellText.includes(uniqueName), `expected workspace label to change, got ${shellText}`);
    } finally {
      if (workspaceCdp) {
        workspaceCdp.close();
      }
      await browser.close();
    }
  });
