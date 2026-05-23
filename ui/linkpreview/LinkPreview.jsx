import React, { useState, useEffect } from 'react';

const S = {
  root: {
    width: '100%',
    height: '100%',
    display: 'flex',
    alignItems: 'flex-end',
    padding: '0 0 0 0',
    pointerEvents: 'none',
  },
  chip: {
    display: 'inline-block',
    maxWidth: '100%',
    padding: '3px 10px 4px',
    background: '#ffffff',
    border: '1px solid #dadce0',
    borderRadius: '0 6px 0 0',
    fontFamily: 'system-ui, -apple-system, sans-serif',
    fontSize: 12,
    lineHeight: '18px',
    color: '#202124',
    overflow: 'hidden',
    whiteSpace: 'nowrap',
    textOverflow: 'ellipsis',
    boxShadow: '0 1px 4px rgba(0,0,0,0.12)',
  },
};

const LinkPreview = () => {
  const [url, setUrl] = useState('');

  useEffect(() => {
    window.__otfSetLinkPreview = (value) => setUrl(value || '');
    return () => { delete window.__otfSetLinkPreview; };
  }, []);

  if (!url) return null;

  return (
    <div style={S.root}>
      <span style={S.chip} title={url}>{url}</span>
    </div>
  );
};

export default LinkPreview;
