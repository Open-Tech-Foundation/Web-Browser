// Realm-coverage probe.
//
// Spawns the main fingerprinting-relevant execution contexts a page can
// create, runs a single shared probe in each, and compares the results
// against the main-page baseline. The point is to discover whether the
// page-policy injection reaches every realm a fingerprinter might pull
// fresh APIs from — workers (the #1 bypass) and synthetic iframes
// (about:blank / srcdoc / blob / data) being the high-risk ones.
//
// Result: a context × property grid rendered inside the expanded row.
// pagePolicyInjected=false in any context is the smoking-gun signal.

const TIMEOUT_MS = 4000;

// The probe runs in every context. Must be standalone, must tolerate
// missing globals (no navigator in some sandboxes, no screen in workers).
// Returns a flat dict of values that the harness compares cell-by-cell.
const PROBE_BODY = `
  const probe = () => {
    const r = {};
    const safe = (fn) => { try { return fn(); } catch (_) { return '<throw>'; } };
    const nav = typeof navigator !== 'undefined' ? navigator : null;
    r.hasNavigator = !!nav;
    if (nav) {
      r.userAgent = safe(() => String(nav.userAgent));
      r.platform = safe(() => String(nav.platform));
      r.language = safe(() => String(nav.language));
      r.languages = safe(() => Array.isArray(nav.languages) ? nav.languages.join(',') : String(nav.languages));
      r.hardwareConcurrency = safe(() => nav.hardwareConcurrency);
      r.deviceMemory = safe(() => nav.deviceMemory);
      r.maxTouchPoints = safe(() => nav.maxTouchPoints);
      r.pdfViewerEnabled = safe(() => String(nav.pdfViewerEnabled));
      r.hasGetBattery = ('getBattery' in nav);
      r.hasConnection = ('connection' in nav);
      r.hasKeyboard = ('keyboard' in nav);
      r.pluginsLen = safe(() => nav.plugins ? nav.plugins.length : null);
      r.mimeTypesLen = safe(() => nav.mimeTypes ? nav.mimeTypes.length : null);
    }
    const hasDOM = typeof document !== 'undefined' && typeof screen !== 'undefined';
    if (hasDOM) {
      r.screenWidth = safe(() => screen.width);
      r.screenHeight = safe(() => screen.height);
      r.colorDepth = safe(() => screen.colorDepth);
      r.devicePixelRatio = safe(() => self.devicePixelRatio);
    }
    r.typeofBatteryManager = (typeof BatteryManager);
    r.typeofNetworkInformation = (typeof NetworkInformation);
    r.typeofKeyboard = (typeof Keyboard);
    r.pagePolicyInjected = !!globalThis.__otfPagePolicyInjected;
    return r;
  };
`;

// Property descriptors — ordered as they should appear in the grid.
// `kind: 'dom'` means the property is only meaningful in DOM contexts;
// workers are expected to report N/A and that's not a mismatch.
const PROPERTIES = [
  { key: 'pagePolicyInjected',       label: 'page policy injected',    kind: 'all' },
  { key: 'platform',                 label: 'navigator.platform',      kind: 'all' },
  { key: 'userAgent',                label: 'navigator.userAgent',     kind: 'all', long: true },
  { key: 'language',                 label: 'navigator.language',      kind: 'all' },
  { key: 'languages',                label: 'navigator.languages',     kind: 'all' },
  { key: 'hardwareConcurrency',      label: 'hardwareConcurrency',     kind: 'all' },
  { key: 'deviceMemory',             label: 'deviceMemory',            kind: 'all' },
  { key: 'maxTouchPoints',           label: 'maxTouchPoints',          kind: 'all' },
  { key: 'pdfViewerEnabled',         label: 'pdfViewerEnabled',        kind: 'dom' },
  { key: 'hasGetBattery',            label: "'getBattery' in nav",     kind: 'all' },
  { key: 'hasConnection',            label: "'connection' in nav",     kind: 'all' },
  { key: 'hasKeyboard',              label: "'keyboard' in nav",       kind: 'all' },
  { key: 'typeofBatteryManager',     label: 'typeof BatteryManager',   kind: 'all' },
  { key: 'typeofNetworkInformation', label: 'typeof NetworkInformation', kind: 'all' },
  { key: 'typeofKeyboard',           label: 'typeof Keyboard',         kind: 'all' },
  { key: 'pluginsLen',               label: 'plugins.length',          kind: 'dom' },
  { key: 'mimeTypesLen',             label: 'mimeTypes.length',        kind: 'dom' },
  { key: 'screenWidth',              label: 'screen.width',            kind: 'dom' },
  { key: 'screenHeight',             label: 'screen.height',           kind: 'dom' },
  { key: 'colorDepth',               label: 'screen.colorDepth',       kind: 'dom' },
  { key: 'devicePixelRatio',         label: 'devicePixelRatio',        kind: 'dom' },
];

const CONTEXTS = [
  { id: 'main',            label: 'main',         kind: 'dom' },
  { id: 'iframe-srcdoc',   label: 'iframe srcdoc', kind: 'dom' },
  { id: 'iframe-blank',    label: 'iframe blank', kind: 'dom' },
  { id: 'iframe-blob',     label: 'iframe blob',  kind: 'dom' },
  { id: 'iframe-data',     label: 'iframe data',  kind: 'dom' },
  { id: 'worker',          label: 'worker',       kind: 'worker' },
  { id: 'shared-worker',   label: 'sharedworker', kind: 'worker' },
  { id: 'nested-worker',   label: 'nested worker', kind: 'worker' },
];

// ── Spawners — one per context. Each returns a Promise<result|null>. ────
const withTimeout = (promise) => Promise.race([
  promise,
  new Promise((resolve) => setTimeout(() => resolve(null), TIMEOUT_MS)),
]);

const probeMain = async () => {
  // eslint-disable-next-line no-new-func
  const fn = new Function(PROBE_BODY + '\nreturn probe();');
  try { return fn(); } catch (_) { return null; }
};

const probeIframe = (configure) => withTimeout(new Promise((resolve) => {
  const token = 'realm-' + Math.random().toString(36).slice(2);
  const iframe = document.createElement('iframe');
  iframe.hidden = true;
  iframe.setAttribute('aria-hidden', 'true');
  const cleanup = () => {
    window.removeEventListener('message', onMessage);
    try { iframe.remove(); } catch (_) {}
  };
  const onMessage = (event) => {
    if (!event.data || event.data.token !== token) return;
    cleanup();
    resolve(event.data.result);
  };
  window.addEventListener('message', onMessage);
  const innerScript = `<script>${PROBE_BODY}\nparent.postMessage({ token: ${JSON.stringify(token)}, result: probe() }, '*');<\/script>`;
  try {
    configure(iframe, innerScript);
  } catch (_) { cleanup(); resolve(null); return; }
  document.body.appendChild(iframe);
}));

const probeIframeSrcdoc = () => probeIframe((iframe, scriptHtml) => {
  iframe.srcdoc = `<!doctype html>${scriptHtml}`;
});

const probeIframeBlank = () => withTimeout(new Promise((resolve) => {
  // about:blank iframe — no src, no srcdoc; populated via document.write
  // so we exercise the path where no navigation fires.
  const token = 'realm-' + Math.random().toString(36).slice(2);
  const iframe = document.createElement('iframe');
  iframe.hidden = true;
  iframe.setAttribute('aria-hidden', 'true');
  const cleanup = () => {
    window.removeEventListener('message', onMessage);
    try { iframe.remove(); } catch (_) {}
  };
  const onMessage = (event) => {
    if (!event.data || event.data.token !== token) return;
    cleanup();
    resolve(event.data.result);
  };
  window.addEventListener('message', onMessage);
  document.body.appendChild(iframe);
  try {
    const doc = iframe.contentDocument;
    if (!doc) { cleanup(); resolve(null); return; }
    doc.open();
    doc.write(`<!doctype html><script>${PROBE_BODY}\nparent.postMessage({ token: ${JSON.stringify(token)}, result: probe() }, '*');<\/script>`);
    doc.close();
  } catch (_) { cleanup(); resolve(null); }
}));

const probeIframeBlob = () => probeIframe((iframe, scriptHtml) => {
  const blob = new Blob([`<!doctype html>${scriptHtml}`], { type: 'text/html' });
  iframe.src = URL.createObjectURL(blob);
});

const probeIframeData = () => probeIframe((iframe, scriptHtml) => {
  iframe.src = 'data:text/html,' + encodeURIComponent(`<!doctype html>${scriptHtml}`);
});

const probeWorker = () => withTimeout(new Promise((resolve) => {
  try {
    const src = `${PROBE_BODY}\nself.postMessage(probe());`;
    const url = URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
    const worker = new Worker(url);
    worker.onmessage = (e) => {
      try { worker.terminate(); } catch (_) {}
      URL.revokeObjectURL(url);
      resolve(e.data);
    };
    worker.onerror = () => { try { worker.terminate(); } catch (_) {} resolve(null); };
  } catch (_) { resolve(null); }
}));

const probeSharedWorker = () => withTimeout(new Promise((resolve) => {
  if (typeof SharedWorker !== 'function') { resolve(null); return; }
  try {
    const src = `
      ${PROBE_BODY}
      self.onconnect = (e) => {
        const port = e.ports[0];
        port.postMessage(probe());
      };
    `;
    const url = URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
    const worker = new SharedWorker(url);
    worker.port.onmessage = (e) => {
      URL.revokeObjectURL(url);
      resolve(e.data);
    };
    worker.port.start();
  } catch (_) { resolve(null); }
}));

const probeNestedWorker = () => withTimeout(new Promise((resolve) => {
  try {
    const innerSrc = `${PROBE_BODY}\nself.postMessage(probe());`;
    const outerSrc = `
      const innerBlob = new Blob([${JSON.stringify(innerSrc)}], { type: 'application/javascript' });
      const innerUrl = URL.createObjectURL(innerBlob);
      const inner = new Worker(innerUrl);
      inner.onmessage = (e) => { try { inner.terminate(); } catch (_) {} self.postMessage(e.data); };
      inner.onerror = () => self.postMessage(null);
    `;
    const outerUrl = URL.createObjectURL(new Blob([outerSrc], { type: 'application/javascript' }));
    const outer = new Worker(outerUrl);
    outer.onmessage = (e) => {
      try { outer.terminate(); } catch (_) {}
      URL.revokeObjectURL(outerUrl);
      resolve(e.data);
    };
    outer.onerror = () => { try { outer.terminate(); } catch (_) {} resolve(null); };
  } catch (_) { resolve(null); }
}));

const SPAWNERS = {
  main: probeMain,
  'iframe-srcdoc': probeIframeSrcdoc,
  'iframe-blank': probeIframeBlank,
  'iframe-blob': probeIframeBlob,
  'iframe-data': probeIframeData,
  'worker': probeWorker,
  'shared-worker': probeSharedWorker,
  'nested-worker': probeNestedWorker,
};

// ── Comparison ────────────────────────────────────────────────────────────
// cellState returns one of: 'baseline' | 'match' | 'mismatch' | 'na' | 'absent'.
//   baseline → main column
//   match    → equal to baseline
//   mismatch → differs from baseline (fingerprint leak)
//   na       → property is DOM-only and this context is a worker
//   absent   → context failed to spawn (whole row is null)
const cellState = (ctx, prop, values, baseline) => {
  const row = values[ctx.id];
  if (!row) return 'absent';
  if (ctx.id === 'main') return 'baseline';
  if (prop.kind === 'dom' && ctx.kind === 'worker') return 'na';
  const got = row[prop.key];
  const want = baseline[prop.key];
  return got === want ? 'match' : 'mismatch';
};

export default {
  module: 'realm-coverage',
  category: 'security',
  produces: [{
    id: 'realm-coverage',
    label: 'Policy coverage across realms',
    entropy: 'security',
    description:
      'Runs a shared identity probe inside iframes (srcdoc, about:blank, blob, data) ' +
      'and workers (dedicated, shared, nested). Verifies the page-policy injection ' +
      'reaches every realm a fingerprinter can pull fresh APIs from.',
  }],
  async run(ctx) {
    // Spawn every context in parallel, gather results.
    const entries = await Promise.all(
      CONTEXTS.map(async (c) => [c.id, await SPAWNERS[c.id]()])
    );
    const values = Object.fromEntries(entries);
    const baseline = values.main || {};

    let mismatches = 0;
    let absents = 0;
    let policyMissing = 0;
    for (const c of CONTEXTS) {
      if (c.id === 'main') continue;
      const row = values[c.id];
      if (!row) { absents += 1; continue; }
      if (row.pagePolicyInjected !== true) policyMissing += 1;
      for (const p of PROPERTIES) {
        const s = cellState(c, p, values, baseline);
        if (s === 'mismatch') mismatches += 1;
      }
    }

    const status = policyMissing > 0 || mismatches > 0 ? 'fail'
      : absents > 0 ? 'warn' : 'ok';
    const summary = status === 'ok'
      ? `${CONTEXTS.length} realms covered, no identity divergence`
      : policyMissing > 0
        ? `${policyMissing} realm(s) missing page policy injection`
        : mismatches > 0
          ? `${mismatches} property mismatch(es) across realms`
          : `${absents} realm(s) failed to spawn`;
    const detail = 'Each non-main context should expose identical static identity values ' +
      'and have __otfPagePolicyInjected=true. A mismatch in any cell indicates a fingerprinting ' +
      'bypass — the policy script never reached that realm.';

    // Compact dl-pair rows summarize per-context counts.
    const rows = CONTEXTS.map((c) => {
      const row = values[c.id];
      if (!row) return [c.label, 'failed to spawn'];
      if (c.id === 'main') return [c.label, 'baseline'];
      let m = 0, mm = 0, na = 0;
      for (const p of PROPERTIES) {
        const s = cellState(c, p, values, baseline);
        if (s === 'match') m += 1;
        else if (s === 'mismatch') mm += 1;
        else if (s === 'na') na += 1;
      }
      const flag = row.pagePolicyInjected === true ? '✓ policy injected' : '✗ POLICY MISSING';
      return [c.label, `${flag} · match: ${m}, mismatch: ${mm}, n/a: ${na}`];
    });

    ctx.set('realm-coverage', status, summary, detail, rows);
    // Hand the page the full grid for the rich detail panel.
    ctx.setExtra('realm-coverage', {
      previewKind: 'realm-grid',
      contexts: CONTEXTS,
      properties: PROPERTIES,
      values,
      baseline,
    });
  },
};
