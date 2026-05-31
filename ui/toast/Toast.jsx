import React from 'react';

const S = {
  root: {
    width: '100%',
    height: '100%',
    display: 'flex',
    alignItems: 'flex-start',
    justifyContent: 'center',
    paddingTop: '4px',
    pointerEvents: 'none',
  },
  pill: {
    display: 'inline-flex',
    alignItems: 'center',
    gap: '6px',
    padding: '6px 14px',
    background: '#FF7A00',
    border: 'none',
    borderRadius: '20px',
    fontSize: '13px',
    fontWeight: 600,
    color: 'white',
    boxShadow: '0 4px 12px rgba(255,122,0,0.35)',
    whiteSpace: 'nowrap',
  },
};

const icons = {
  check: (
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" style={{flexShrink: 0}}>
      <path d="M20 6L9 17l-5-5"/>
    </svg>
  ),
  save: (
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" style={{flexShrink: 0}}>
      <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/>
      <polyline points="17 21 17 13 7 13 7 21"/>
      <polyline points="7 3 7 8 15 8"/>
    </svg>
  ),
  copy: (
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" style={{flexShrink: 0}}>
      <rect x="9" y="9" width="13" height="13" rx="2" ry="2"/>
      <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>
    </svg>
  ),
  error: (
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" style={{flexShrink: 0}}>
      <circle cx="12" cy="12" r="10"/>
      <line x1="15" y1="9" x2="9" y2="15"/>
      <line x1="9" y1="9" x2="15" y2="15"/>
    </svg>
  ),
  info: (
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" style={{flexShrink: 0}}>
      <circle cx="12" cy="12" r="10"/>
      <line x1="12" y1="16" x2="12" y2="12"/>
      <line x1="12" y1="8" x2="12.01" y2="8"/>
    </svg>
  ),
  fullscreen: (
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" style={{flexShrink: 0}}>
      <path d="M8 3H5a2 2 0 0 0-2 2v3"/>
      <path d="M21 8V5a2 2 0 0 0-2-2h-3"/>
      <path d="M16 21h3a2 2 0 0 0 2-2v-3"/>
      <path d="M3 16v3a2 2 0 0 0 2 2h3"/>
    </svg>
  ),
};

const Toast = () => {
  const [toast, setToast] = React.useState({ icon: 'check', msg: '' });

  React.useEffect(() => {
    window.__otfSetToastMessage = (icon, msg) => {
      setToast({ icon: icon || 'check', msg: msg || '' });
    };
    return () => { delete window.__otfSetToastMessage; };
  }, []);

  if (!toast.msg) return null;

  return (
    <div style={S.root}>
      <span style={S.pill}>
        {icons[toast.icon] || icons.check}
        {toast.msg}
      </span>
    </div>
  );
};

export default Toast;