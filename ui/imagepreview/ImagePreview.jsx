import React, { useEffect, useState, useRef } from 'react';

const ImagePreview = () => {
  const [url, setUrl] = useState('');
  const [scale, setScale] = useState(1);
  const [rotation, setRotation] = useState(0);
  const [isDragging, setIsDragging] = useState(false);
  const [position, setPosition] = useState({ x: 0, y: 0 });
  const [naturalWidth, setNaturalWidth] = useState(0);
  const [naturalHeight, setNaturalHeight] = useState(0);
  const [showInfo, setShowInfo] = useState(true);
  const [toast, setToast] = useState('');

  const dragStart = useRef({ x: 0, y: 0 });
  const imgRef = useRef(null);

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
            setNaturalWidth(0);
            setNaturalHeight(0);
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

  const handleImageLoad = (e) => {
    setNaturalWidth(e.target.naturalWidth);
    setNaturalHeight(e.target.naturalHeight);
  };

  const handleDoubleClick = () => {
    if (scale !== 1) {
      setScale(1);
      setPosition({ x: 0, y: 0 });
    } else if (imgRef.current && naturalWidth) {
      const displayWidth = imgRef.current.clientWidth;
      const fitScale = naturalWidth / displayWidth;
      setScale(fitScale > 1 ? fitScale : 1.5);
      setPosition({ x: 0, y: 0 });
    }
  };

  const showToast = (msg) => {
    setToast(msg);
    setTimeout(() => setToast(''), 2200);
  };

  const handleSave = () => {
    if (window.cefQuery) {
      window.cefQuery({ request: 'download-image:' + url });
      showToast("Starting image download...");
    }
  };

  const handleCopyImage = async () => {
    try {
      const response = await fetch(url);
      const blob = await response.blob();
      await navigator.clipboard.write([
        new ClipboardItem({ [blob.type]: blob })
      ]);
      showToast("Copied image to clipboard!");
    } catch (err) {
      navigator.clipboard.writeText(url);
      showToast("Copied image URL to clipboard!");
    }
  };

  const handleCopyUrl = () => {
    navigator.clipboard.writeText(url);
    showToast("Copied URL to clipboard!");
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
          padding: 8px 14px;
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
        @keyframes fadeIn {
          from { opacity: 0; transform: translate(-50%, 10px); }
          to { opacity: 1; transform: translate(-50%, 0); }
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

        <div style={{ width: '1px', height: '20px', backgroundColor: 'rgba(255,255,255,0.15)', margin: '0 4px' }}></div>

        <button onClick={handleSave} className="btn-action" title="Download Image">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
          Save
        </button>
        <button onClick={handleCopyImage} className="btn-action" title="Copy Image to Clipboard">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
          Copy Image
        </button>
        <button onClick={handleCopyUrl} className="btn-action" title="Copy Image URL">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"></path><path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"></path></svg>
          Copy URL
        </button>

        <div style={{flex: 1}}></div>
        <button onClick={handleClose} className="btn-close" title="Close Preview">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
        </button>
      </div>
      
      <div 
        style={{ flex: 1, overflow: 'hidden', display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: isDragging ? 'grabbing' : 'grab' }}
        onMouseDown={handleMouseDown}
        onDoubleClick={handleDoubleClick}
      >
        <img 
          ref={imgRef}
          src={url} 
          alt="Preview" 
          onLoad={handleImageLoad}
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

      {/* Metadata Panel */}
      {showInfo && naturalWidth > 0 && (
        <div style={infoPanelStyle}>
          <div style={{ fontWeight: '700', marginBottom: '6px', fontSize: '12px', color: '#fff', display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '8px' }}>
            <span>Image Information</span>
            <button onClick={() => setShowInfo(false)} style={infoCloseBtnStyle} title="Hide info panel">×</button>
          </div>
          <div style={{ fontSize: '11px', color: 'rgba(255,255,255,0.7)', lineHeight: '1.5', wordBreak: 'break-all' }}>
            <strong>Resolution:</strong> {naturalWidth} × {naturalHeight} px<br />
            <strong>Format:</strong> {url.split('.').pop().split('?')[0].toUpperCase()}<br />
            <strong>Source:</strong> {url.length > 45 ? url.substring(0, 45) + '...' : url}
          </div>
        </div>
      )}
      {!showInfo && naturalWidth > 0 && (
        <button onClick={() => setShowInfo(true)} style={infoOpenBtnStyle} title="Show image info">
          <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="16" x2="12" y2="12"></line><line x1="12" y1="8" x2="12.01" y2="8"></line></svg>
          Info
        </button>
      )}

      {/* Toast Notification */}
      {toast && (
        <div style={toastStyle}>
          {toast}
        </div>
      )}
    </div>
  );
};

const infoPanelStyle = {
  position: 'absolute',
  bottom: '16px',
  left: '16px',
  background: 'rgba(0, 0, 0, 0.75)',
  border: '1px solid rgba(255, 255, 255, 0.12)',
  padding: '12px 14px',
  borderRadius: '8px',
  maxWidth: '300px',
  zIndex: 20,
  backdropFilter: 'blur(10px)',
  color: 'white',
  boxShadow: '0 4px 12px rgba(0,0,0,0.5)',
};

const infoCloseBtnStyle = {
  background: 'none',
  border: 'none',
  color: 'rgba(255,255,255,0.5)',
  cursor: 'pointer',
  fontSize: '16px',
  lineHeight: 1,
  padding: 0,
  marginLeft: 'auto',
};

const infoOpenBtnStyle = {
  position: 'absolute',
  bottom: '16px',
  left: '16px',
  background: 'rgba(0, 0, 0, 0.75)',
  border: '1px solid rgba(255, 255, 255, 0.12)',
  color: 'white',
  padding: '6px 12px',
  borderRadius: '20px',
  cursor: 'pointer',
  fontSize: '11px',
  fontWeight: '600',
  display: 'flex',
  alignItems: 'center',
  gap: '4px',
  zIndex: 20,
  backdropFilter: 'blur(10px)',
  boxShadow: '0 4px 12px rgba(0,0,0,0.5)',
  transition: 'background 0.2s',
};

const toastStyle = {
  position: 'fixed',
  bottom: '24px',
  left: '50%',
  transform: 'translateX(-50%)',
  background: 'rgba(0,0,0,0.85)',
  border: '1px solid rgba(255,255,255,0.15)',
  color: 'white',
  padding: '8px 16px',
  borderRadius: '20px',
  fontSize: '13px',
  zIndex: 100,
  backdropFilter: 'blur(8px)',
  boxShadow: '0 4px 12px rgba(0,0,0,0.4)',
  animation: 'fadeIn 0.2s ease-out',
};

const btnStyle = {};

export default ImagePreview;
