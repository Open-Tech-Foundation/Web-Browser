import React, { useEffect, useRef, useState, useCallback } from 'react';
import { nativeRequest } from '../src/shared/nativeRequest';

// Must match cef_log_severity_t: DEFAULT=0, VERBOSE=1, INFO=2, WARNING=3, ERROR=4
const LEVEL_VERBOSE = 1;
const LEVEL_INFO = 2;
const LEVEL_WARNING = 3;
const LEVEL_ERROR = 4;

function levelLabel(level) {
  if (level >= LEVEL_ERROR) return 'error';
  if (level === LEVEL_WARNING) return 'warn';
  if (level === LEVEL_INFO) return 'log';  // console.log and console.info both map here
  return 'log';
}

function levelColor(level) {
  if (level >= LEVEL_ERROR) return '#ef4444';
  if (level === LEVEL_WARNING) return '#f59e0b';
  if (level === LEVEL_INFO) return '#60a5fa';
  return '#94a3b8';
}

function levelBg(level) {
  if (level >= LEVEL_ERROR) return 'rgba(239,68,68,0.08)';
  if (level === LEVEL_WARNING) return 'rgba(245,158,11,0.06)';
  return 'transparent';
}

function levelTextColor(level) {
  if (level >= LEVEL_ERROR) return '#fca5a5';
  if (level === LEVEL_WARNING) return '#fcd34d';
  return '#cbd5e1';
}

const TABLE_SENTINEL = '__OTF_TABLE__:';
const LOG_SENTINEL = '__OTF_LOG__:';

function parseMessage(message) {
  if (!message) return { summary: '', lines: [], table: null, logArgs: null };
  if (message.startsWith(TABLE_SENTINEL)) {
    try {
      const table = JSON.parse(message.slice(TABLE_SENTINEL.length));
      return { summary: '[table]', lines: [], table, logArgs: null };
    } catch {}
  }
  if (message.startsWith(LOG_SENTINEL)) {
    try {
      const parsed = JSON.parse(message.slice(LOG_SENTINEL.length));
      if (parsed && Array.isArray(parsed.args)) {
        return { summary: '', lines: [], table: null, logArgs: parsed.args };
      }
    } catch {}
  }
  const parts = message.split('\n');
  const summary = parts[0];
  const lines = parts.slice(1).filter(Boolean);
  return { summary, lines, table: null, logArgs: null };
}

// Keep backward compat alias used by MessageModal
function parseStackTrace(message) {
  const { summary, lines } = parseMessage(message);
  return { summary, lines };
}

function LogValue({ value, depth = 0, defaultOpen }) {
  const initialOpen = defaultOpen !== undefined ? defaultOpen : depth < 1;
  const [open, setOpen] = useState(initialOpen);
  const collapsibleStyle = { cursor: 'pointer', color: '#94a3b8', userSelect: 'none' };
  if (!value || typeof value !== 'object') {
    return <span style={{ color: '#94a3b8' }}>?</span>;
  }
  switch (value.t) {
    case 'null':      return <span style={{ color: '#64748b' }}>null</span>;
    case 'undefined': return <span style={{ color: '#64748b' }}>undefined</span>;
    case 'boolean':   return <span style={{ color: '#fbbf24' }}>{String(value.v)}</span>;
    case 'number':    return <span style={{ color: '#60a5fa' }}>{String(value.v)}</span>;
    case 'bigint':    return <span style={{ color: '#60a5fa' }}>{value.v}</span>;
    case 'string':    return <span style={{ color: '#86efac' }}>{JSON.stringify(value.v)}</span>;
    case 'symbol':    return <span style={{ color: '#c084fc' }}>{value.v}</span>;
    case 'function':  return <span style={{ color: '#c084fc' }}>ƒ {value.v}()</span>;
    case 'date':      return <span style={{ color: '#fbbf24' }}>Date({value.v})</span>;
    case 'regexp':    return <span style={{ color: '#f97316' }}>{value.v}</span>;
    case 'circular':  return <span style={{ color: '#ef4444' }}>[Circular]</span>;
    case 'error': {
      const e = value.v || {};
      return (
        <span>
          <span style={collapsibleStyle} onClick={() => setOpen(o => !o)}>
            {open ? '▾' : '▸'} <span style={{ color: '#ef4444' }}>{e.name || 'Error'}: {e.message}</span>
          </span>
          {open && e.stack && (
            <pre style={{ margin: '2px 0 2px 14px', fontSize: '10px', color: '#64748b', whiteSpace: 'pre-wrap' }}>
              {e.stack}
            </pre>
          )}
        </span>
      );
    }
    case 'array':
    case 'object':
    case 'map':
    case 'set': {
      const isArr = value.t === 'array';
      const isMap = value.t === 'map';
      const isSet = value.t === 'set';
      const openCh = isArr || isSet ? '[' : '{';
      const closeCh = isArr || isSet ? ']' : '}';
      const label = value.ctor || (isArr ? 'Array' : isMap ? 'Map' : isSet ? 'Set' : 'Object');
      const count = value.len !== undefined ? value.len
        : isArr || isSet ? (value.v ? value.v.length : 0)
        : isMap ? (value.v ? value.v.length : 0)
        : (value.v ? Object.keys(value.v).length : 0);
      let entries = [];
      if (isArr) entries = (value.v || []).map((v, i) => [i, v]);
      else if (isMap) entries = (value.v || []);
      else if (isSet) entries = (value.v || []).map((v, i) => [i, v]);
      else entries = Object.entries(value.v || {});
      return (
        <span>
          <span style={collapsibleStyle} onClick={() => setOpen(o => !o)}>
            {open ? '▾' : '▸'} <span style={{ color: '#cbd5e1' }}>{label}</span>
            <span style={{ color: '#64748b' }}>({count})</span> {openCh}
          </span>
          {open && (
            <div style={{ paddingLeft: '14px', borderLeft: '1px solid rgba(255,255,255,0.05)' }}>
              {entries.map(([k, v], i) => (
                <div key={i} style={{ display: 'flex', gap: '6px' }}>
                  <span style={{ color: '#64748b', flexShrink: 0 }}>
                    {isMap ? <LogValue value={k} depth={depth + 1} /> : String(k)}:
                  </span>
                  <span style={{ flex: 1, minWidth: 0 }}>
                    <LogValue value={v} depth={depth + 1} />
                  </span>
                </div>
              ))}
              {value.truncated && <div style={{ color: '#64748b' }}>… truncated</div>}
            </div>
          )}
          {!open && <span style={{ color: '#64748b' }}> … </span>}
          <span style={{ color: '#94a3b8' }}>{closeCh}</span>
        </span>
      );
    }
    default:
      return <span style={{ color: '#94a3b8' }}>?</span>;
  }
}

function LogArgsView({ args }) {
  return (
    <div style={{
      fontFamily: 'ui-monospace, "Cascadia Code", Consolas, monospace',
      fontSize: '11px',
      lineHeight: '1.5',
      color: '#cbd5e1',
    }}>
      {args.map((a, i) => (
        <div key={i} style={{ marginBottom: i < args.length - 1 ? '2px' : 0 }}>
          <LogValue value={a} depth={0} />
        </div>
      ))}
    </div>
  );
}

function TableView({ table }) {
  const { keys, rows } = table;
  const cellStyle = {
    padding: '2px 8px',
    borderRight: '1px solid rgba(255,255,255,0.07)',
    whiteSpace: 'nowrap',
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    maxWidth: '200px',
    fontSize: '11px',
    fontFamily: 'ui-monospace, monospace',
    color: '#cbd5e1',
  };
  const headStyle = {
    ...cellStyle,
    color: '#94a3b8',
    fontWeight: 700,
    background: 'rgba(255,255,255,0.04)',
    borderBottom: '1px solid rgba(255,255,255,0.1)',
  };
  return (
    <div style={{ overflowX: 'auto', margin: '4px 0' }}>
      <table style={{
        borderCollapse: 'collapse',
        fontSize: '11px',
        width: '100%',
        background: 'rgba(0,0,0,0.2)',
        border: '1px solid rgba(255,255,255,0.07)',
        borderRadius: '3px',
      }}>
        <thead>
          <tr>
            <th style={{ ...headStyle, color: '#475569' }}>#</th>
            {keys.map((k) => (
              <th key={k} style={headStyle}>{k}</th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map((row, i) => (
            <tr key={i} style={{ borderBottom: '1px solid rgba(255,255,255,0.04)' }}
              onMouseEnter={(e) => { e.currentTarget.style.background = 'rgba(255,255,255,0.03)'; }}
              onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent'; }}
            >
              <td style={{ ...cellStyle, color: '#475569' }}>{i}</td>
              {keys.map((k) => {
                const val = row[k];
                const display = val === undefined ? '' : val === null ? 'null' : typeof val === 'object' ? JSON.stringify(val) : String(val);
                return <td key={k} style={cellStyle} title={display}>{display}</td>;
              })}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function formatSource(source, line) {
  if (!source) return '';
  try {
    const u = new URL(source);
    const name = u.pathname.split('/').pop() || u.hostname;
    return line > 0 ? `${name}:${line}` : name;
  } catch {
    const short = source.length > 36 ? '…' + source.slice(-33) : source;
    return line > 0 ? `${short}:${line}` : short;
  }
}

function formatTime(ts) {
  const d = new Date(ts);
  const h = String(d.getHours()).padStart(2, '0');
  const m = String(d.getMinutes()).padStart(2, '0');
  const s = String(d.getSeconds()).padStart(2, '0');
  const ms = String(d.getMilliseconds()).padStart(3, '0');
  return `${h}:${m}:${s}.${ms}`;
}

function LevelBadge({ level }) {
  const label = levelLabel(level);
  const color = levelColor(level);
  return (
    <span style={{
      display: 'inline-flex',
      alignItems: 'center',
      justifyContent: 'center',
      minWidth: '34px',
      padding: '0 4px',
      height: '16px',
      borderRadius: '3px',
      fontSize: '9px',
      fontFamily: 'system-ui',
      fontWeight: 700,
      letterSpacing: '0.04em',
      textTransform: 'uppercase',
      color,
      border: `1px solid ${color}40`,
      background: `${color}15`,
      flexShrink: 0,
    }}>
      {label}
    </span>
  );
}

function MessageModal({ entry, onClose }) {
  const { summary, lines } = parseStackTrace(entry.message);
  const src = entry.source
    ? (entry.line > 0 ? `${entry.source}:${entry.line}` : entry.source)
    : '';

  useEffect(() => {
    const handler = (e) => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [onClose]);

  return (
    <div
      onClick={onClose}
      style={{
        position: 'fixed', inset: 0, zIndex: 1000,
        background: 'rgba(0,0,0,0.6)',
        display: 'flex', alignItems: 'flex-end',
      }}
    >
      <div
        onClick={(e) => e.stopPropagation()}
        style={{
          width: '100%',
          maxHeight: '70vh',
          background: '#1e293b',
          borderTop: `2px solid ${levelColor(entry.level)}`,
          display: 'flex',
          flexDirection: 'column',
          overflow: 'hidden',
        }}
      >
        {/* Modal header */}
        <div style={{
          display: 'flex',
          alignItems: 'center',
          gap: '8px',
          padding: '8px 12px',
          borderBottom: '1px solid rgba(255,255,255,0.08)',
          flexShrink: 0,
        }}>
          <LevelBadge level={entry.level} />
          {entry.timestamp_ms > 0 && (
            <span style={{ fontSize: '10px', color: '#475569', fontFamily: 'ui-monospace, monospace' }}>
              {formatTime(entry.timestamp_ms)}
            </span>
          )}
          <div style={{ flex: 1 }} />
          <button
            onClick={onClose}
            style={{
              width: '22px', height: '22px', border: 'none', borderRadius: '4px',
              background: 'transparent', cursor: 'pointer', color: '#64748b',
              display: 'flex', alignItems: 'center', justifyContent: 'center',
            }}
            onMouseEnter={(e) => { e.currentTarget.style.background = 'rgba(255,255,255,0.08)'; e.currentTarget.style.color = '#e2e8f0'; }}
            onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent'; e.currentTarget.style.color = '#64748b'; }}
          >
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
              <line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>
            </svg>
          </button>
        </div>
        {/* Message body */}
        <div style={{ flex: 1, overflowY: 'auto', padding: '12px' }}>
          <div style={{
            color: levelTextColor(entry.level),
            fontFamily: 'ui-monospace, "Cascadia Code", Consolas, monospace',
            fontSize: '12px',
            lineHeight: '1.6',
            whiteSpace: 'pre-wrap',
            wordBreak: 'break-word',
          }}>
            {summary}
          </div>
          {lines.length > 0 && (
            <div style={{
              marginTop: '10px',
              padding: '8px 10px',
              background: 'rgba(0,0,0,0.3)',
              borderRadius: '4px',
              borderLeft: '2px solid rgba(255,255,255,0.1)',
            }}>
              {lines.map((line, i) => (
                <div key={i} style={{
                  color: '#64748b',
                  fontFamily: 'ui-monospace, "Cascadia Code", Consolas, monospace',
                  fontSize: '11px',
                  lineHeight: '1.7',
                  whiteSpace: 'pre-wrap',
                  wordBreak: 'break-word',
                }}>
                  {line}
                </div>
              ))}
            </div>
          )}
          {src && (
            <div style={{
              marginTop: '10px',
              fontSize: '10px',
              color: '#475569',
              fontFamily: 'ui-monospace, monospace',
              wordBreak: 'break-all',
            }}>
              {src}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

function ConsoleRow({ entry, onClick }) {
  const color = levelColor(entry.level);
  const bg = levelBg(entry.level);
  const src = formatSource(entry.source, entry.line);
  const { summary, lines, table, logArgs } = parseMessage(entry.message);
  const hasStack = lines.length > 0;

  if (logArgs) {
    return (
      <div style={{
        padding: '4px 8px',
        borderBottom: '1px solid rgba(255,255,255,0.04)',
        background: bg,
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '6px', marginBottom: '4px' }}>
          <LevelBadge level={entry.level} />
          {entry.timestamp_ms > 0 && (
            <span style={{ color: '#475569', fontSize: '10px', fontFamily: 'ui-monospace, monospace' }}>
              {formatTime(entry.timestamp_ms)}
            </span>
          )}
          {src && <span style={{ color: '#475569', fontSize: '10px', marginLeft: 'auto', fontFamily: 'ui-monospace, monospace' }} title={`${entry.source}:${entry.line}`}>{src}</span>}
        </div>
        <LogArgsView args={logArgs} />
      </div>
    );
  }

  if (table) {
    return (
      <div style={{
        padding: '4px 8px',
        borderBottom: '1px solid rgba(255,255,255,0.04)',
        background: bg,
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '6px', marginBottom: '4px' }}>
          <LevelBadge level={entry.level} />
          <span style={{ color: '#64748b', fontSize: '10px', fontFamily: 'ui-monospace, monospace' }}>table</span>
          {src && <span style={{ color: '#475569', fontSize: '10px', marginLeft: 'auto', fontFamily: 'ui-monospace, monospace' }} title={`${entry.source}:${entry.line}`}>{src}</span>}
        </div>
        <TableView table={table} />
      </div>
    );
  }

  return (
    <div
      onClick={onClick}
      style={{
        display: 'flex',
        alignItems: 'center',
        gap: '6px',
        padding: '0 8px',
        height: '22px',
        borderBottom: '1px solid rgba(255,255,255,0.04)',
        background: bg,
        cursor: 'pointer',
        flexShrink: 0,
      }}
      onMouseEnter={(e) => {
        e.currentTarget.style.background = bg
          ? bg.replace('0.08', '0.14').replace('0.06', '0.12')
          : 'rgba(255,255,255,0.04)';
      }}
      onMouseLeave={(e) => { e.currentTarget.style.background = bg; }}
    >
      <LevelBadge level={entry.level} />
      <span style={{
        flex: 1,
        color: levelTextColor(entry.level),
        whiteSpace: 'nowrap',
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        lineHeight: '22px',
      }}>
        {summary}
      </span>
      {hasStack && (
        <span style={{ color: '#475569', fontSize: '10px', flexShrink: 0 }} title="Has stack trace">⋮</span>
      )}
      {src && (
        <span style={{
          color: '#475569',
          fontSize: '10px',
          flexShrink: 0,
          maxWidth: '100px',
          overflow: 'hidden',
          textOverflow: 'ellipsis',
          whiteSpace: 'nowrap',
          fontFamily: 'ui-monospace, monospace',
        }} title={`${entry.source}:${entry.line}`}>
          {src}
        </span>
      )}
    </div>
  );
}

function NavMarker({ label }) {
  return (
    <div style={{
      display: 'flex',
      alignItems: 'center',
      gap: '8px',
      padding: '2px 8px',
      height: '20px',
      background: 'rgba(99,102,241,0.08)',
      borderBottom: '1px solid rgba(99,102,241,0.15)',
      borderTop: '1px solid rgba(99,102,241,0.15)',
      flexShrink: 0,
    }}>
      <div style={{ flex: 1, height: '1px', background: 'rgba(99,102,241,0.25)' }} />
      <span style={{
        fontSize: '9px',
        fontFamily: 'system-ui',
        fontWeight: 600,
        color: '#818cf8',
        letterSpacing: '0.06em',
        textTransform: 'uppercase',
        flexShrink: 0,
      }}>
        {label}
      </span>
      <div style={{ flex: 1, height: '1px', background: 'rgba(99,102,241,0.25)' }} />
    </div>
  );
}

function ResizeHandle() {
  const startXRef = useRef(0);
  const startWidthRef = useRef(0);
  const rafRef = useRef(null);
  const pendingWidthRef = useRef(null);

  const onMouseDown = (e) => {
    e.preventDefault();
    const panel = e.currentTarget.parentElement;
    startWidthRef.current = panel ? panel.offsetWidth : 420;
    startXRef.current = e.clientX;

    const sendWidth = () => {
      if (pendingWidthRef.current !== null) {
        nativeRequest({
          method: 'console.setWidth',
          params: { width: pendingWidthRef.current },
        }).catch(() => {});
        pendingWidthRef.current = null;
      }
      rafRef.current = null;
    };

    const onMouseMove = (ev) => {
      const delta = ev.clientX - startXRef.current;
      pendingWidthRef.current = Math.max(240, Math.min(900, Math.round(startWidthRef.current - delta)));
      if (!rafRef.current) {
        rafRef.current = requestAnimationFrame(sendWidth);
      }
    };

    const onMouseUp = () => {
      document.removeEventListener('mousemove', onMouseMove);
      document.removeEventListener('mouseup', onMouseUp);
      if (rafRef.current) {
        cancelAnimationFrame(rafRef.current);
        rafRef.current = null;
      }
      // Flush final position
      if (pendingWidthRef.current !== null) {
        nativeRequest({
          method: 'console.setWidth',
          params: { width: pendingWidthRef.current },
        }).catch(() => {});
        pendingWidthRef.current = null;
      }
    };

    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup', onMouseUp);
  };

  return (
    <div
      onMouseDown={onMouseDown}
      style={{
        position: 'absolute',
        top: 0,
        left: 0,
        width: '5px',
        height: '100%',
        cursor: 'col-resize',
        zIndex: 10,
        background: 'transparent',
        transition: 'background 0.15s',
      }}
      onMouseEnter={(e) => { e.currentTarget.style.background = 'rgba(99,102,241,0.35)'; }}
      onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent'; }}
    />
  );
}

const FILTER_ALL = 'all';
const MAX_DISPLAY = 500;

export default function ConsolePanel() {
  const [entries, setEntries] = useState([]);
  const [filter, setFilter] = useState(FILTER_ALL);
  const [tabId, setTabId] = useState(-1);
  const [autoScroll, setAutoScroll] = useState(true);
  const [modal, setModal] = useState(null);
  const bottomRef = useRef(null);
  const scrollRef = useRef(null);
  const tabIdRef = useRef(-1);

  const loadLogsForTab = useCallback((tid) => {
    if (!window.cefQuery || tid < 0) return;
    nativeRequest({
      method: 'console.logs',
      params: { tabId: tid },
    })
      .then((items) => {
        if (tid !== tabIdRef.current) return;
        setEntries(Array.isArray(items) ? items.slice(-MAX_DISPLAY) : []);
        setTabId(tid);
      })
      .catch(() => {});
  }, []);

  useEffect(() => {
    if (!window.cefQuery) return;

    window.cefQuery({
      request: 'subscribe-console',
      persistent: true,
      onSuccess: (eventStr) => {
        try {
          const event = JSON.parse(eventStr);
          if (event.key === 'console-tab-changed') {
            // Fix race: update ref synchronously before async fetch
            tabIdRef.current = event.tabId;
            setTabId(event.tabId);
            loadLogsForTab(event.tabId);
            return;
          }
          if (event.key === 'console-entry') {
            if (event.tabId === tabIdRef.current) {
              setEntries((prev) => {
                const next = [...prev, event];
                return next.length > MAX_DISPLAY ? next.slice(-MAX_DISPLAY) : next;
              });
            }
          }
        } catch {}
      },
    });

    window.cefQuery({
      request: 'get-active-tab',
      onSuccess: (id) => {
        const tid = parseInt(id, 10);
        if (!isNaN(tid) && tid >= 0) {
          tabIdRef.current = tid;
          setTabId(tid);
          loadLogsForTab(tid);
        }
      },
    });
  }, [loadLogsForTab]);

  useEffect(() => {
    if (autoScroll && bottomRef.current) {
      bottomRef.current.scrollIntoView({ block: 'end' });
    }
  }, [entries, autoScroll]);

  const handleScroll = () => {
    const el = scrollRef.current;
    if (!el) return;
    const atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 40;
    setAutoScroll(atBottom);
  };

  const handleClear = () => {
    if (tabId >= 0) {
      nativeRequest({
        method: 'console.clear',
        params: { tabId },
      }).catch(() => {});
    }
    setEntries([]);
  };

  const handleClose = () => {
    nativeRequest({ method: 'ui.console.hide' }).catch(() => {});
  };

  const filtered = filter === FILTER_ALL
    ? entries
    : entries.filter((e) => levelLabel(e.level) === filter);

  const counts = {
    error: entries.filter((e) => e.level >= LEVEL_ERROR).length,
    warn: entries.filter((e) => e.level === LEVEL_WARNING).length,
    info: entries.filter((e) => e.level === LEVEL_INFO).length,
    log: entries.filter((e) => e.level < LEVEL_WARNING && e.level !== LEVEL_INFO).length,
  };

  const filterButtons = [
    { key: FILTER_ALL, label: 'All' },
    { key: 'error', label: 'Errors', count: counts.error },
    { key: 'warn', label: 'Warn', count: counts.warn },
    { key: 'info', label: 'Info', count: counts.info },
    { key: 'log', label: 'Log', count: counts.log },
  ];

  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      width: '100%',
      height: '100%',
      background: '#0f172a',
      borderLeft: '1px solid rgba(255,255,255,0.08)',
      fontFamily: 'ui-monospace, "Cascadia Code", "Fira Code", Consolas, monospace',
      fontSize: '12px',
      color: '#e2e8f0',
      overflow: 'hidden',
      position: 'relative',
    }}>
      <ResizeHandle />
      {/* Header */}
      <div style={{
        display: 'flex',
        alignItems: 'center',
        gap: '4px',
        padding: '0 6px',
        height: '34px',
        minHeight: '34px',
        background: '#1e293b',
        borderBottom: '1px solid rgba(255,255,255,0.08)',
        userSelect: 'none',
        flexShrink: 0,
      }}>
        <span style={{ fontFamily: 'system-ui', fontSize: '10px', fontWeight: 700, color: '#94a3b8', letterSpacing: '0.07em', flexShrink: 0 }}>
          CONSOLE
        </span>
        {/* Clear chip */}
        <button
          onClick={handleClear}
          title="Clear console"
          style={{
            display: 'flex', alignItems: 'center', gap: '3px',
            padding: '1px 8px', borderRadius: '10px',
            border: '1px solid rgba(239,68,68,0.35)',
            background: 'rgba(239,68,68,0.12)', cursor: 'pointer',
            fontSize: '9px', fontFamily: 'system-ui', fontWeight: 700,
            color: '#f87171', letterSpacing: '0.03em',
            flexShrink: 0, lineHeight: '16px',
          }}
          onMouseEnter={(e) => { e.currentTarget.style.background = 'rgba(239,68,68,0.25)'; e.currentTarget.style.color = '#fca5a5'; }}
          onMouseLeave={(e) => { e.currentTarget.style.background = 'rgba(239,68,68,0.12)'; e.currentTarget.style.color = '#f87171'; }}
        >
          clear
        </button>
        <div style={{ flex: 1 }} />
        {filterButtons.map(({ key, label, count }) => (
          <button
            key={key}
            onClick={() => setFilter(key)}
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: '3px',
              padding: '2px 5px',
              borderRadius: '4px',
              border: 'none',
              cursor: 'pointer',
              fontSize: '9px',
              fontFamily: 'system-ui',
              fontWeight: 600,
              background: filter === key ? 'rgba(148,163,184,0.15)' : 'transparent',
              color: filter === key ? '#e2e8f0' : '#64748b',
              letterSpacing: '0.03em',
            }}
          >
            {label}
            {count > 0 && (
              <span style={{
                background: key === 'error' ? '#ef4444' : key === 'warn' ? '#f59e0b' : 'rgba(148,163,184,0.3)',
                color: key === 'error' || key === 'warn' ? '#fff' : '#94a3b8',
                borderRadius: '8px',
                padding: '0 4px',
                fontSize: '8px',
                fontWeight: 700,
                minWidth: '14px',
                textAlign: 'center',
              }}>
                {count}
              </span>
            )}
          </button>
        ))}
        <div style={{ width: '1px', height: '14px', background: 'rgba(255,255,255,0.1)', margin: '0 2px', flexShrink: 0 }} />
        {/* Close */}
        <button
          onClick={handleClose}
          title="Close console (Ctrl+Shift+J)"
          style={{
            width: '22px', height: '22px', border: 'none', borderRadius: '4px',
            background: 'transparent', cursor: 'pointer', color: '#64748b',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            flexShrink: 0,
          }}
          onMouseEnter={(e) => { e.currentTarget.style.background = 'rgba(255,255,255,0.08)'; e.currentTarget.style.color = '#e2e8f0'; }}
          onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent'; e.currentTarget.style.color = '#64748b'; }}
        >
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
            <line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>
          </svg>
        </button>
      </div>

      {/* Log entries */}
      <div
        ref={scrollRef}
        onScroll={handleScroll}
        style={{ flex: 1, overflowY: 'auto', overflowX: 'hidden', display: 'flex', flexDirection: 'column' }}
      >
        {filtered.length === 0 ? (
          <div style={{
            display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center',
            flex: 1, gap: '8px', color: '#334155',
          }}>
            <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
              <polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/>
            </svg>
            <span style={{ fontFamily: 'system-ui', fontSize: '11px' }}>No console output</span>
          </div>
        ) : (
          filtered.map((entry, i) => {
            const prev = filtered[i - 1];
            const isNavBreak = entry.nav_marker;
            return (
              <React.Fragment key={i}>
                {isNavBreak && <NavMarker label="Page navigated" />}
                <ConsoleRow entry={entry} onClick={() => setModal(entry)} />
              </React.Fragment>
            );
          })
        )}
        <div ref={bottomRef} style={{ flexShrink: 0 }} />
      </div>

      {/* Jump to latest */}
      {!autoScroll && entries.length > 0 && (
        <button
          onClick={() => {
            setAutoScroll(true);
            bottomRef.current?.scrollIntoView({ block: 'end' });
          }}
          style={{
            position: 'absolute',
            bottom: '10px',
            left: '50%',
            transform: 'translateX(-50%)',
            padding: '4px 12px',
            background: '#1d4ed8',
            color: '#e2e8f0',
            border: 'none',
            borderRadius: '10px',
            cursor: 'pointer',
            fontSize: '10px',
            fontFamily: 'system-ui',
            fontWeight: 600,
            boxShadow: '0 2px 8px rgba(0,0,0,0.5)',
            whiteSpace: 'nowrap',
          }}
        >
          ↓ Jump to latest
        </button>
      )}

      {/* Message modal */}
      {modal && <MessageModal entry={modal} onClose={() => setModal(null)} />}
    </div>
  );
}
