import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  pressKey,
  startStaticServer,
  timeoutMs,
  typeTextWithKeys,
  waitFor,
} from './helpers/browserHarness.js';
import { connectShell, waitForAddress } from './helpers/e2eUtils.js';

test('new tab search box navigates direct URLs',
  { timeout: timeoutMs + 15000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><title>New Tab Navigation E2E</title><main>New Tab Navigation E2E</main>');
    });
    const browser = await launchDevBrowser({
      settings: {
        searchEngine: 'google',
        historyEnabled: true,
        downloadsEnabled: true,
        startupBehavior: 'newtab',
        startupUrls: [],
        httpsOnly: false,
        blockInsecure: false,
        appearanceMode: 'auto',
      },
    });
    let shellCdp = null;
    let newtabCdp = null;
    let pageCdp = null;
    try {
      shellCdp = await connectShell(browser);
      newtabCdp = await browser.connectToTarget((target) =>
        /newtab\.html/i.test(target.url || '') ||
        /New Tab/i.test(target.title || ''),
        15000,
      );
      await waitFor(
        newtabCdp,
        `document.querySelector('input')?.placeholder || ''`,
        (placeholder) => /Google/i.test(placeholder),
        15000,
      );
      const directUrl = `${server.origin}/from-newtab`;
      await clickSelector(newtabCdp, 'input');
      await newtabCdp.evaluate(`
        (() => {
          const input = document.querySelector('input');
          input?.select();
        })()
      `);
      await typeTextWithKeys(newtabCdp, directUrl);
      await waitFor(newtabCdp, `document.querySelector('input')?.value || ''`, (value) => value === directUrl, 5000);
      await pressKey(newtabCdp, 'Enter');

      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(directUrl) ||
        /New Tab Navigation E2E/i.test(target.title || ''),
        15000,
      );
      const title = await waitFor(pageCdp, `document.title`, (value) => value === 'New Tab Navigation E2E', 15000);
      assert.equal(title, 'New Tab Navigation E2E');
      await waitForAddress(shellCdp, (value) => value.includes('/from-newtab'));
    } finally {
      if (pageCdp) pageCdp.close();
      if (newtabCdp) newtabCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
      await server.close();
    }
  });
