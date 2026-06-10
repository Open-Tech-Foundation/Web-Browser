import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';

import {
  addressBarSelector,
  clickByText,
  clickSelector,
  connectToReadyTarget,
  launchDevBrowser,
  navigateFromAddressBar,
  startStaticServer,
  timeoutMs,
  waitFor,
} from './helpers/browserHarness.js';
import { setSitePermissionFromUi } from './helpers/e2eUtils.js';

const FIXTURE_SVG = Buffer.from(`<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <rect width="16" height="16" fill="#ff0000"/>
</svg>`);

let serial = Promise.resolve();
function serialTest(name, options, fn) {
  test(name, options, () => {
    const run = serial.then(fn);
    serial = run.catch(() => {});
    return run;
  });
}

const AVIF_CACHE_PATH = '/tmp/otf-browser-avif-cs-gray-7f7f7f.avif';
const AVIF_REMOTE_URL = 'https://raw.githubusercontent.com/PixarAnimationStudios/OpenUSD/dev/pxr/imaging/plugin/hioAvif/testenv/testHioAvif/cs-gray-7f7f7f.avif';

const BASE_IMAGE_FIXTURES = [
  {
    name: 'image-preview-png.png',
    format: 'PNG',
    mime: 'image/png',
    body: Buffer.from('iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAHUlEQVR4nGP8z8Dwn4ECwESJ5lEDRg0YNWAwGQAAWG0CHvXMz6IAAAAASUVORK5CYII=', 'base64'),
  },
  {
    name: 'image-preview-jpg.jpg',
    format: 'JPG',
    mime: 'image/jpeg',
    body: Buffer.from('/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAAQABADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDi6KKK+ZP3E//Z', 'base64'),
  },
  {
    name: 'image-preview-gif.gif',
    format: 'GIF',
    mime: 'image/gif',
    body: Buffer.from('R0lGODdhEAAQAIEAAP8AAAAAAAAAAAAAACwAAAAAEAAQAEAIHQABCBxIsKDBgwgTKlzIsKHDhxAjSpxIsaLFgQEBADs=', 'base64'),
  },
  {
    name: 'image-preview-webp.webp',
    format: 'WEBP',
    mime: 'image/webp',
    body: Buffer.from('UklGRjwAAABXRUJQVlA4IDAAAADQAQCdASoQABAAAUAmJaACdLoB+AADsAD+8ut//NgVzXPv9//S4P0uD9Lg/9KQAAA=', 'base64'),
  },
  {
    name: 'image-preview-bmp.bmp',
    format: 'BMP',
    mime: 'image/bmp',
    body: Buffer.from('Qk02AwAAAAAAADYAAAAoAAAAEAAAABAAAAABABgAAAAAAAADAADEDgAAxA4AAAAAAAAAAAAAAAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/', 'base64'),
  },
  {
    name: 'image-preview-ico.ico',
    format: 'ICO',
    mime: 'image/x-icon',
    body: Buffer.from('AAABAAEAEBAAAAAAIABWAAAAFgAAAIlQTkcNChoKAAAADUlIRFIAAAAQAAAAEAgGAAAAH/P/YQAAAB1JREFUeJxj/M/A8J+BAsBEieZRA0YNGDVgMBkAAFhtAh71zM+iAAAAAElFTkSuQmCC', 'base64'),
  },
  {
    name: 'image-preview-svg.svg',
    format: 'SVG',
    mime: 'image/svg+xml',
    body: FIXTURE_SVG,
  },
  {
    name: 'image-preview-avif.avif',
    format: 'AVIF',
    mime: 'image/avif',
    body: Buffer.from('AAAAFGZ0eXBhdmlmAAAAAG1pZjEAAACgbWV0YQAAAAAAAAAOcGl0bQAAAAAAAQAAAB5pbG9jAAAAAEQAAAEAAQAAAAEAAAC8AAAAGwAAACNpaW5mAAAAAAABAAAAFWluZmUCAAAAAAEAAGF2MDEAAAAARWlwcnAAAAAoaXBjbwAAABRpc3BlAAAAAAAAAAQAAAAEAAAADGF2MUOBAAAAAAAAFWlwbWEAAAAAAAAAAQABAgECAAAAI21kYXQSAAoIP8R8hAQ0BUAyDWeeUy0JG+QAACANEkA=', 'base64'),
  },
  {
    name: 'image-preview-tiff-single.tif',
    format: 'TIFF',
    mime: 'image/tiff',
    body: Buffer.from('SUkqABoAAAB4nPvPwPB/FI2ikYoAGAD/AQAKAAABAwABAAAAEAAAAAEBAwABAAAAEAAAAAIBAwADAAAAmAAAAAMBAwABAAAACAAAAAYBAwABAAAAAgAAABEBBAABAAAACAAAABUBAwABAAAAAwAAABYBAwABAAAAEAAAABcBBAABAAAAEQAAABwBAwABAAAAAQAAAAAAAAAIAAgACAA=', 'base64'),
  },
  {
    name: 'image-preview-tiff-multi.tiff',
    format: 'TIFF',
    mime: 'image/tiff',
    body: Buffer.from('SUkqAAgBAAAoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoDAD+AAQAAQAAAAIAAAAAAQMAAQAAABAAAAABAQMAAQAAABAAAAACAQMAAQAAAAgAAAADAQMAAQAAAAEAAAAGAQMAAQAAAAEAAAARAQQAAQAAAAgAAAASAQMAAQAAAAEAAAAVAQMAAQAAAAEAAAAXAQQAAQAAAAABAAAcAQMAAQAAAAEAAAApAQMAAgAAAAAAAgCeAgAA3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3AwA/gAEAAEAAAACAAAAAAEDAAEAAAAQAAAAAQEDAAEAAAAQAAAAAgEDAAEAAAAIAAAAAwEDAAEAAAABAAAABgEDAAEAAAABAAAAEQEEAAEAAACeAQAAEgEDAAEAAAABAAAAFQEDAAEAAAABAAAAFwEEAAEAAAAAAQAAHAEDAAEAAAABAAAAKQEDAAIAAAABAAIAAAAAAA==', 'base64'),
  },
];

const previewStateExpression = `(() => {
  const i = document.querySelector('img[alt="Preview"]');
  return {
    text: document.body?.innerText || '',
    complete: !!i?.complete,
    w: i?.naturalWidth || 0,
    h: i?.naturalHeight || 0,
  };
})()`;

const fixtureLinkId = (fixture) => `#dl-${fixture.name.replace(/[^a-z0-9]+/gi, '-')}`;

async function clickFixtureDownload(pageCdp, fixture) {
  const selector = fixtureLinkId(fixture);
  await waitFor(pageCdp, `!!document.querySelector(${JSON.stringify(selector)})`, Boolean, 15000);
  await clickSelector(pageCdp, selector);
}

async function openDownloadedPreview(downloadsCdp) {
  await waitFor(
    downloadsCdp,
    `(() => [...document.querySelectorAll('button')]
      .some((button) => (button.textContent || '').trim() === 'Open'))()`,
    Boolean,
    30000,
  );
  await clickByText(downloadsCdp, 'button', 'Open');
}

async function connectFixturePage(browser, server) {
  const originWithoutScheme = server.origin.replace(/^https?:\/\//, '');
  await waitFor(
    browser.cdp,
    `document.querySelector(${JSON.stringify(addressBarSelector)})?.value || ""`,
    (value) => value.startsWith(server.origin) || value.startsWith(originWithoutScheme),
    15000,
  );
  return browser.connectToTarget((t) => (t.url || '').startsWith(server.origin), 15000);
}

async function openFixturePage(browser, server) {
  await setSitePermissionFromUi(browser, server.origin, 'downloads', 'allow');
  await navigateFromAddressBar(browser.cdp, server.origin);
  return connectFixturePage(browser, server);
}

async function connectRenderedPreview(browser, format, deadlineMs = 20000) {
  return connectToReadyTarget(
    (t) =>
      (t.url || '').includes('imagepreview.html') ||
      (t.url || '').startsWith('browser://image-preview/'),
    previewStateExpression,
    (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes(`Format: ${format}`),
    deadlineMs,
  );
}

function imageFixtureServer(fixtures) {
  return (req, res) => {
    if (req.url === '/favicon.ico') {
      res.writeHead(204);
      res.end();
      return;
    }
    const match = fixtures.find((f) => req.url === `/${f.name}`);
    if (match) {
      res.writeHead(200, {
        'content-type': match.mime,
        'content-disposition': `attachment; filename="${match.name}"`,
        'content-length': match.body.length,
      });
      res.end(match.body);
      return;
    }
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
    res.end(`<!doctype html>
      <html>
        <head><title>Image Preview Test</title></head>
        <body>
          <main>
            <ul>
              ${fixtures.map((f) => `<li><a id="dl-${f.name.replace(/[^a-z0-9]+/gi, '-')}" href="/${f.name}">${f.name}</a></li>`).join('')}
            </ul>
          </main>
        </body>
      </html>`);
  };
}

serialTest('image preview opens PNG from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-png.png');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: PNG'),
        20000,
      );
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens PNG from downloads with packaged browser scheme',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-png.png');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({
      productionUi: true,
      settings: { downloadsEnabled: true },
    });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      const previewUrl = await previewCdp.evaluate('location.href');
      assert.equal(previewUrl.startsWith('browser://image-preview/'), true);
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens JPG from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-jpg.jpg');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: JPG'),
        20000,
      );
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens GIF from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-gif.gif');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: GIF'),
        20000,
      );
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens WEBP from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-webp.webp');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: WEBP'),
        20000,
      );
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens BMP from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-bmp.bmp');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: BMP'),
        20000,
      );
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens ICO from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-ico.ico');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: ICO'),
        20000,
      );
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens SVG from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-svg.svg');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: SVG'),
        20000,
      );
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens AVIF from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    let avifBody;
    try {
      avifBody = await readFile(AVIF_CACHE_PATH);
    } catch {
      const resp = await fetch(AVIF_REMOTE_URL);
      assert.equal(resp.ok, true);
      avifBody = Buffer.from(await resp.arrayBuffer());
      await writeFile(AVIF_CACHE_PATH, avifBody);
    }
    const fixture = { ...BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-avif.avif'), body: avifBody };
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: AVIF'),
        20000,
      );
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens single-page TIFF from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-tiff-single.tif');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: TIFF'),
        20000,
      );
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('image preview opens multi-page TIFF and navigates pages',
  { timeout: timeoutMs + 35000 },
  async () => {
    const fixture = BASE_IMAGE_FIXTURES.find((f) => f.name === 'image-preview-tiff-multi.tiff');
    const server = await startStaticServer(imageFixtureServer([fixture]));
    const browser = await launchDevBrowser({ settings: { downloadsEnabled: true } });
    let pageCdp = null;
    let downloadsCdp = null;
    let previewCdp = null;
    try {
      pageCdp = await openFixturePage(browser, server);
      await clickFixtureDownload(pageCdp, fixture);
      await navigateFromAddressBar(browser.cdp, 'browser://downloads');
      downloadsCdp = await browser.connectToTarget((t) =>
        (t.title || '') === 'Downloads' || /downloads\.html/i.test(t.url || ''),
      );
      await waitFor(downloadsCdp, 'document.body?.innerText || ""', (t) => t.includes(fixture.name), 30000);
      await openDownloadedPreview(downloadsCdp);
      previewCdp = await connectRenderedPreview(browser, fixture.format);
      await waitFor(
        previewCdp,
        `(() => { const i = document.querySelector('img[alt="Preview"]'); return { text: document.body?.innerText || '', complete: !!i?.complete, w: i?.naturalWidth || 0, h: i?.naturalHeight || 0 }; })()`,
        (s) => s.complete && s.w > 0 && s.h > 0 && s.text.includes('Format: TIFF'),
        20000,
      );
      await waitFor(previewCdp, 'document.body?.innerText || ""', (t) => t.includes('Pages: 2') && t.includes('Current Page: 1'), 15000);
      await previewCdp.evaluate(`document.querySelector('button[title^="Next Page"]')?.click()`);
      await waitFor(previewCdp, 'document.body?.innerText || ""', (t) => t.includes('Current Page: 2'), 15000);
      await previewCdp.evaluate(`document.querySelector('button[title^="Previous Page"]')?.click()`);
      await waitFor(previewCdp, 'document.body?.innerText || ""', (t) => t.includes('Current Page: 1'), 15000);
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });
