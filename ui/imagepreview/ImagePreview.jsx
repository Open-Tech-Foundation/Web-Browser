import React, { useEffect, useState, useRef } from 'react';

const ImagePreview = () => {
  const [url, setUrl] = useState('');
  const [scale, setScale] = useState(1);
  const [rotation, setRotation] = useState(0);
  const [isDragging, setIsDragging] = useState(false);
  const [position, setPosition] = useState({ x: 0, y: 0 });
  const dragStart = useRef({ x: 0, y: 0 });

  useEffect(() => {
    if (!window.cefQuery) return;

    const sub = window.cefQuery({
      request: 'image-preview-subscribe',
      persistent: true,
      onSuccess: (json) => {
        try {
          const ev = JSON.parse(json);
          if (ev.key === 'load-image') {
            setUrl(ev.url);
            setScale(1);
            setRotation(0);
            setPosition({ x: 0, y: 0 });
          }
        } catch (_) {}
      },
    });

    const onKeyDown = (event) => {
      if (event.key === 'Escape' && window.cefQuery) {
        event.preventDefault();
        window.cefQuery({ request: 'hide-imagepreview' });
      }
    };
    window.addEventListener('keydown', onKeyDown);

    return () => {
      window.removeEventListener('keydown', onKeyDown);
      if (sub && typeof sub.cancel === 'function') sub.cancel();
    };
  }, []);

  const handleClose = () => {
    setUrl('');
    if (window.cefQuery) {
      window.cefQuery({ request: 'hide-imagepreview' });
    }
  };

  const zoomIn = () => setScale(s => Math.min(s * 1.5, 10));
  const zoomOut = () => setScale(s => Math.max(s / 1.5, 0.1));
  const rotateLeft = () => setRotation(r => r - 90);
  const rotateRight = () => setRotation(r => r + 90);
  const reset = () => { setScale(1); setRotation(0); setPosition({ x: 0, y: 0 }); };

  const handleWheel = (e) => {
    if (e.deltaY < 0) zoomIn();
    else zoomOut();
  };

  const handleMouseDown = (e) => {
    e.preventDefault();
    setIsDragging(true);
    dragStart.current = { x: e.clientX - position.x, y: e.clientY - position.y };
  };

  const handleMouseMove = (e) => {
    if (!isDragging) return;
    setPosition({
      x: e.clientX - dragStart.current.x,
      y: e.clientY - dragStart.current.y
    });
  };

  const handleMouseUp = () => {
    setIsDragging(false);
  };

  if (!url) return null;

  return (
    <div 
      style={{
        position: 'fixed', inset: 0,
        backgroundColor: 'rgba(0, 0, 0, 0.85)',
        display: 'flex', flexDirection: 'column',
        backdropFilter: 'blur(4px)',
      }}
      onWheel={handleWheel}
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
      onMouseLeave={handleMouseUp}
    >
      <style>{`
        .btn-action {
          padding: 8px 16px;
          background: rgba(255,255,255,0.08);
          border: 1px solid rgba(255,255,255,0.15);
          color: rgba(255,255,255,0.85);
          border-radius: 6px;
          cursor: pointer;
          font-weight: 600;
          font-size: 13px;
          display: flex;
          align-items: center;
          justify-content: center;
          gap: 6px;
          transition: background 0.15s ease, border-color 0.15s ease, color 0.15s ease;
        }
        .btn-action:hover {
          background: rgba(255,255,255,0.18);
          border-color: rgba(255,255,255,0.3);
          color: white;
        }
        .btn-close {
          padding: 8px 12px;
          background: rgba(239, 68, 68, 0.12);
          border: 1px solid rgba(239, 68, 68, 0.25);
          color: #fca5a5;
          border-radius: 6px;
          cursor: pointer;
          display: flex;
          align-items: center;
          justify-content: center;
          transition: background 0.15s ease, border-color 0.15s ease, color 0.15s ease;
        }
        .btn-close:hover {
          background: rgba(239, 68, 68, 0.3);
          border-color: rgba(239, 68, 68, 0.5);
          color: white;
        }
      `}</style>
      <div style={{
        padding: '16px', display: 'flex', justifyContent: 'center', gap: '12px',
        backgroundColor: 'rgba(0,0,0,0.5)', zIndex: 10, alignItems: 'center'
      }}>
        <button onClick={zoomIn} className="btn-action" title="Zoom In">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line><line x1="11" y1="8" x2="11" y2="14"></line><line x1="8" y1="11" x2="14" y2="11"></line></svg>
          Zoom In
        </button>
        <button onClick={zoomOut} className="btn-action" title="Zoom Out">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line><line x1="8" y1="11" x2="14" y2="11"></line></svg>
          Zoom Out
        </button>
        <button onClick={rotateLeft} className="btn-action" title="Rotate Left">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"></path><path d="M3 3v5h5"></path></svg>
          Rotate Left
        </button>
        <button onClick={rotateRight} className="btn-action" title="Rotate Right">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 12a9 9 0 1 1-9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
          Rotate Right
        </button>
        <button onClick={reset} className="btn-action" title="Reset view">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"/><path d="M3 21v-5h5"/></svg>
          Reset
        </button>
        <div style={{flex: 1}}></div>
        <button onClick={handleClose} className="btn-close" title="Close Preview">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
        </button>
      </div>
      
      <div 
        style={{ flex: 1, overflow: 'hidden', display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: isDragging ? 'grabbing' : 'grab' }}
        onMouseDown={handleMouseDown}
      >
        <img 
          src={url} 
          alt="Preview" 
          style={{
            transform: `translate(${position.x}px, ${position.y}px) scale(${scale}) rotate(${rotation}deg)`,
            transition: isDragging ? 'none' : 'transform 0.2s ease-out',
            maxWidth: '100%',
            maxHeight: '100%',
            objectFit: 'contain',
            pointerEvents: 'none'
          }} 
          draggable="false"
        />
      </div>
    </div>
  );
};

const btnStyle = {};

export default ImagePreview;
