import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickSelector,
  connectToReadyTarget,
  launchDevBrowser,
  navigateFromAddressBar,
  pressKey,
  startStaticServer,
  timeoutMs,
  typeText,
  waitFor,
} from './helpers/browserHarness.js';

async function openFindbarFromPage(browser, pageCdp) {
  await clickSelector(pageCdp, 'body');
  await pageCdp.send('Input.dispatchKeyEvent', {
    type: 'rawKeyDown',
    key: 'F',
    code: 'KeyF',
    windowsVirtualKeyCode: 70,
    nativeVirtualKeyCode: 70,
    modifiers: 2,
  });
  await pageCdp.send('Input.dispatchKeyEvent', {
    type: 'keyUp',
    key: 'F',
    code: 'KeyF',
    windowsVirtualKeyCode: 70,
    nativeVirtualKeyCode: 70,
    modifiers: 2,
  });

  const findbarCdp = await connectToReadyTarget(
    (target) =>
      (target.url || '').endsWith('/findbar.html') ||
      /findbar/i.test(target.url || '') ||
      /findbar/i.test(target.title || ''),
    `!!document.querySelector('input[placeholder="Find in page..."]')`,
    Boolean,
    15000,
  );
  return findbarCdp;
}

async function waitForFindCount(findbarCdp, expected) {
  return waitFor(
    findbarCdp,
    `document.querySelector('input[placeholder="Find in page..."]')?.nextElementSibling?.textContent || ''`,
    (text) => text.trim() === expected,
    15000,
  );
}

test('user can open the find bar and search text on a page',
  { timeout: timeoutMs + 20000 },
  async () => {
    const uniqueWord = `OTF FIND ${Date.now()}`;
    const staticServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head>
            <title>OTF Findbar E2E</title>
          </head>
          <body>
            <main>
              <h1>${uniqueWord}</h1>
              <p>${uniqueWord} appears again here.</p>
              <p>And one more ${uniqueWord} occurrence.</p>
            </main>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    let findbarCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, staticServer.origin);
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(staticServer.origin) ||
        /OTF Findbar E2E/i.test(target.title || ''),
      );

      await waitFor(
        pageCdp,
        `document.body.innerText`,
        (text) => text.includes(uniqueWord),
        15000,
      );

      findbarCdp = await openFindbarFromPage(browser, pageCdp);

      await clickSelector(findbarCdp, 'input[placeholder="Find in page..."]');
      await typeText(findbarCdp, uniqueWord);
      await waitFor(
        findbarCdp,
        `document.querySelector('input[placeholder="Find in page..."]')?.value || ''`,
        (value) => value === uniqueWord,
        15000,
      );
      await pressKey(findbarCdp, 'Enter');

      const countText = await waitFor(
        findbarCdp,
        `document.querySelector('input[placeholder="Find in page..."]')?.nextElementSibling?.textContent || ''`,
        (text) => /^[1-9]\d*\/[1-9]\d*$/.test(text.trim()),
        15000,
      );
      assert.match(countText.trim(), /^[1-9]\d*\/[1-9]\d*$/);
    } finally {
      if (findbarCdp) {
        findbarCdp.close();
      }
      if (pageCdp) {
        pageCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });

test('find bar can move between matches and respect match case',
  { timeout: timeoutMs + 20000 },
  async () => {
    const uniqueWord = `Needle${Date.now()}`;
    const lowerWord = uniqueWord.toLowerCase();
    const staticServer = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
        <html>
          <head>
            <title>OTF Findbar Navigation E2E</title>
          </head>
          <body>
            <main>
              <p>${uniqueWord} first match.</p>
              <p>${uniqueWord} second match.</p>
              <p>${uniqueWord} third match.</p>
            </main>
          </body>
        </html>`);
    });

    const browser = await launchDevBrowser();
    let pageCdp = null;
    let findbarCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, staticServer.origin);
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(staticServer.origin) ||
        /OTF Findbar Navigation E2E/i.test(target.title || ''),
      );

      await waitFor(
        pageCdp,
        `document.body.innerText`,
        (text) => text.includes(uniqueWord),
        15000,
      );

      findbarCdp = await openFindbarFromPage(browser, pageCdp);
      await clickSelector(findbarCdp, 'input[placeholder="Find in page..."]');
      await typeText(findbarCdp, uniqueWord);
      await pressKey(findbarCdp, 'Enter');
      await waitForFindCount(findbarCdp, '1/3');

      await pressKey(findbarCdp, 'Enter');
      await waitForFindCount(findbarCdp, '2/3');

      const clickedPrev = await findbarCdp.evaluate(`
        (() => {
          const button = document.querySelector('button[title="Previous (Shift+Enter)"]');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedPrev, true);
      await waitForFindCount(findbarCdp, '1/3');

      const toggledCase = await findbarCdp.evaluate(`
        (() => {
          const button = document.querySelector('button[title="Match case"]');
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(toggledCase, true);

      await clickSelector(findbarCdp, 'input[placeholder="Find in page..."]');
      await findbarCdp.send('Input.dispatchKeyEvent', {
        type: 'keyDown',
        key: 'a',
        code: 'KeyA',
        modifiers: 2,
      });
      await findbarCdp.send('Input.dispatchKeyEvent', {
        type: 'keyUp',
        key: 'a',
        code: 'KeyA',
        modifiers: 2,
      });
      await typeText(findbarCdp, lowerWord);
      await waitForFindCount(findbarCdp, '0/0');
    } finally {
      if (findbarCdp) {
        findbarCdp.close();
      }
      if (pageCdp) {
        pageCdp.close();
      }
      await browser.close();
      await staticServer.close();
    }
  });
