import test from 'node:test';
import assert from 'node:assert/strict';
import { execSync, spawnSync } from 'node:child_process';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
  sleep,
} from './helpers/browserHarness.js';

const tabTextExpression = `[...document.querySelectorAll('a[href^="tab-context-menu:"]')].map(t => t.textContent || '')`;
const kInitialTitle = 'Initial Page Title';
const kDynamicTitle = 'Dynamic Title Changed';

async function getWindowTitle() {
  try {
    const out = execSync(
      'xdotool search --name "OTF Browser" getwindowname',
      { encoding: 'utf8', timeout: 3000 },
    );
    const lines = out.trim().split('\n');
    return lines[0] || null;
  } catch {
    return null;
  }
}

test('tab title and window title update on navigation and dynamic change',
  { timeout: timeoutMs + 20000 },
  async () => {
    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html><html><head><title>${kInitialTitle}</title></head><body><p>Title test</p></body></html>`);
    });
    const browser = await launchDevBrowser({
      settings: {
        searchEngine: 'google',
      },
    });
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'browser://settings');
      await waitFor(
        browser.cdp,
        tabTextExpression,
        (texts) => texts.some((t) => /settings/i.test(t)),
        15000,
      );
      await sleep(500);

      const tabCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || ''),
        15000,
      );
      await waitFor(tabCdp, 'document.title', (t) => /settings/i.test(t), 15000);
      tabCdp.close();

      const winTitleSettings = await getWindowTitle();
      assert.ok(winTitleSettings, 'could not read window title via xdotool');
      assert.ok(/settings/i.test(winTitleSettings),
        `window title should include "Settings" but got "${winTitleSettings}"`);

      await navigateFromAddressBar(browser.cdp, server.origin.replace(/^http:\/\//, '') + '/test');

      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${server.origin}/test`),
        15000,
      );
      await waitFor(pageCdp, 'document.title', (t) => t === kInitialTitle, 15000);

      const tabs1 = await waitFor(
        browser.cdp,
        tabTextExpression,
        (texts) => texts.some((t) => t.includes(kInitialTitle)),
        15000,
      );
      assert.ok(tabs1.some((t) => t.includes(kInitialTitle)),
        `tab strip should show "${kInitialTitle}"`);

      await sleep(500);
      const winTitle1 = await getWindowTitle();
      assert.ok(winTitle1, 'could not read window title via xdotool');
      assert.ok(winTitle1.includes(kInitialTitle),
        `window title "${winTitle1}" should include "${kInitialTitle}"`);

      await pageCdp.evaluate(`document.title = ${JSON.stringify(kDynamicTitle)}`);

      const tabs2 = await waitFor(
        browser.cdp,
        tabTextExpression,
        (texts) => texts.some((t) => t.includes(kDynamicTitle)),
        15000,
      );
      assert.ok(tabs2.some((t) => t.includes(kDynamicTitle)),
        `tab strip should show "${kDynamicTitle}"`);

      await sleep(500);
      const winTitle2 = await getWindowTitle();
      assert.ok(winTitle2, 'could not read window title via xdotool');
      assert.ok(winTitle2.includes(kDynamicTitle),
        `window title "${winTitle2}" should include "${kDynamicTitle}"`);
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  },
);
