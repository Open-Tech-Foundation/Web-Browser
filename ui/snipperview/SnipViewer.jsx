import React, { useRef, useState, useEffect, useCallback } from 'react';
import { isBridgeAvailable, nativeRequest } from '../src/shared/nativeRequest';

const S = {
  root: {
    position: 'fixed',
    inset: 0,
    background: 'rgba(0,0,0,0.6)',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    cursor: 'crosshair',
    userSelect: 'none',
  },
  image: {
    maxWidth: '100%',
    maxHeight: '100%',
    objectFit: 'contain',
    pointerEvents: 'none',
    boxShadow: '0 8px 32px rgba(0,0,0,0.5)',
  },
  selection: {
    position: 'absolute',
    border: '2px dashed #FF7A00',
    background: 'rgba(255,122,0,0.15)',
    pointerEvents: 'none',
  },
  tooltip: {
    position: 'absolute',
    background: '#1e293b',
    color: '#fff',
    fontSize: '11px',
    fontWeight: 600,
    padding: '3px 8px',
    borderRadius: '4px',
    pointerEvents: 'none',
    whiteSpace: 'nowrap',
  },
  previewPanel: {
    position: 'fixed',
    top: '50%',
    left: '50%',
    transform: 'translate(-50%, -50%)',
    background: '#0f172a',
    borderRadius: '12px',
    boxShadow: '0 20px 60px rgba(0,0,0,0.6)',
    display: 'flex',
    flexDirection: 'column',
    maxWidth: '90vw',
    maxHeight: '90vh',
    overflow: 'hidden',
  },
  previewHeader: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '12px 16px',
    borderBottom: '1px solid #1e293b',
    background: '#1e293b',
  },
  previewTitle: {
    fontSize: '14px',
    fontWeight: 600,
    color: '#f1f5f9',
  },
  previewInfo: {
    fontSize: '12px',
    color: '#94a3b8',
    fontWeight: 500,
  },
  previewBody: {
    flex: 1,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    padding: '24px',
    overflow: 'auto',
    background: '#0f172a',
  },
  previewImage: {
    maxWidth: '100%',
    maxHeight: '60vh',
    objectFit: 'contain',
    borderRadius: '4px',
    boxShadow: '0 4px 16px rgba(0,0,0,0.4)',
  },
  previewFooter: {
    display: 'flex',
    gap: '8px',
    padding: '12px 16px',
    borderTop: '1px solid #1e293b',
    background: '#1e293b',
    justifyContent: 'flex-end',
  },
  btn: {
    padding: '8px 16px',
    fontSize: '13px',
    fontWeight: 600,
    border: 'none',
    borderRadius: '6px',
    cursor: 'pointer',
    transition: 'all 0.15s',
    display: 'flex',
    alignItems: 'center',
    gap: '6px',
  },
  btnPrimary: {
    background: '#FF7A00',
    color: '#fff',
  },
  btnSecondary: {
    background: '#334155',
    color: '#e2e8f0',
  },
  btnGhost: {
    background: 'transparent',
    color: '#94a3b8',
  },
  closeBtn: {
    background: 'transparent',
    border: 'none',
    color: '#94a3b8',
    cursor: 'pointer',
    padding: '4px',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    borderRadius: '4px',
    transition: 'all 0.15s',
  },
  toolbar: {
    position: 'absolute',
    top: '16px',
    left: '50%',
    transform: 'translateX(-50%)',
    display: 'flex',
    gap: '8px',
    background: '#1e293b',
    padding: '8px 12px',
    borderRadius: '8px',
    boxShadow: '0 4px 16px rgba(0,0,0,0.4)',
    zIndex: 10,
  },
  toolbarBtn: {
    padding: '6px 12px',
    fontSize: '12px',
    fontWeight: 600,
    border: 'none',
    borderRadius: '6px',
    cursor: 'pointer',
    transition: 'all 0.15s',
    display: 'flex',
    alignItems: 'center',
    gap: '6px',
    background: '#334155',
    color: '#e2e8f0',
  },
  toolbarCloseBtn: {
    padding: '6px',
    background: 'transparent',
    border: 'none',
    color: '#94a3b8',
    cursor: 'pointer',
    borderRadius: '6px',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    transition: 'all 0.15s',
  },
};

const SnipViewer = () => {
  const [imageData, setImageData] = useState(null);
  const [selection, setSelection] = useState(null);
  const [isDragging, setIsDragging] = useState(false);
  const [showPreview, setShowPreview] = useState(false);
  const [croppedDataUrl, setCroppedDataUrl] = useState(null);
  const [cropDimensions, setCropDimensions] = useState(null);
  const [copied, setCopied] = useState(false);
  const containerRef = useRef(null);
  const imageRef = useRef(null);
  const startRef = useRef(null);

  useEffect(() => {
    window.__otfSetSnipImage = (data) => {
      if (data && data.data) {
        setImageData(data.data);
      } else {
        setImageData(null);
      }
      setSelection(null);
      setShowPreview(false);
      setCroppedDataUrl(null);
      setCropDimensions(null);
      setCopied(false);
    };
    return () => { delete window.__otfSetSnipImage; };
  }, []);

  useEffect(() => {
    const handleKey = (e) => {
      if (e.key === 'Escape') {
        if (showPreview) {
          setShowPreview(false);
          setSelection(null);
          setCroppedDataUrl(null);
        } else if (isBridgeAvailable()) {
          nativeRequest({ method: 'ui.snipPreview.hide' }).catch(() => {});
        }
      }
    };
    window.addEventListener('keydown', handleKey);
    return () => window.removeEventListener('keydown', handleKey);
  }, [showPreview]);

  const handleMouseDown = (e) => {
    if (showPreview || !imageData) return;
    const rect = containerRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    startRef.current = { x, y };
    setSelection({ x, y, w: 0, h: 0 });
    setIsDragging(true);
  };

  const handleMouseMove = (e) => {
    if (!isDragging || !startRef.current) return;
    const rect = containerRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    const sx = startRef.current.x;
    const sy = startRef.current.y;
    setSelection({
      x: Math.min(sx, x),
      y: Math.min(sy, y),
      w: Math.abs(x - sx),
      h: Math.abs(y - sy),
    });
  };

  const handleMouseUp = () => {
    if (!isDragging) return;
    setIsDragging(false);
    if (selection && selection.w > 5 && selection.h > 5) {
      const canvas = cropImage();
      if (canvas) {
        const dataUrl = canvas.toDataURL('image/png');
        setCroppedDataUrl(dataUrl);
        setCropDimensions({ width: canvas.width, height: canvas.height });
        setShowPreview(true);
      }
    } else {
      setSelection(null);
    }
  };

  const cropImage = useCallback(() => {
    if (!selection || !imageRef.current) return null;
    const img = imageRef.current;
    const imgRect = img.getBoundingClientRect();
    const naturalW = img.naturalWidth;
    const naturalH = img.naturalHeight;
    const scaleX = naturalW / imgRect.width;
    const scaleY = naturalH / imgRect.height;

    const containerRect = containerRef.current.getBoundingClientRect();
    const imgOffsetX = imgRect.left - containerRect.left;
    const imgOffsetY = imgRect.top - containerRect.top;

    const cropX = Math.round((selection.x - imgOffsetX) * scaleX);
    const cropY = Math.round((selection.y - imgOffsetY) * scaleY);
    const cropW = Math.round(selection.w * scaleX);
    const cropH = Math.round(selection.h * scaleY);

    const canvas = document.createElement('canvas');
    canvas.width = cropW;
    canvas.height = cropH;
    const ctx = canvas.getContext('2d');
    ctx.drawImage(img, cropX, cropY, cropW, cropH, 0, 0, cropW, cropH);
    return canvas;
  }, [selection]);

  const handleCopy = async () => {
    if (!croppedDataUrl) return;
    try {
      const response = await fetch(croppedDataUrl);
      const blob = await response.blob();
      await navigator.clipboard.write([
        new ClipboardItem({ 'image/png': blob }),
      ]);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
      
      nativeRequest({
        method: 'ui.toast.show',
        params: { icon: 'copy', message: 'Image copied' },
      }).catch(() => {});
    } catch (err) {
      console.error('Copy failed:', err);
    }
  };

  const handleSave = () => {
    if (!croppedDataUrl) return;
    const a = document.createElement('a');
    a.href = croppedDataUrl;
    a.download = `snip-${Date.now()}.png`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    
    nativeRequest({
      method: 'ui.toast.show',
      params: { icon: 'save', message: 'Image saved' },
    }).catch(() => {});
  };

  const handleReselect = () => {
    setShowPreview(false);
    setSelection(null);
    setCroppedDataUrl(null);
    setCropDimensions(null);
  };

  const handleFullPage = () => {
    if (!imageRef.current) return;
    const img = imageRef.current;
    const imgRect = img.getBoundingClientRect();
    const containerRect = containerRef.current.getBoundingClientRect();
    
    const imgOffsetX = imgRect.left - containerRect.left;
    const imgOffsetY = imgRect.top - containerRect.top;
    
    setSelection({
      x: imgOffsetX,
      y: imgOffsetY,
      w: imgRect.width,
      h: imgRect.height,
    });
    
    setTimeout(() => {
      const canvas = document.createElement('canvas');
      canvas.width = img.naturalWidth;
      canvas.height = img.naturalHeight;
      const ctx = canvas.getContext('2d');
      ctx.drawImage(img, 0, 0);
      const dataUrl = canvas.toDataURL('image/png');
      setCroppedDataUrl(dataUrl);
      setCropDimensions({ width: img.naturalWidth, height: img.naturalHeight });
      setShowPreview(true);
    }, 50);
  };

  const handleClose = () => {
    if (isBridgeAvailable()) {
      nativeRequest({ method: 'ui.snipPreview.hide' }).catch(() => {});
    }
  };

  const handleBackdropClick = (e) => {
    if (e.target === containerRef.current && !showPreview) {
      handleClose();
    }
  };

  if (!imageData) return null;

  const dataUrl = `data:image/png;base64,${imageData}`;

  return (
    <>
      <div
        ref={containerRef}
        style={{
          ...S.root,
          cursor: showPreview ? 'default' : 'crosshair',
        }}
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onClick={handleBackdropClick}
      >
        {imageData && !showPreview && !isDragging && !selection && (
          <div style={S.toolbar}>
            <button
              style={S.toolbarBtn}
              onClick={handleFullPage}
              onMouseDown={(e) => e.stopPropagation()}
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <rect x="3" y="3" width="18" height="18" rx="2" ry="2"/>
              </svg>
              Full Page
            </button>
            <button
              style={S.toolbarCloseBtn}
              onClick={handleClose}
              onMouseDown={(e) => e.stopPropagation()}
              title="Close (Esc)"
            >
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <line x1="18" y1="6" x2="6" y2="18"/>
                <line x1="6" y1="6" x2="18" y2="18"/>
              </svg>
            </button>
          </div>
        )}

        <img
          ref={imageRef}
          src={dataUrl}
          style={S.image}
          draggable={false}
          alt=""
        />

        {selection && !showPreview && (
          <div
            style={{
              ...S.selection,
              left: selection.x,
              top: selection.y,
              width: selection.w,
              height: selection.h,
            }}
          />
        )}

        {selection && !showPreview && selection.w > 0 && selection.h > 0 && (
          <div
            style={{
              ...S.tooltip,
              left: selection.x + selection.w + 8,
              top: selection.y + selection.h + 8,
            }}
          >
            {Math.round(selection.w)} x {Math.round(selection.h)}
          </div>
        )}
      </div>

      {showPreview && croppedDataUrl && (
        <div style={S.previewPanel}>
          <div style={S.previewHeader}>
            <div style={S.previewTitle}>Snip Preview</div>
            {cropDimensions && (
              <div style={S.previewInfo}>
                {cropDimensions.width} × {cropDimensions.height} px
              </div>
            )}
            <button
              style={S.closeBtn}
              onClick={handleClose}
              title="Close (Esc)"
            >
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <line x1="18" y1="6" x2="6" y2="18"/>
                <line x1="6" y1="6" x2="18" y2="18"/>
              </svg>
            </button>
          </div>

          <div style={S.previewBody}>
            <img
              src={croppedDataUrl}
              style={S.previewImage}
              alt="Cropped snip"
              draggable={false}
            />
          </div>

          <div style={S.previewFooter}>
            <button
              style={{ ...S.btn, ...S.btnGhost }}
              onClick={handleReselect}
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <polyline points="1 4 1 10 7 10"/>
                <path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/>
              </svg>
              Reselect
            </button>
            <button
              style={{ ...S.btn, ...S.btnSecondary }}
              onClick={handleSave}
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
                <polyline points="7 10 12 15 17 10"/>
                <line x1="12" y1="15" x2="12" y2="3"/>
              </svg>
              Save
            </button>
            <button
              style={{ ...S.btn, ...S.btnPrimary }}
              onClick={handleCopy}
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <rect x="9" y="9" width="13" height="13" rx="2" ry="2"/>
                <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>
              </svg>
              {copied ? 'Copied!' : 'Copy'}
            </button>
          </div>
        </div>
      )}
    </>
  );
};

export default SnipViewer;
