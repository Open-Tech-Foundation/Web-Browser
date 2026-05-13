import React, { useEffect, useState } from 'react';

const S = {
  bar: {
    display: 'flex',
    alignItems: 'center',
    gap: 4,
    height: 40,
    padding: '4px',
    background: 'var(--bg, #ffffff)',
    border: '1px solid var(--accent, #FF7A00)',
    borderRadius: 10,
    boxShadow: '0 8px 20px rgba(15,23,42,0.18), 0 0 0 1px rgba(255,122,0,0.14)',
    fontFamily: "'Inter', system-ui, sans-serif",
  },
  button: {
    width: 32,
    height: 30,
    border: 'none',
    borderRadius: 8,
    background: 'transparent',
    color: 'var(--fg, #0f172a)',
    cursor: 'pointer',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
  },
  reset: {
    minWidth: 54,
    height: 30,
    border: 'none',
    borderRadius: 8,
    background: 'transparent',
    color: 'var(--fg, #0f172a)',
    fontSize: 12,
    fontWeight: 700,
    cursor: 'pointer',
    padding: '0 8px',
  },
};

const ZoomBar = () => {
  const [tabId, setTabId] = useState(-1);
  const [zoomPercent, setZoomPercent] = useState(100);

  useEffect(() => {
    if (!window.cefQuery) return;
    const sub = window.cefQuery({
      request: 'zoombar-subscribe',
      persistent: true,
      onSuccess: (json) => {
        try {
          const ev = JSON.parse(json);
          if (ev.key === 'zoom-restore') {
            setTabId(ev.tabId ?? -1);
            setZoomPercent(ev.zoomPercent ?? 100);
          }
        } catch (_) {}
      },
    });
    return () => {
      if (sub && typeof sub.cancel === 'function') sub.cancel();
    };
  }, []);

  useEffect(() => {
    const onKeyDown = (event) => {
      if (event.key === 'Escape' && window.cefQuery) {
        event.preventDefault();
        window.cefQuery({ request: 'hide-zoombar' });
      }
    };

    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  const act = (action) => {
    if (!window.cefQuery || tabId < 0) return;
    window.cefQuery({ request: `${action}:${tabId}` });
  };

  return (
    <div style={S.bar}>
      <button style={S.button} onClick={() => act('zoom-out')} title="Zoom out">
        <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.6" strokeLinecap="round" strokeLinejoin="round"><path d="M5 12h14"/></svg>
      </button>
      <button style={S.reset} onClick={() => act('zoom-reset')} title="Reset zoom">
        {zoomPercent}%
      </button>
      <button style={S.button} onClick={() => act('zoom-in')} title="Zoom in">
        <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.6" strokeLinecap="round" strokeLinejoin="round"><path d="M12 5v14"/><path d="M5 12h14"/></svg>
      </button>
    </div>
  );
};

export default ZoomBar;
