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

      const clickedAppearance = await settingsCdp.evaluate(`
        (() => {
          const button = [...document.querySelectorAll('aside nav button')]
            .find((item) => (item.textContent || '').includes('Appearance'));
          if (!button) return false;
          button.click();
          return true;
        })()
      `);
      assert.equal(clickedAppearance, true);
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
