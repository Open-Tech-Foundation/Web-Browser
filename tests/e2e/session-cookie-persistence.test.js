import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

import {
  launchDevBrowser,
  navigateFromAddressBar,
  sleep,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';

test('HttpOnly session cookies persist across browser restart',
  { timeout: timeoutMs + 45000, concurrency: false },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-session-cookie-'));
    const unique = Date.now();
    const cookieName = `otf_http_only_session_${unique}`;
    const cookieValue = `secret-${unique}`;
    const checkCookie = (header = '') =>
      header.split(/;\s*/).includes(`${cookieName}=${cookieValue}`);
    const checkRequests = [];

    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      if (req.url === '/set') {
        res.writeHead(200, {
          'content-type': 'text/html; charset=utf-8',
          'cache-control': 'no-store',
          'set-cookie': `${cookieName}=${cookieValue}; Path=/; HttpOnly; SameSite=Lax`,
        });
        res.end(`<!doctype html>
          <title>set httponly session cookie</title>
          <main>set httponly session cookie</main>`);
        return;
      }

      if (req.url === '/check') {
        const cookieHeader = req.headers.cookie || '';
        const present = checkCookie(cookieHeader);
        checkRequests.push({ cookieHeader, present });
        res.writeHead(200, {
          'content-type': 'text/html; charset=utf-8',
          'cache-control': 'no-store',
        });
        res.end(`<!doctype html>
          <title>httponly session cookie ${present ? 'present' : 'missing'}</title>
          <main id="result">${present ? 'present' : 'missing'}</main>`);
        return;
      }

      res.writeHead(404);
      res.end('not found');
    });

    const settings = {
      searchEngine: 'google',
      historyEnabled: true,
      downloadsEnabled: true,
      startupBehavior: 'newtab',
      startupUrls: [],
      httpsOnly: false,
      blockInsecure: false,
      appearanceMode: 'auto',
    };

    let browser = null;
    let pageCdp = null;
    try {
      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      await navigateFromAddressBar(browser.cdp, `${server.origin}/set`);
      pageCdp = await browser.connectToTarget(
        (target) => (target.url || '').startsWith(`${server.origin}/set`),
        15000,
      );
      await waitFor(pageCdp, 'document.readyState', (state) => state === 'complete', 15000);
      assert.equal(
        await pageCdp.evaluate(`document.cookie.includes(${JSON.stringify(cookieName)})`),
        false,
        'HttpOnly cookie should not be visible through document.cookie',
      );
      pageCdp.close();
      pageCdp = null;

      await navigateFromAddressBar(browser.cdp, `${server.origin}/check`);
      await browser.connectToTarget(
        (target) => /httponly session cookie present/i.test(target.title || ''),
        15000,
      ).then((cdp) => {
        cdp.close();
      });
      assert.equal(checkRequests.at(-1)?.present, true, 'cookie should be sent before restart');

      await browser.close();
      browser = null;
      await sleep(1000);

      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      await navigateFromAddressBar(browser.cdp, `${server.origin}/check`);
      await browser.connectToTarget(
        (target) => /httponly session cookie present/i.test(target.title || ''),
        15000,
      ).then((cdp) => {
        cdp.close();
      });

      assert.equal(checkRequests.at(-1)?.present, true, 'cookie should be sent after restart');
      assert.ok(
        checkRequests.length >= 2,
        `expected pre- and post-restart checks, got ${checkRequests.length}`,
      );
    } finally {
      if (pageCdp) {
        pageCdp.close();
      }
      if (browser) {
        await browser.close();
      }
      await server.close();
      await rm(profileRoot, { recursive: true, force: true });
    }
  });

test('capped HttpOnly cookies persist across browser restart',
  { timeout: timeoutMs + 45000, concurrency: false },
  async () => {
    const profileRoot = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-capped-cookie-'));
    const unique = Date.now();
    const cookieName = `otf_http_only_capped_${unique}`;
    const cookieValue = `secret-${unique}`;
    const checkCookie = (header = '') =>
      header.split(/;\s*/).includes(`${cookieName}=${cookieValue}`);
    const checkRequests = [];

    const server = await startStaticServer((req, res) => {
      if (req.url === '/favicon.ico') {
        res.writeHead(204);
        res.end();
        return;
      }

      if (req.url === '/set') {
        res.writeHead(200, {
          'content-type': 'text/html; charset=utf-8',
          'cache-control': 'no-store',
          'set-cookie': `${cookieName}=${cookieValue}; Path=/; Max-Age=31536000; HttpOnly; SameSite=Lax`,
        });
        res.end(`<!doctype html>
          <title>set capped httponly cookie</title>
          <main>set capped httponly cookie</main>`);
        return;
      }

      if (req.url === '/check') {
        const cookieHeader = req.headers.cookie || '';
        const present = checkCookie(cookieHeader);
        checkRequests.push({ cookieHeader, present });
        res.writeHead(200, {
          'content-type': 'text/html; charset=utf-8',
          'cache-control': 'no-store',
        });
        res.end(`<!doctype html>
          <title>capped httponly cookie ${present ? 'present' : 'missing'}</title>
          <main id="result">${present ? 'present' : 'missing'}</main>`);
        return;
      }

      res.writeHead(404);
      res.end('not found');
    });

    const settings = {
      searchEngine: 'google',
      historyEnabled: true,
      downloadsEnabled: true,
      startupBehavior: 'newtab',
      startupUrls: [],
      httpsOnly: false,
      blockInsecure: false,
      appearanceMode: 'auto',
    };

    let browser = null;
    let pageCdp = null;
    try {
      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      await navigateFromAddressBar(browser.cdp, `${server.origin}/set`);
      pageCdp = await browser.connectToTarget(
        (target) => (target.url || '').startsWith(`${server.origin}/set`),
        15000,
      );
      await waitFor(pageCdp, 'document.readyState', (state) => state === 'complete', 15000);
      assert.equal(
        await pageCdp.evaluate(`document.cookie.includes(${JSON.stringify(cookieName)})`),
        false,
        'HttpOnly cookie should not be visible through document.cookie',
      );
      pageCdp.close();
      pageCdp = null;

      await navigateFromAddressBar(browser.cdp, `${server.origin}/check`);
      await browser.connectToTarget(
        (target) => /capped httponly cookie present/i.test(target.title || ''),
        15000,
      ).then((cdp) => {
        cdp.close();
      });
      assert.equal(checkRequests.at(-1)?.present, true, 'cookie should be sent before restart');

      await browser.close();
      browser = null;
      await sleep(1000);

      browser = await launchDevBrowser({ profileRoot, preserveProfile: true, settings });
      await navigateFromAddressBar(browser.cdp, `${server.origin}/check`);
      await browser.connectToTarget(
        (target) => /capped httponly cookie present/i.test(target.title || ''),
        15000,
      ).then((cdp) => {
        cdp.close();
      });

      assert.equal(checkRequests.at(-1)?.present, true, 'capped cookie should be sent after restart');
    } finally {
      if (pageCdp) {
        pageCdp.close();
      }
      if (browser) {
        await browser.close();
      }
      await server.close();
      await rm(profileRoot, { recursive: true, force: true });
    }
  });
