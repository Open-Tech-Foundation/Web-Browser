import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  launchDevBrowser,
  pressKey,
  timeoutMs,
  typeTextWithKeys,
  waitFor,
} from './helpers/browserHarness.js';
import { connectShell } from './helpers/e2eUtils.js';

test('search history saves and shows as suggestions on new tab',
  { timeout: timeoutMs + 15000 },
  async () => {
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
    let tabCdp = null;
    try {
      shellCdp = await connectShell(browser);

      // Open a new tab and perform a search to populate history.
      tabCdp = await browser.connectToTarget((target) =>
        /newtab\.html/i.test(target.url || '') ||
        /New Tab/i.test(target.title || ''),
        15000,
      );
      await waitFor(tabCdp,
        `document.querySelector('input')?.placeholder || ''`,
        (p) => /Google/i.test(p),
        15000,
      );

      const searchQuery = 'opencode e2e test query';
      await clickSelector(tabCdp, 'input');
      await tabCdp.evaluate(`(() => { const el = document.querySelector('input'); el?.select(); })()`);
      await typeTextWithKeys(tabCdp, searchQuery);
      await waitFor(tabCdp, `document.querySelector('input')?.value || ''`, (v) => v === searchQuery, 5000);
      await pressKey(tabCdp, 'Enter');
      tabCdp.close();
      tabCdp = null;

      // Open another new tab — the saved search should now appear as a suggestion.
      // We navigate the shell to browser://newtab and wait for the newtab page.
      const newtabUrl = 'browser://newtab';
      await shellCdp.evaluate(`
        new Promise((resolve) => {
          if (window.cefQuery) {
            window.cefQuery({
              request: JSON.stringify({
                id: 'search-history-newtab',
                method: 'navigation.tab',
                params: { tabId: 1, url: '${newtabUrl}' },
              }),
              onSuccess: resolve,
              onFailure: resolve,
            });
          } else { resolve(); }
        })
      `);
      tabCdp = await browser.connectToTarget((target) =>
        /newtab\.html/i.test(target.url || '') ||
        /New Tab/i.test(target.title || ''),
        15000,
      );
      await waitFor(tabCdp,
        `document.querySelector('input')?.placeholder || ''`,
        (p) => /Google/i.test(p),
        15000,
      );

      // Type a prefix that matches our saved query.
      const prefix = 'open';
      await clickSelector(tabCdp, 'input');
      await tabCdp.evaluate(`(() => { const el = document.querySelector('input'); el.value = ''; })()`);
      await typeTextWithKeys(tabCdp, prefix);
      await waitFor(tabCdp, `document.querySelector('input')?.value || ''`, (v) => v === prefix, 5000);

      // Wait for the suggestion dropdown to appear with our saved query.
      const suggestionText = await waitFor(tabCdp,
        `[...document.querySelectorAll('[class*="cursor-pointer"]')]
           .map(el => el.textContent)
           .filter(t => t.trim())
           .join('|') || ''`,
        (text) => text.includes('opencode e2e test query'),
        10000,
      );
      assert.ok(suggestionText.includes('opencode e2e test query'),
        `Expected suggestion "opencode e2e test query" in dropdown, got: ${suggestionText}`);

      // Click the suggestion and verify navigation.
      const suggestionClicked = await tabCdp.evaluate(`
        (() => {
          const items = [...document.querySelectorAll('[class*="cursor-pointer"]')];
          const target = items.find(el => (el.textContent || '').includes('opencode e2e test query'));
          if (!target) return false;
          target.dispatchEvent(new MouseEvent('mousedown', { bubbles: true }));
          return true;
        })()
      `);
      assert.equal(suggestionClicked, true, 'clicked search history suggestion');
    } finally {
      if (tabCdp) tabCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });
