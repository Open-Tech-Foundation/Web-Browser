import React, { useEffect, useState } from 'react';
import { nativeRequest } from '../src/shared/nativeRequest';

const S = {
  wrapper: {
    padding: '3px 6px',
    width: '100%',
    height: '100%',
    boxSizing: 'border-box',
    background: 'transparent',
  },
  bar: {
    display: 'flex',
    alignItems: 'center',
    gap: 4,
    height: '100%',
    padding: '2px 4px',
    background: 'var(--bg, #ffffff)',
    border: '1px solid var(--sep)',
    borderRadius: 16,
    boxShadow: '0 8px 20px rgba(15,23,42,0.12)',
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
      request: JSON.stringify({
        id: `zoombar-subscription-${Date.now()}`,
        method: 'ui.zoomBar.subscribe',
        params: {},
      }),
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
    const onBlur = () => {
      nativeRequest({ method: 'ui.zoomBar.hide' }).catch(() => {});
    };
    const onKeyDown = (event) => {
      if (event.key === 'Escape') {
        event.preventDefault();
        nativeRequest({ method: 'ui.zoomBar.hide' }).catch(() => {});
      }
    };

    window.addEventListener('blur', onBlur);
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('blur', onBlur);
      window.removeEventListener('keydown', onKeyDown);
    };
  }, []);

  const act = (action) => {
    if (tabId < 0) return;
    const methodByAction = {
      'zoom-in': 'tabs.zoomIn',
      'zoom-out': 'tabs.zoomOut',
      'zoom-reset': 'tabs.zoomReset',
    };
    nativeRequest({
      method: methodByAction[action],
      params: { tabId },
    }).catch(() => {});
  };

  return (
    <div style={S.wrapper}>
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
        <button style={S.button} onClick={() => nativeRequest({ method: 'ui.fullscreen.toggle' }).catch(() => {})} title="Fullscreen">
          <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.6" strokeLinecap="round" strokeLinejoin="round"><path d="M8 3H5a2 2 0 0 0-2 2v3"/><path d="M21 8V5a2 2 0 0 0-2-2h-3"/><path d="M16 21h3a2 2 0 0 0 2-2v-3"/><path d="M3 16v3a2 2 0 0 0 2 2h3"/></svg>
        </button>
      </div>
    </div>
  );
};

export default ZoomBar;
