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
  },
};

const Toast = () => {
  const [msg, setMsg] = React.useState('');

  React.useEffect(() => {
    window.__otfSetToastMessage = (value) => {
      setMsg(value || '');
    };
    return () => { delete window.__otfSetToastMessage; };
  }, []);

  if (!msg) return null;

  return (
    <div style={S.root}>
      <span style={S.pill}>
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" style={{flexShrink: 0}}>
          <path d="M20 6L9 17l-5-5"/>
        </svg>
        {msg}
      </span>
    </div>
  );
};

export default Toast;