import { signal } from "@opentf/web";

// Per-row state. Status: 'idle' | 'running' | 'ok' | 'warn' | 'fail'
//                       | 'pending-gesture' | 'pending-reload'.
// `rows` is an array of [key, value] pairs rendered as a definition list
// when the row is expanded. `extra` lets a module attach renderer-specific
// payloads (e.g. canvas/webgl preview hooks) without bloating the store.
const makeRowState = () => ({
  status: 'idle',
  summary: 'Not yet run',
  detail: '',
  rows: [],
  extra: null,
});

export const rowIds = signal([]);              // ordered list of row ids
export const rowsMeta = signal({});            // id -> { label, entropy, category, module }
export const rowsState = signal({});           // id -> { status, summary, detail, rows, extra }
export const expanded = signal({});            // id -> bool

export const runState = signal('idle');        // 'idle' | 'running' | 'done' | 'awaiting-reload'
export const filter = signal('all');           // 'all' | 'failures' | 'warnings' | 'high' | 'medium' | 'low' | 'security'

export const registerRow = (id, meta) => {
  if (!rowsState.value[id]) {
    rowsState.value = { ...rowsState.value, [id]: makeRowState() };
  }
  rowsMeta.value = { ...rowsMeta.value, [id]: meta };
  if (!rowIds.value.includes(id)) {
    rowIds.value = [...rowIds.value, id];
  }
};

export const setRow = (id, patch) => {
  const prev = rowsState.value[id] || makeRowState();
  rowsState.value = { ...rowsState.value, [id]: { ...prev, ...patch } };
};

export const setStatus = (id, status, summary, detail, rows) => {
  setRow(id, {
    status,
    summary: summary ?? '',
    detail: detail ?? '',
    ...(rows !== undefined ? { rows } : {}),
  });
};

export const setExtra = (id, extra) => setRow(id, { extra });

export const toggleExpanded = (id) => {
  expanded.value = { ...expanded.value, [id]: !expanded.value[id] };
};

// Score derived from status counts. Categories: ok=1, warn=0.5, fail=0.
export const computeScore = () => {
  const ids = rowIds.value;
  const state = rowsState.value;
  const scoreable = ids.filter((id) => {
    const s = state[id]?.status;
    return s === 'ok' || s === 'warn' || s === 'fail';
  });
  if (scoreable.length === 0) return { score: 0, complete: 0, total: ids.length };
  const sum = scoreable.reduce((acc, id) => {
    const s = state[id].status;
    return acc + (s === 'ok' ? 1 : s === 'warn' ? 0.5 : 0);
  }, 0);
  return {
    score: Math.round((sum / ids.length) * 100),
    complete: scoreable.length,
    total: ids.length,
  };
};
