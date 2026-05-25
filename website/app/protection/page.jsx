import { onMount, signal } from "@opentf/web";
import { protectionStyles } from "./styles.js";
import {
  rowIds, rowsMeta, rowsState, expanded, runState, filter,
  toggleExpanded, computeScore,
} from "./store.js";
import { registerAll, startSuite, maybeAutoResume } from "./runner.js";
import { drawFingerprintScene } from "./tests/helpers.js";
import { playTestTone, onSpeakerStateChange, getSpeakerState } from "./tests/media-test.js";

// Filter chips: 'all' | 'failures' | 'warnings' | 'high' | 'medium' | 'low' | 'security'.
const FILTERS = [
  ['all', 'All'],
  ['failures', 'Failures'],
  ['warnings', 'Warnings'],
  ['high', 'High entropy'],
  ['medium', 'Medium entropy'],
  ['low', 'Low entropy'],
  ['security', 'Security'],
];

const isRowVisible = (id, currentFilter) => {
  const meta = rowsMeta.value[id];
  const state = rowsState.value[id];
  if (!meta || !state) return false;
  switch (currentFilter) {
    case 'all': return true;
    case 'failures': return state.status === 'fail';
    case 'warnings': return state.status === 'warn';
    case 'high': return meta.entropy === 'high';
    case 'medium': return meta.entropy === 'medium';
    case 'low': return meta.entropy === 'low';
    case 'security': return meta.entropy === 'security' || meta.category === 'security';
    default: return true;
  }
};

const statusGlyph = (status) =>
  status === 'ok' ? '✓'
  : status === 'fail' ? '×'
  : status === 'warn' ? '!'
  : status === 'pending-gesture' ? '?'
  : status === 'pending-reload' ? '↻'
  : status === 'running' ? '…'
  : '·';

// Reload-banner signal — flipped on by the runner via onReloadNeeded.
// Resolves on user choice; the runner waits on the resulting Promise.
const reloadPrompt = signal(null);   // { modules: [...], resolve } | null

const buildJsonReport = () => {
  const tests = rowIds.value.map((id) => {
    const meta = rowsMeta.value[id];
    const state = rowsState.value[id];
    const entry = {
      id,
      module: meta?.module || null,
      category: meta?.category || null,
      entropy: meta?.entropy || null,
      status: state?.status || null,
      summary: state?.summary || '',
      detail: state?.detail || '',
    };
    if (state?.rows?.length) {
      entry.rows = state.rows.map(([label, value]) => ({ label, value }));
    }
    return entry;
  });
  const { score } = computeScore();
  return {
    schema: 'otf-protection-report',
    schemaVersion: 3,
    capturedAt: new Date().toISOString(),
    userAgent: navigator.userAgent,
    score,
    tests,
  };
};

export default function ProtectionPage() {
  const exportJson = signal('');
  const speakerState = signal(getSpeakerState());

  onMount(() => {
    registerAll();
    onSpeakerStateChange((s) => { speakerState.value = { ...s }; });
    // If the runner reloaded us mid-suite, pick up where we left off.
    maybeAutoResume({
      onReloadNeeded: (mod) => new Promise((resolve) => {
        reloadPrompt.value = { modules: mod, resolve };
      }),
    });
  });

  const startSuiteWithGesture = () => {
    if (runState.value === 'running' || runState.value === 'awaiting-reload') return;
    // Modules with needsGesture must run inside this click frame so transient
    // user activation is still valid. startSuite handles the dispatch.
    startSuite({
      onReloadNeeded: (mod) => new Promise((resolve) => {
        reloadPrompt.value = { modules: mod, resolve };
      }),
    }).catch((err) => console.error('Suite error:', err));
  };

  const onExpandClick = (id) => {
    toggleExpanded(id);
    // After the detail mounts, draw the live preview if this row has one.
    const extra = rowsState.value[id]?.extra;
    if (extra?.previewKind === 'canvas-fingerprint') {
      requestAnimationFrame(() => {
        const canvas = document.getElementById(`preview-canvas-${id}`);
        if (canvas) drawFingerprintScene(canvas);
      });
    }
  };

  const acceptReload = () => {
    const p = reloadPrompt.value;
    if (p) { p.resolve(true); reloadPrompt.value = null; }
  };
  const declineReload = () => {
    const p = reloadPrompt.value;
    if (p) { p.resolve(false); reloadPrompt.value = null; }
  };

  const onExport = () => {
    exportJson.value = JSON.stringify(buildJsonReport(), null, 2);
  };

  const startLabel = () => {
    const s = runState.value;
    return s === 'running' ? 'Running…'
      : s === 'awaiting-reload' ? 'Reload pending…'
      : s === 'done' ? 'Re-run Test Suite'
      : 'Start Test Suite';
  };

  return (
    <div id="protection-container">
      <style>{protectionStyles}</style>
      <main>
        <header>
          <p className="eyebrow">Browser Protection Test Center</p>
          <h1>Privacy, Security & Abuse Resistance Tests</h1>
          <p>
            Run this page in any browser to inspect fingerprinting exposure, security-sensitive
            API behavior, and protection gaps. Click a row to see the underlying probe data.
            Tests are tagged by <strong>entropy</strong> — how many bits of identification each
            surface can leak on its own.
          </p>
          <div className="header-actions">
            <button
              onClick={startSuiteWithGesture}
              className={runState.value === 'running' || runState.value === 'awaiting-reload' ? 'is-disabled' : ''}
            >
              {startLabel()}
            </button>
            <button
              className={runState.value === 'done' ? 'secondary' : 'secondary is-disabled'}
              onClick={() => { if (runState.value === 'done') onExport(); }}
            >
              Export JSON
            </button>
          </div>
          {reloadPrompt.value ? (
            <div className="reload-banner">
              <div>
                <strong>Some tests need to reload the page.</strong>
                <small>
                  These tests compare values across two fresh page loads. Continue to reload now.
                </small>
              </div>
              <div className="banner-actions">
                <button onClick={acceptReload}>Continue</button>
                <button className="secondary" onClick={declineReload}>Skip</button>
              </div>
            </div>
          ) : null}
          {exportJson.value ? (
            <textarea
              id="export-json-output"
              readOnly
              spellCheck="false"
              aria-label="Exported JSON report"
            >{exportJson.value}</textarea>
          ) : null}
        </header>

        <section className="score-card">
          <div className="score-number">
            {computeScore().score}<small>/100</small>
          </div>
          <div>
            <div>Unified browser protection score</div>
            <div className="score-bar">
              <div style={{ width: `${computeScore().score}%` }}></div>
            </div>
            <div className="score-label">
              {computeScore().complete}/{computeScore().total} tests complete
            </div>
          </div>
        </section>

        <div className="filter-bar">
          {FILTERS.map(([key, label]) => (
            <button
              key={key}
              className={filter.value === key ? 'active' : ''}
              onClick={() => { filter.value = key; }}
            >
              {label}
            </button>
          ))}
        </div>

        <div className="row-list">
          {rowIds.value.filter((id) => isRowVisible(id, filter.value)).map((id) => (
            <div className="row" id={id} key={id}>
              <div className="row-head" onClick={() => onExpandClick(id)}>
                <span className={`row-icon ${rowsState.value[id].status}`}>
                  {statusGlyph(rowsState.value[id].status)}
                </span>
                <div className="row-meta">
                  <div className="row-label">{rowsMeta.value[id].label}</div>
                  <div className="row-summary">{rowsState.value[id].summary}</div>
                </div>
                <div className="row-tags">
                  <span className={`tag entropy-${rowsMeta.value[id].entropy}`}>
                    {rowsMeta.value[id].entropy}
                  </span>
                </div>
                <div className="row-toggle">{expanded.value[id] ? '▾' : '▸'}</div>
              </div>
              {expanded.value[id] ? (
                <div className="row-detail">
                  {rowsMeta.value[id].description ? (
                    <div className="detail-text">{rowsMeta.value[id].description}</div>
                  ) : null}
                  {rowsState.value[id].detail ? (
                    <div className="detail-text">{rowsState.value[id].detail}</div>
                  ) : null}
                  {rowsState.value[id].extra?.previewKind === 'canvas-fingerprint' ? (
                    <div className="preview">
                      <canvas id={`preview-canvas-${id}`} width="420" height="140"></canvas>
                    </div>
                  ) : null}
                  {rowsState.value[id].extra?.previewKind === 'realm-grid' ? (() => {
                    const { contexts, properties, values, baseline } = rowsState.value[id].extra;
                    const cellState = (ctx, prop) => {
                      const row = values[ctx.id];
                      if (!row) return 'absent';
                      if (prop.kind === 'dom' && ctx.kind === 'worker') return 'na';
                      const v = row[prop.key];
                      if (v === null || v === undefined) return 'null';
                      if (ctx.id === 'main') return 'baseline';
                      if (String(v) === String(baseline[prop.key])) return 'match';
                      return 'mismatch';
                    };
                    return (
                      <div className="realm-grid-wrap">
                        <div className="realm-grid-scroll">
                          <table className="realm-grid">
                            <thead>
                              <tr>
                                <th className="realm-grid-corner">Property</th>
                                {contexts.map((c) => (
                                  <th key={c.id} className="realm-grid-ph" title={c.id}>{c.label}</th>
                                ))}
                              </tr>
                            </thead>
                            <tbody>
                              {properties.map((p) => (
                                <tr key={p.key}>
                                  <td className="realm-grid-ctx" title={p.key}>{p.label}</td>
                                  {contexts.map((c) => {
                                    const s = cellState(c, p);
                                    const row = values[c.id];
                                    const val = row ? String(row[p.key] ?? '–') : '–';
                                    const tip = s === 'na' ? 'N/A (DOM-only)' : val;
                                    return (
                                      <td key={c.id} className={`realm-grid-cell rg-${s}`} title={tip}>
                                        {s === 'match' ? '✓'
                                          : s === 'mismatch' ? '✗'
                                          : s === 'baseline' ? '·'
                                          : s === 'na' ? '—'
                                          : s === 'absent' ? '!'
                                          : '?'}
                                      </td>
                                    );
                                  })}
                                </tr>
                              ))}
                            </tbody>
                          </table>
                        </div>
                        <div className="realm-grid-legend">
                          <span className="rgl rg-match">✓ match</span>
                          <span className="rgl rg-mismatch">✗ mismatch</span>
                          <span className="rgl rg-baseline">· baseline</span>
                          <span className="rgl rg-na">— N/A</span>
                          <span className="rgl rg-absent">! failed</span>
                          <span className="rgl rg-null">? null</span>
                        </div>
                      </div>
                    );
                  })() : null}
                  {id === 'fp-enumerate-devices' ? (
                    <div className="preview">
                      <div className="speaker-test-header">
                        <strong>Speaker test</strong>
                        <span className="speaker-test-note">
                          Plays a 700 ms 440 Hz tone through the default output to confirm
                          audio still works with the spoofed device identity. Not scored.
                        </span>
                      </div>
                      <button
                        onClick={playTestTone}
                        className={speakerState.value.status === 'playing' ? 'is-disabled' : ''}
                      >
                        {speakerState.value.status === 'playing'
                          ? 'Playing…'
                          : speakerState.value.status === 'ok'
                            ? 'Play tone again'
                            : 'Play test tone'}
                      </button>
                      {speakerState.value.detail ? (
                        <div className={`speaker-test-result ${speakerState.value.status}`}>
                          {speakerState.value.detail}
                        </div>
                      ) : null}
                      {speakerState.value.rows.length > 0 ? (
                        <dl>
                          {speakerState.value.rows.map(([k, v], i) => (
                            <div key={`spk-${k}-${i}`} className="dl-pair">
                              <dt>{k}</dt>
                              <dd>{v}</dd>
                            </div>
                          ))}
                        </dl>
                      ) : null}
                    </div>
                  ) : null}
                  {rowsState.value[id].rows && rowsState.value[id].rows.length > 0 ? (
                    <dl>
                      {rowsState.value[id].rows.map(([k, v], i) => (
                        <div key={`${k}-${i}`} className="dl-pair">
                          <dt>{k}</dt>
                          <dd>{v}</dd>
                        </div>
                      ))}
                    </dl>
                  ) : null}
                </div>
              ) : null}
            </div>
          ))}
        </div>
      </main>
    </div>
  );
}
