import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  sleep,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

async function resolveClickPoint(cdp, selector) {
  const rect = await cdp.evaluate(`
    (() => {
      const el = document.querySelector(${JSON.stringify(selector)});
      if (!el) return null;
      el.scrollIntoView({ block: 'center', inline: 'center' });
      const style = window.getComputedStyle(el);
      if (style.display === 'none' || style.visibility === 'hidden' ||
          style.pointerEvents === 'none') return null;
      const rect = el.getBoundingClientRect();
      return {
        x: rect.left + rect.width / 2,
        y: rect.top + rect.height / 2,
        width: rect.width,
        height: rect.height,
      };
    })()
  `);
  assert.ok(rect, `not found or not interactable: ${selector}`);
  assert.ok(rect.width > 0 && rect.height > 0, `not visible: ${selector}`);
  return rect;
}

async function clickSelectorWithMouse(cdp, selector, { button = 'left', modifiers = 0 } = {}) {
  const rect = await resolveClickPoint(cdp, selector);
  const buttons = button === 'middle' ? 4 : 1;
  const moveButtons = button === 'middle' ? 4 : 0;
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mouseMoved',
    x: rect.x,
    y: rect.y,
    buttons: moveButtons,
    modifiers,
  });
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mousePressed',
    x: rect.x,
    y: rect.y,
    button,
    buttons,
    clickCount: 1,
    modifiers,
  });
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mouseReleased',
    x: rect.x,
    y: rect.y,
    button,
    buttons: 0,
    clickCount: 1,
    modifiers,
  });
}

test('popup gating blocks window.open but allows user-intended new tabs',
  { timeout: timeoutMs + 20000 },
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
    let pageCdp = null;
    let blankTabCdp = null;
    let middleTabCdp = null;
    let ctrlTabCdp = null;
    try {
      const popupUrl = `${browser.devUrl}/popup-intent.html`;
      const targetBaseUrl = `${browser.devUrl}/popup-intent-target.html`;

      await navigateFromAddressBar(browser.cdp, popupUrl);
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(popupUrl) ||
        /Popup Intent E2E/i.test(target.title || ''),
      );
      await waitFor(
        pageCdp,
        `!!document.querySelector('#js-open') && !!document.querySelector('#plain-link') && !!document.querySelector('#self-link') && !!document.querySelector('#parent-link') && !!document.querySelector('#top-link') && !!document.querySelector('#middle-link') && !!document.querySelector('#ctrl-link')`,
        Boolean,
        15000,
      );
      await waitFor(
        pageCdp,
        `document.querySelector('#auto-result')?.textContent || ''`,
        (value) => value === 'auto blocked',
        15000,
      );

      await clickSelectorWithMouse(pageCdp, '#js-open');
      await sleep(1000);
      await waitFor(
        pageCdp,
        `document.querySelector('#result')?.textContent || ''`,
        (value) => value === 'blocked',
        15000,
      );

      await clickSelectorWithMouse(pageCdp, '#blank-link');
      blankTabCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${targetBaseUrl}?mode=blank`) ||
        /Popup Intent Target/i.test(target.title || ''),
        15000,
      );
      await waitFor(
        blankTabCdp,
        `document.title`,
        (title) => title === 'Popup Intent Target',
        15000,
      );
      blankTabCdp.close();
      blankTabCdp = null;

      await clickSelectorWithMouse(pageCdp, '#self-link');
      await waitFor(
        pageCdp,
        `location.href`,
        (href) => href.startsWith(`${targetBaseUrl}?mode=self`),
        15000,
      );
      await navigateFromAddressBar(browser.cdp, popupUrl);
      pageCdp.close();
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(popupUrl) ||
        /Popup Intent E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#parent-link')`, Boolean, 15000);

      await clickSelectorWithMouse(pageCdp, '#parent-link');
      await waitFor(
        pageCdp,
        `location.href`,
        (href) => href.startsWith(`${targetBaseUrl}?mode=parent`),
        15000,
      );
      await navigateFromAddressBar(browser.cdp, popupUrl);
      pageCdp.close();
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(popupUrl) ||
        /Popup Intent E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#top-link')`, Boolean, 15000);

      await clickSelectorWithMouse(pageCdp, '#top-link');
      await waitFor(
        pageCdp,
        `location.href`,
        (href) => href.startsWith(`${targetBaseUrl}?mode=top`),
        15000,
      );
      await navigateFromAddressBar(browser.cdp, popupUrl);
      pageCdp.close();
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(popupUrl) ||
        /Popup Intent E2E/i.test(target.title || ''),
      );
      await waitFor(pageCdp, `!!document.querySelector('#middle-link')`, Boolean, 15000);

      await clickSelectorWithMouse(pageCdp, '#middle-link', { button: 'middle' });
      middleTabCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${targetBaseUrl}?mode=middle`),
        15000,
      );
      await waitFor(
        middleTabCdp,
        `document.title`,
        (title) => title === 'Popup Intent Target',
        15000,
      );
      middleTabCdp.close();
      middleTabCdp = null;

      await clickSelectorWithMouse(pageCdp, '#ctrl-link', { modifiers: 2 });
      ctrlTabCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith(`${targetBaseUrl}?mode=ctrl`),
        15000,
      );
      await waitFor(
        ctrlTabCdp,
        `document.title`,
        (title) => title === 'Popup Intent Target',
        15000,
      );
    } finally {
      if (ctrlTabCdp) ctrlTabCdp.close();
      if (middleTabCdp) middleTabCdp.close();
      if (blankTabCdp) blankTabCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
    }
  });
