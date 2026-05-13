import React, { useEffect, useMemo, useState } from 'react';

const S = {
  panel: {
    display: 'flex',
    flexDirection: 'column',
    height: 360,
    background: 'var(--bg, #fff)',
    border: '1px solid var(--accent, #FF7A00)',
    borderRadius: 12,
    boxShadow: '0 14px 34px rgba(15,23,42,0.18)',
    overflow: 'hidden',
    fontFamily: "'Inter', system-ui, sans-serif",
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '12px 14px 10px',
    borderBottom: '1px solid var(--sep, #e2e8f0)',
    color: 'var(--fg, #0f172a)',
  },
  list: {
    flex: 1,
    overflowY: 'auto',
    padding: 10,
    background: 'var(--surface, #f8fafc)',
  },
  footer: {
    display: 'flex',
    justifyContent: 'space-between',
    gap: 8,
    padding: 10,
    borderTop: '1px solid var(--sep, #e2e8f0)',
    background: 'var(--bg, #fff)',
  },
  card: {
    display: 'flex',
    flexDirection: 'column',
    gap: 8,
    padding: 12,
    background: 'var(--bg, #fff)',
    border: '1px solid var(--sep, #e2e8f0)',
    borderRadius: 10,
    marginBottom: 10,
  },
  title: {
    fontSize: 13,
    fontWeight: 700,
    color: 'var(--fg, #0f172a)',
    whiteSpace: 'nowrap',
    overflow: 'hidden',
    textOverflow: 'ellipsis',
  },
  meta: {
    fontSize: 11,
    color: 'var(--muted, #64748b)',
    display: 'flex',
    gap: 8,
    flexWrap: 'wrap',
  },
  progressTrack: {
    width: '100%',
    height: 6,
    borderRadius: 999,
    background: 'rgba(148,163,184,0.25)',
    overflow: 'hidden',
  },
  progressFill: {
    height: '100%',
    borderRadius: 999,
    background: 'var(--accent, #FF7A00)',
  },
  actions: {
    display: 'flex',
    gap: 6,
    flexWrap: 'wrap',
  },
  button: {
    border: '1px solid var(--sep, #e2e8f0)',
    borderRadius: 8,
    padding: '6px 10px',
    background: 'transparent',
    color: 'var(--fg, #0f172a)',
    fontSize: 11,
    fontWeight: 700,
    cursor: 'pointer',
  },
};

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
}

function formatProgress(item) {
  if (item.isComplete || item.isCanceled || item.isInterrupted) return '';
  if (item.percent >= 0) return `${item.percent}%`;
  if (item.totalBytes > 0) return `${formatBytes(item.receivedBytes)} / ${formatBytes(item.totalBytes)}`;
  return formatBytes(item.receivedBytes);
}

function formatFileSize(item) {
  if (item.totalBytes > 0) return formatBytes(item.totalBytes);
  if (item.receivedBytes > 0) return formatBytes(item.receivedBytes);
  return '';
}

function shouldShowProgress(item) {
  return item.isInProgress || item.isPaused || (!item.isComplete && !item.isCanceled && !item.isInterrupted);
}

function statusChip(item) {
  if (item.isComplete) {
    return (
      <span style={{ color: '#10b981', display: 'flex', alignItems: 'center', gap: 4, fontWeight: 700 }}>
        <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3.2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="12" cy="12" r="10" />
          <polyline points="8 12.5 11 15.5 16 9.5" />
        </svg>
        Completed
      </span>
    );
  }
  if (item.isCanceled) {
    return (
      <span style={{ color: '#64748b', display: 'flex', alignItems: 'center', gap: 4 }}>
        <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.8" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="12" cy="12" r="10" />
          <path d="M9 9l6 6M15 9l-6 6" />
        </svg>
        Cancelled
      </span>
    );
  }
  if (item.isInterrupted) {
    return (
      <span style={{ color: '#ef4444', display: 'flex', alignItems: 'center', gap: 4, fontWeight: 700 }}>
        <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.8" strokeLinecap="round" strokeLinejoin="round">
          <path d="M12 3l10 18H2L12 3z" />
          <path d="M12 9v5" />
          <path d="M12 17h.01" />
        </svg>
        Failed
      </span>
    );
  }
  return <span>{item.status}</span>;
}

const DownloadsBar = () => {
  const [downloads, setDownloads] = useState([]);

  useEffect(() => {
    if (!window.cefQuery) return;

    const sync = (payload) => {
      setDownloads(Array.isArray(payload) ? payload : []);
    };

    window.cefQuery({
      request: 'get-downloads',
      onSuccess: (json) => {
        try { sync(JSON.parse(json)); } catch (_) {}
      },
    });

    const sub = window.cefQuery({
      request: 'downloads-subscribe',
      persistent: true,
      onSuccess: (json) => {
        try {
          const ev = JSON.parse(json);
          if (ev.key === 'downloads-update') {
            sync(ev.downloads);
          }
        } catch (_) {}
      },
    });

    const onKeyDown = (event) => {
      if (event.key === 'Escape' && window.cefQuery) {
        event.preventDefault();
        window.cefQuery({ request: 'hide-downloadsbar' });
      }
    };
    window.addEventListener('keydown', onKeyDown);

    return () => {
      window.removeEventListener('keydown', onKeyDown);
      if (sub && typeof sub.cancel === 'function') sub.cancel();
    };
  }, []);

  const sortedDownloads = useMemo(() => {
    return [...downloads].sort((a, b) => {
      const aActive = a.isInProgress;
      const bActive = b.isInProgress;
      if (aActive !== bActive) return aActive ? -1 : 1;
      return b.id - a.id;
    });
  }, [downloads]);

  const latestDownloads = useMemo(() => sortedDownloads.slice(0, 5), [sortedDownloads]);

  const run = (request) => window.cefQuery?.({ request });

  return (
    <div style={S.panel}>
      <div style={S.header}>
        <div style={{ fontSize: 13, fontWeight: 800 }}>Downloads</div>
        <div style={{ fontSize: 11, color: 'var(--muted, #64748b)' }}>
          Latest 5
        </div>
      </div>
      <div style={S.list}>
        {latestDownloads.length === 0 ? (
          <div style={{ color: 'var(--muted, #64748b)', fontSize: 12, padding: 12 }}>
            No downloads yet.
          </div>
        ) : latestDownloads.map((item) => (
          <div key={item.id} style={S.card}>
            <div style={S.title}>{item.suggestedName || 'download'}</div>
            <div style={S.meta}>
              {statusChip(item)}
              {formatFileSize(item) && <span>{formatFileSize(item)}</span>}
              {!item.isComplete && !item.isCanceled && !item.isInterrupted && <span>{formatProgress(item)}</span>}
              {item.speedBytesPerSec > 0 && <span>{formatBytes(item.speedBytesPerSec)}/s</span>}
            </div>
            {shouldShowProgress(item) && (
              <div style={S.progressTrack}>
                <div style={{ ...S.progressFill, width: `${Math.max(0, item.percent >= 0 ? item.percent : 0)}%` }} />
              </div>
            )}
            <div style={S.actions}>
              {item.canCancel && <button style={S.button} onClick={() => run(`cancel-download:${item.id}`)}>Cancel</button>}
              {item.canPause && <button style={S.button} onClick={() => run(`pause-download:${item.id}`)}>Pause</button>}
              {item.canResume && <button style={S.button} onClick={() => run(`resume-download:${item.id}`)}>Resume</button>}
              {item.canOpen && <button style={S.button} onClick={() => run(`open-download:${item.id}`)}>Open</button>}
              {item.canShowInFolder && <button style={S.button} onClick={() => run(`show-download-in-folder:${item.id}`)}>Show in Folder</button>}
            </div>
          </div>
        ))}
      </div>
      <div style={S.footer}>
        <button style={S.button} onClick={() => run('clear-finished-downloads')}>Clear Finished</button>
        <button style={S.button} onClick={() => run('open-downloads-page')}>Show All Downloads</button>
      </div>
    </div>
  );
};

export default DownloadsBar;
