import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
  navigateFromAddressBar,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('user can change appearance mode in Settings',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let settingsCdp = null;
    try {
      await waitFor(
        browser.cdp,
        `!!document.querySelector('button[title="Settings"]')`,
        Boolean,
      );
      await clickSelector(browser.cdp, 'button[title="Settings"]');

      settingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
      );

      await waitFor(
        settingsCdp,
        `document.body.innerText`,
        (text) => /Search Engine/i.test(text) && /Appearance/i.test(text),
      );

      await clickByText(settingsCdp, 'aside nav button', 'Appearance');
      await waitFor(
        settingsCdp,
        `document.body.innerText`,
        (text) => /Theme Mode/i.test(text) && /Light/i.test(text),
      );

      await clickByText(settingsCdp, 'main button', 'Light');
      const state = await waitFor(
        settingsCdp,
        `(() => {
          const htmlIsDark = document.documentElement.classList.contains('dark');
          const buttons = [...document.querySelectorAll('button')];
          const light = buttons.find((button) => (button.textContent || '').includes('Light'));
          return {
            htmlIsDark,
            lightText: light?.textContent || '',
            lightSelected: Boolean(light && (light.textContent || '').includes('✓')),
          };
        })()`,
        (value) => value.lightSelected === true && value.htmlIsDark === false,
      );

      assert.equal(state.lightSelected, true);
      assert.equal(state.htmlIsDark, false);
    } finally {
      if (settingsCdp) {
        settingsCdp.close();
      }
      await browser.close();
    }
  });

test('settings reset section exposes optional reset items',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let settingsCdp = null;
    try {
      await clickSelector(browser.cdp, 'button[title="Settings"]');
      settingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
      );

      await clickByText(settingsCdp, 'aside nav button', 'Reset Settings');
      await waitFor(settingsCdp, `document.body.innerText`, (text) => /default reset items/i.test(text), 15000);

      const before = await settingsCdp.evaluate(`
        (() => {
          const buttons = [...document.querySelectorAll('main button')];
          const history = buttons.find((item) => (item.textContent || '').includes('Browsing history'));
          const downloads = buttons.find((item) => (item.textContent || '').includes('Download history'));
          return {
            hasHistory: Boolean(history),
            hasDownloads: Boolean(downloads),
            className: history?.querySelector('div')?.className || '',
          };
        })()
      `);
      assert.equal(before.hasHistory, true);
      assert.equal(before.hasDownloads, true);
      await clickByText(settingsCdp, 'main button', 'Browsing history');

      const after = await waitFor(
        settingsCdp,
        `(() => {
          const buttons = [...document.querySelectorAll('main button')];
          const history = buttons.find((item) => (item.textContent || '').includes('Browsing history'));
          return history?.querySelector('div')?.className || '';
        })()`,
        (className) => className !== before.className,
        5000,
      );
      assert.notEqual(before.className, after);
    } finally {
      if (settingsCdp) settingsCdp.close();
      await browser.close();
    }
  });

test('clear browsing data RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let settingsCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'browser://settings');
      settingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
        15000,
      );

      await waitFor(
        settingsCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /Settings/i.test(text) && /Search Engine/i.test(text),
        15000,
      );

      const response = await settingsCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'clear-data-extra-param',
              method: 'browsingData.clear',
              params: {
                categories: ['history'],
                timeRange: 'all',
                extra: true,
              },
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
      assert.equal(parsed.id, 'clear-data-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (settingsCdp) settingsCdp.close();
      await browser.close();
    }
  });

test('settings RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let settingsCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'browser://settings');
      settingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
        15000,
      );

      await waitFor(
        settingsCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /Settings/i.test(text) && /Search Engine/i.test(text),
        15000,
      );

      const response = await settingsCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'settings-extra-param',
              method: 'settings.get',
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
      assert.equal(parsed.id, 'settings-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (settingsCdp) settingsCdp.close();
      await browser.close();
    }
  });

test('settings storage RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let settingsCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'browser://settings');
      settingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
        15000,
      );

      await waitFor(
        settingsCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /Settings/i.test(text) && /Search Engine/i.test(text),
        15000,
      );

      const response = await settingsCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'settings-storage-extra-param',
              method: 'settings.setStoragePath',
              params: {
                purpose: 'cache',
                path: '/tmp',
                extra: true,
              },
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
      assert.equal(parsed.id, 'settings-storage-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (settingsCdp) settingsCdp.close();
      await browser.close();
    }
  });

test('settings reset RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let settingsCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'browser://settings');
      settingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
        15000,
      );

      await waitFor(
        settingsCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /Settings/i.test(text) && /Search Engine/i.test(text),
        15000,
      );

      const response = await settingsCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'settings-reset-extra-param',
              method: 'settings.resetBrowserData',
              params: {
                cookies: true,
                extra: true,
              },
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
      assert.equal(parsed.id, 'settings-reset-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (settingsCdp) settingsCdp.close();
      await browser.close();
    }
  });

test('settings site usage RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let settingsCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'browser://settings');
      settingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
        15000,
      );

      await waitFor(
        settingsCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /Settings/i.test(text) && /Search Engine/i.test(text),
        15000,
      );

      const response = await settingsCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'settings-site-usage-extra-param',
              method: 'settings.siteUsageList',
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
      assert.equal(parsed.id, 'settings-site-usage-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (settingsCdp) settingsCdp.close();
      await browser.close();
    }
  });

test('settings restart RPC rejects unknown schema fields',
  { timeout: timeoutMs + 10000 },
  async () => {
    const browser = await launchDevBrowser();
    let settingsCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'browser://settings');
      settingsCdp = await browser.connectToTarget((target) =>
        /settings/i.test(target.title || '') ||
        /settings\.html/i.test(target.url || ''),
        15000,
      );

      await waitFor(
        settingsCdp,
        `typeof window.cefQuery === 'function' && document.body.innerText`,
        (text) => /Settings/i.test(text) && /Search Engine/i.test(text),
        15000,
      );

      const response = await settingsCdp.evaluate(`
        new Promise((resolve) => {
          window.cefQuery({
            request: JSON.stringify({
              id: 'settings-restart-extra-param',
              method: 'settings.restart',
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
      assert.equal(parsed.id, 'settings-restart-extra-param');
      assert.equal(parsed.ok, false);
      assert.match(parsed.error.message, /unexpected param: extra/);
    } finally {
      if (settingsCdp) settingsCdp.close();
      await browser.close();
    }
  });
