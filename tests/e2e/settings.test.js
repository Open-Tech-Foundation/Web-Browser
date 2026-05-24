import test from 'node:test';
import assert from 'node:assert/strict';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
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
