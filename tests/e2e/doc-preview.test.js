import test from 'node:test';
import assert from 'node:assert/strict';

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

// Minimal valid PDF (1 page, 1x1 white pixel)
const FIXTURE_PDF = Buffer.from(
  '%PDF-1.0\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n' +
  '2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n' +
  '3 0 obj<</Type/Page/MediaBox[0 0 72 72]/Parent 2 0 R/Resources<<>>>>endobj\n' +
  'xref\n0 4\n0000000000 65535 f \n0000000009 00000 n \n0000000058 00000 n \n0000000115 00000 n \n' +
  'trailer<</Size 4/Root 1 0 R>>\nstartxref\n190\n%%EOF',
  'ascii',
);

const FIXTURE_JSON = Buffer.from('{"key": "value", "number": 42}', 'utf-8');

const FIXTURE_JSON5 = Buffer.from('{\n  key: "value",\n  number: 42,\n  // comment\n  nested: {\n    flag: true,\n  },\n}', 'utf-8');

const FIXTURE_JSONC = Buffer.from('{\n  "key": "value",\n  // comment\n  "number": 42\n}', 'utf-8');

const FIXTURE_CSV = Buffer.from('Name,Age,City\nAlice,30,New York\nBob,25,Los Angeles\nCharlie,35,Chicago\n"Doe, Jane",28,Miami\n', 'utf-8');

const DOC_FIXTURES = [
  {
    name: 'doc-preview-test.pdf',
    format: 'PDF',
    mime: 'application/pdf',
    body: FIXTURE_PDF,
  },
  {
    name: 'doc-preview-test.json',
    format: 'JSON',
    mime: 'application/json',
    body: FIXTURE_JSON,
  },
  {
    name: 'doc-preview-test.json5',
    format: 'JSON5',
    mime: 'application/json5',
    body: FIXTURE_JSON5,
  },
  {
    name: 'doc-preview-test.jsonc',
    format: 'JSONC',
    mime: 'application/json',
    body: FIXTURE_JSONC,
  },
  {
    name: 'doc-preview-test.csv',
    format: 'CSV',
    mime: 'text/csv',
    body: FIXTURE_CSV,
  },
];

let serial = Promise.resolve();
function serialTest(name, options, fn) {
  test(name, options, () => {
    const run = serial.then(fn);
    serial = run.catch(() => {});
    return run;
  });
}

const docPreviewStateExpression = `(() => {
  const text = document.body?.innerText || '';
  const iframe = document.querySelector('iframe');
  const monaco = document.querySelector('.monaco-editor');
  const embed = document.querySelector('embed[type="application/pdf"]');
  const object = document.querySelector('object[type="application/pdf"]');
  return {
    text,
    hasIframe: !!iframe,
    hasMonaco: !!monaco,
    hasPdfViewer: !!embed || !!object || text.includes('%PDF'),
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

function docFixtureServer(fixtures) {
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
        <head><title>Doc Preview Test</title></head>
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

serialTest('doc preview opens PDF from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = DOC_FIXTURES.find((f) => f.name === 'doc-preview-test.pdf');
    const server = await startStaticServer(docFixtureServer([fixture]));
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

      // For PDFs, the page navigates to the content URL and CEF's PDF viewer takes over.
      // Wait for a new tab/page to appear with the PDF content URL.
      await new Promise((r) => setTimeout(r, 3000));

      // Check all targets for one that loaded the PDF content
      const targets = await browser.cdp.send('Target.getTargets');
      const pdfTarget = (targets.targetInfos || []).find((t) =>
        (t.url || '').includes('doc-preview/content/') ||
        (t.url || '').includes('docpreview')
      );
      console.log('[DOC-PREVIEW E2E] PDF target:', JSON.stringify(pdfTarget ? { url: pdfTarget.url, type: pdfTarget.type } : null));

      // Also check the downloads page still works
      const dlState = await downloadsCdp.evaluate('document.body?.innerText || ""');
      console.log('[DOC-PREVIEW E2E] Downloads page still alive:', dlState.includes('doc-preview-test.pdf'));

      const pdfLoaded = pdfTarget && (
        pdfTarget.url.includes('doc-preview/content/') ||
        pdfTarget.url.includes('docpreview')
      );
      assert.ok(pdfLoaded,
        `PDF should be loaded, got targets: ${JSON.stringify((targets.targetInfos || []).map(t => t.url).slice(0, 10))}`);
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('doc preview opens JSON from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = DOC_FIXTURES.find((f) => f.name === 'doc-preview-test.json');
    const server = await startStaticServer(docFixtureServer([fixture]));
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

      // Connect to the doc preview tab
      previewCdp = await connectToReadyTarget(
        (t) =>
          (t.url || '').includes('docpreview.html') ||
          (t.url || '').startsWith('browser://doc-preview/'),
        docPreviewStateExpression,
        (s) => s.text.includes('JSON') || s.hasMonaco || s.text.includes('key'),
        20000,
      );

      // Verify the preview loaded with Monaco editor
      const state = await previewCdp.evaluate(docPreviewStateExpression);
      console.log('[DOC-PREVIEW E2E] Preview state:', JSON.stringify(state));

      // JSON should be rendered in Monaco editor
      assert.ok(state.hasMonaco || state.text.includes('key'),
        `JSON preview should show Monaco editor, got: ${JSON.stringify(state)}`);
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('doc preview opens JSON5 from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = DOC_FIXTURES.find((f) => f.name === 'doc-preview-test.json5');
    const server = await startStaticServer(docFixtureServer([fixture]));
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

      previewCdp = await connectToReadyTarget(
        (t) =>
          (t.url || '').includes('docpreview.html') ||
          (t.url || '').startsWith('browser://doc-preview/'),
        docPreviewStateExpression,
        (s) => s.text.includes('key') || s.hasMonaco,
        20000,
      );

      const state = await previewCdp.evaluate(docPreviewStateExpression);
      console.log('[DOC-PREVIEW E2E] JSON5 preview state:', JSON.stringify(state));

      assert.ok(state.hasMonaco || state.text.includes('key'),
        `JSON5 preview should show Monaco editor, got: ${JSON.stringify(state)}`);
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('doc preview opens JSONC from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = DOC_FIXTURES.find((f) => f.name === 'doc-preview-test.jsonc');
    const server = await startStaticServer(docFixtureServer([fixture]));
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

      previewCdp = await connectToReadyTarget(
        (t) =>
          (t.url || '').includes('docpreview.html') ||
          (t.url || '').startsWith('browser://doc-preview/'),
        docPreviewStateExpression,
        (s) => s.text.includes('key') || s.hasMonaco,
        20000,
      );

      const state = await previewCdp.evaluate(docPreviewStateExpression);
      console.log('[DOC-PREVIEW E2E] JSONC preview state:', JSON.stringify(state));

      assert.ok(state.hasMonaco || state.text.includes('key'),
        `JSONC preview should show Monaco editor, got: ${JSON.stringify(state)}`);
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });

serialTest('doc preview opens CSV from downloads',
  { timeout: timeoutMs + 30000 },
  async () => {
    const fixture = DOC_FIXTURES.find((f) => f.name === 'doc-preview-test.csv');
    const server = await startStaticServer(docFixtureServer([fixture]));
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

      // Connect to the doc preview tab
      previewCdp = await connectToReadyTarget(
        (t) =>
          (t.url || '').includes('docpreview.html') ||
          (t.url || '').startsWith('browser://doc-preview/'),
        docPreviewStateExpression,
        (s) => s.text.includes('NAME') && s.text.includes('Alice'),
        20000,
      );

      // Verify the CSV table rendered
      const state = await previewCdp.evaluate(docPreviewStateExpression);
      console.log('[DOC-PREVIEW E2E] CSV preview state:', JSON.stringify(state));

      assert.ok(state.text.includes('NAME') && state.text.includes('Alice') && state.text.includes('New York'),
        `CSV preview should show table with headers and data, got: ${JSON.stringify(state)}`);
    } finally {
      if (previewCdp) previewCdp.close();
      if (downloadsCdp) downloadsCdp.close();
      if (pageCdp) pageCdp.close();
      await browser.close();
      await server.close();
    }
  });
