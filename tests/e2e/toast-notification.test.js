import test from 'node:test';
import assert from 'node:assert/strict';

import {
  connectToReadyTarget,
  launchDevBrowser,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('toast notification renders pill with tick icon and message',
  { timeout: timeoutMs + 20000 },
  async () => {
    const browser = await launchDevBrowser();
    let toastCdp = null;
    try {
      // Wait for the toast overlay browser to load and React to mount.
      toastCdp = await connectToReadyTarget(
        (target) =>
          (target.url || '').endsWith('/toast.html') ||
          /toast/i.test(target.url || ''),
        `typeof window.__otfSetToastMessage === 'function'`,
        Boolean,
        15000,
      );

      // Set a message via the same path C++ uses, then wait for React
      // to re-render the pill.
      await toastCdp.evaluate(`window.__otfSetToastMessage('Link copied')`);
      const pillText = await waitFor(
        toastCdp,
        `document.querySelector('span')?.textContent || ''`,
        (text) => text === 'Link copied',
        5000,
      );
      assert.equal(pillText, 'Link copied');

      // Verify the green tick SVG is rendered.
      const hasTick = await toastCdp.evaluate(`!!document.querySelector('svg')`);
      assert.equal(hasTick, true);

      // Clear and verify the pill disappears.
      await toastCdp.evaluate(`window.__otfSetToastMessage('')`);
      const gone = await waitFor(
        toastCdp,
        `document.querySelector('span')?.textContent || ''`,
        (text) => text === '',
        5000,
      );
      assert.equal(gone, '');
    } finally {
      if (toastCdp) toastCdp.close();
      await browser.close();
    }
  });
