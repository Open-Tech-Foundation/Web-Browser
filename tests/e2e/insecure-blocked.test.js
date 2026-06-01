import test from 'node:test';
import assert from 'node:assert/strict';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

const addressSelector = 'input[placeholder="Search or enter address..."]';

test('HTTP→HTTPS upgrade: site with HTTPS loads successfully over HTTPS',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'http://example.com');

      // example.com supports HTTPS — the browser should upgrade and load it.
      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith('https://example.com'),
        20000,
      );

      const title = await waitFor(
        pageCdp,
        'document.title',
        (t) => t.length > 0,
        15000,
      );

      assert.ok(
        title.toLowerCase().includes('example'),
        `expected Example Domain page, got title: ${title}`,
      );
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
    }
  });

test('HTTP→HTTPS upgrade failure: insecure-blocked page shown when HTTPS is unreachable',
  { timeout: timeoutMs + 15000 },
  async () => {
    // Start an HTTP-only server on a random port so we have a known URL
    // that does NOT support HTTPS.  We use a non-localhost hostname
    // (resolved via /etc/hosts or the test runner) by navigating to the
    // raw IP — but 127.0.0.1 is allowlisted for HTTP.  Instead, use a
    // guaranteed-unreachable HTTPS host: the HTTPS upgrade to a
    // non-existent domain will fail with a connection error.
    const browser = await launchDevBrowser();
    let blockedCdp = null;
    try {
      // Use a non-existent domain that will fail HTTPS connection.
      await navigateFromAddressBar(
        browser.cdp,
        'http://this-domain-does-not-exist-12345.example',
      );

      blockedCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith('browser://insecure-blocked') ||
        /insecure-blocked/i.test(target.url || ''),
        25000,
      );

      const visible = await waitFor(
        blockedCdp,
        `(() => {
          const text = document.body?.innerText || '';
          const blocked = [...document.querySelectorAll('div')].map((item) => item.textContent || '').join(' ');
          return { text, blocked };
        })()`,
        (value) =>
          value.text.includes('This site is not secure') &&
          value.blocked.includes('http://this-domain-does-not-exist-12345.example'),
        15000,
      );

      assert.ok(visible.text.includes('This site is not secure'));
      assert.ok(visible.blocked.includes('http://this-domain-does-not-exist-12345.example'));
    } finally {
      if (blockedCdp) blockedCdp.close();
      await browser.close();
    }
  });

test('address bar shows HTTPS URL after upgrade from HTTP',
  { timeout: timeoutMs + 15000 },
  async () => {
    const browser = await launchDevBrowser();
    let pageCdp = null;
    try {
      await navigateFromAddressBar(browser.cdp, 'http://example.com');

      pageCdp = await browser.connectToTarget((target) =>
        (target.url || '').startsWith('https://example.com'),
        20000,
      );

      const address = await waitFor(
        browser.cdp,
        `document.querySelector(${JSON.stringify(addressSelector)})?.value || ""`,
        (value) => value.includes('example.com'),
        15000,
      );

      assert.ok(
        address.includes('example.com'),
        `expected address bar to show example.com, got: ${address}`,
      );
    } finally {
      if (pageCdp) pageCdp.close();
      await browser.close();
    }
  });
