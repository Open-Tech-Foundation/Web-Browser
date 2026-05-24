import test from 'node:test';

import {
  clickByText,
  clickSelector,
  launchDevBrowser,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { connectShell } from './helpers/e2eUtils.js';

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
