import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { connectShell } from './helpers/e2eUtils.js';

test('ui RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(
        shellCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /New Tab/i.test(text) || /Search/i.test(text),
        15000,
      );

      const response = await shellCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'ui-extra-param',
              method: 'ui.appMenu.toggle',
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
      assert.equal(parsed.id, 'ui-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('ui RPC accepts structured toast show request',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(
        shellCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /New Tab/i.test(text) || /Search/i.test(text),
        15000,
      );

      const response = await shellCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'ui-toast-show',
              method: 'ui.toast.show',
              params: { icon: 'copy', message: 'Link copied' },
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
      assert.equal(parsed.id, 'ui-toast-show');
      assert.equal(parsed.ok, true);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('ui popup RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(
        shellCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /New Tab/i.test(text) || /Search/i.test(text),
        15000,
      );

      const response = await shellCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'ui-popup-extra-param',
              method: 'ui.popup.hide',
              params: { name: 'workspace', extra: true },
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
      assert.equal(parsed.id, 'ui-popup-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('ui QR RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(
        shellCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /New Tab/i.test(text) || /Search/i.test(text),
        15000,
      );

      const response = await shellCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'ui-qr-extra-param',
              method: 'ui.qr.show',
              params: { url: 'https://example.test/', extra: true },
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
      assert.equal(parsed.id, 'ui-qr-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('ui snip RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(
        shellCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /New Tab/i.test(text) || /Search/i.test(text),
        15000,
      );

      const response = await shellCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'ui-snip-extra-param',
              method: 'ui.snip.start',
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
      assert.equal(parsed.id, 'ui-snip-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('ui snip preview hide RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    try {
      shellCdp = await connectShell(browser);
      await waitFor(
        shellCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /New Tab/i.test(text) || /Search/i.test(text),
        15000,
      );

      const response = await shellCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'ui-snip-preview-hide-extra-param',
              method: 'ui.snipPreview.hide',
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
      assert.equal(parsed.id, 'ui-snip-preview-hide-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });

test('app menu opens browser pages and closes after selection',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    let shellCdp = null;
    let menuCdp = null;
    try {
      shellCdp = await connectShell(browser);
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
        (text) => /downloads/i.test(text) && /bookmarks/i.test(text) && /history/i.test(text),
        15000,
      );

      await clickByText(menuCdp, 'button', 'History');

      await waitFor(
        shellCdp,
        `(() => {
          const address = document.querySelector('input[placeholder="Search or enter address..."]')?.value || '';
          const tabs = [...document.querySelectorAll('a[href^="tab-context-menu:"]')]
            .map((tab) => tab.textContent || '')
            .join(' ');
          return { address, tabs };
        })()`,
        (value) => value.address.includes('browser://history') || /history/i.test(value.tabs),
        15000,
      );
    } finally {
      if (menuCdp) menuCdp.close();
      if (shellCdp) shellCdp.close();
      await browser.close();
    }
  });
