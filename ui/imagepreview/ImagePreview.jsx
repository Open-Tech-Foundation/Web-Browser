import React, { useEffect, useState, useRef } from 'react';

const ImagePreview = () => {
  const [url, setUrl] = useState('');
  const [displayUrl, setDisplayUrl] = useState('');
  const [scale, setScale] = useState(1);
  const [rotation, setRotation] = useState(0);
  const [isDragging, setIsDragging] = useState(false);
  const [position, setPosition] = useState({ x: 0, y: 0 });
  const [naturalWidth, setNaturalWidth] = useState(0);
  const [naturalHeight, setNaturalHeight] = useState(0);
  const [showInfo, setShowInfo] = useState(true);
  const [toast, setToast] = useState('');
  const [fileSize, setFileSize] = useState('');
  const [formatLabel, setFormatLabel] = useState('');
  const [isTiffPreview, setIsTiffPreview] = useState(false);
  const [pageCount, setPageCount] = useState(1);
  const [currentPage, setCurrentPage] = useState(0);
  const [decodeNonce, setDecodeNonce] = useState(0);
  const [isDecoding, setIsDecoding] = useState(false);
  const [previewError, setPreviewError] = useState('');
  const [downloadProgress, setDownloadProgress] = useState(null);

  const dragStart = useRef({ x: 0, y: 0 });
  const imgRef = useRef(null);
  const hasSnapshotRef = useRef(false);
  const hasBackendInfoRef = useRef(false);
  const previewTabIdRef = useRef(-1);
  const decodeNonceRef = useRef(0);
  const applyLoadImageRef = useRef(null);
  // Page index the current displayUrl was decoded for. Lets the decode
  // effect skip the round-trip when the server already shipped a matching
  // data: URL (initial load and tab switches both come pre-decoded).
  const displayedPageRef = useRef(-1);

  const applyLoadedImageMeta = (ev) => {
    if (!ev || ev.key !== 'load-image') return;
    if (typeof ev.naturalWidth === 'number' && ev.naturalWidth >= 0) {
      setNaturalWidth(ev.naturalWidth);
    }
    if (typeof ev.naturalHeight === 'number' && ev.naturalHeight >= 0) {
      setNaturalHeight(ev.naturalHeight);
    }
    if (typeof ev.showInfo === 'boolean') {
      setShowInfo(ev.showInfo);
    }
    if (typeof ev.fileSizeBytes === 'number' && ev.fileSizeBytes >= 0) {
      hasBackendInfoRef.current = true;
      setFileSize(formatBytes(ev.fileSizeBytes));
    }
    if (typeof ev.format === 'string' && ev.format) {
      setFormatLabel(ev.format);
    }
    if (typeof ev.pageCount === 'number' && ev.pageCount > 0) {
      setPageCount(ev.pageCount);
    }
    if (typeof ev.currentPage === 'number' && ev.currentPage >= 0) {
      setCurrentPage(ev.currentPage);
    }
  };

  useEffect(() => {
    if (!window.cefQuery) return;

    // Apply a load-image event snapshot. C++ owns authoritative tab state
    // (url, current page, page count), so we treat every event as a full
    // snapshot — never a delta — so tab switches restore the exact page
    // the user was last viewing instead of resetting to page 0.
    const applyLoadImage = (ev) => {
      if (!ev || ev.key !== 'load-image') return;
      if (ev.error) {
        setPreviewError(ev.error);
        setToast(ev.error);
        setTimeout(() => setToast(''), 3000);
      } else {
        setPreviewError('');
      }
      const incomingPage = typeof ev.currentPage === 'number' && ev.currentPage >= 0 ? ev.currentPage : 0;
      const incomingDisplay = ev.error ? '' : (ev.displayUrl || '');
      const isRemoteSource = (ev.url || '').startsWith('http://') || (ev.url || '').startsWith('https://');
      displayedPageRef.current = incomingDisplay.startsWith('data:') ? incomingPage : -1;
      hasSnapshotRef.current = !!incomingDisplay;
      previewTabIdRef.current = typeof ev.tabId === 'number' ? ev.tabId : -1;
      setUrl(ev.url || '');
      setDisplayUrl(incomingDisplay);
      if (incomingDisplay) {
        setPreviewError('');
        setToast('');
      }
      setPageCount(typeof ev.pageCount === 'number' && ev.pageCount > 0 ? ev.pageCount : 1);
      if (typeof ev.fileSizeBytes === 'number' && ev.fileSizeBytes >= 0) {
        hasBackendInfoRef.current = true;
        setFileSize(formatBytes(ev.fileSizeBytes));
      } else {
        hasBackendInfoRef.current = false;
        setFileSize('');
      }
      setFormatLabel(typeof ev.format === 'string' ? ev.format : '');
      setIsTiffPreview(isTiffSource(ev.format, ev.url));
      setCurrentPage(incomingPage);
      setNaturalWidth(typeof ev.naturalWidth === 'number' && ev.naturalWidth >= 0 ? ev.naturalWidth : 0);
      setNaturalHeight(typeof ev.naturalHeight === 'number' && ev.naturalHeight >= 0 ? ev.naturalHeight : 0);
      setShowInfo(ev.showInfo !== false);
      setDecodeNonce(typeof ev.decodeNonce === 'string' || typeof ev.decodeNonce === 'number'
        ? Number(ev.decodeNonce)
        : 0);
      decodeNonceRef.current = typeof ev.decodeNonce === 'string' || typeof ev.decodeNonce === 'number'
        ? Number(ev.decodeNonce)
        : 0;
      if (!incomingDisplay && isRemoteSource) {
        setDownloadProgress(null);
        setIsDecoding(true);
      } else {
        setDownloadProgress(null);
        setIsDecoding(false);
      }
      setScale(1);
      setRotation(0);
      setPosition({ x: 0, y: 0 });
    };
    applyLoadImageRef.current = applyLoadImage;

    // Expose for direct push from C++ (SwitchTab). CEF may cancel the
    // persistent subscription when the BrowserView is hidden, so we can't
    // rely on it for tab-switch updates — the C++ side calls this directly.
    window.__otfApplyImagePreview = applyLoadImage;

    const sub = window.cefQuery({
      request: 'image-preview-subscribe',
      persistent: true,
      onSuccess: (json) => {
        try {
          const ev = JSON.parse(json);
          if (ev && ev.key === 'load-image') {
            applyLoadImage(ev);
          } else if (ev && (ev.key === 'image-preview-download-progress' || ev.key === 'tiff-download-progress')) {
            const eventNonce = typeof ev.decodeNonce === 'string' || typeof ev.decodeNonce === 'number'
              ? Number(ev.decodeNonce)
              : 0;
            if (eventNonce && eventNonce === decodeNonceRef.current) {
              setDownloadProgress({
                percent: typeof ev.percent === 'number' ? ev.percent : -1,
                receivedBytes: typeof ev.receivedBytes === 'number' ? ev.receivedBytes : 0,
                totalBytes: typeof ev.totalBytes === 'number' ? ev.totalBytes : -1,
              });
              setIsDecoding(true);
              if (typeof ev.totalBytes === 'number' && ev.totalBytes > 0) {
                hasBackendInfoRef.current = true;
                setFileSize(formatBytes(ev.totalBytes));
              }
              if (isTiffPreview && typeof ev.totalBytes === 'number' && ev.totalBytes > 0) {
                setFormatLabel('TIFF');
                setIsTiffPreview(true);
              }
            }
          }
        } catch (_) {}
      },
    });

    // The dedicated preview tab's BrowserView may be hidden by CEF when
    // the user switches tabs; on switch-back, the persistent subscription
    // may have been cancelled or the renderer may have been reloaded. Pull
    // a fresh snapshot whenever we become visible so the page is never
    // left blank.
    const refresh = () => {
      if (hasSnapshotRef.current) return;
      if (!window.cefQuery) return;
      window.cefQuery({
        request: 'image-preview-refresh',
        onSuccess: (json) => {
          try {
            const ev = JSON.parse(json);
            if (ev && ev.key === 'load-image') applyLoadImage(ev);
          } catch (_) {}
        },
      });
    };
    const onVis = () => { if (document.visibilityState === 'visible') refresh(); };
    document.addEventListener('visibilitychange', onVis);

    const onKeyDown = (event) => {
      if (event.key === 'Escape' && window.cefQuery) {
        event.preventDefault();
        window.cefQuery({ request: 'close-imagepreview' });
      }
    };
    window.addEventListener('keydown', onKeyDown);

    return () => {
      window.removeEventListener('keydown', onKeyDown);
      document.removeEventListener('visibilitychange', onVis);
      if (window.__otfApplyImagePreview === applyLoadImage) {
        delete window.__otfApplyImagePreview;
      }
      applyLoadImageRef.current = null;
      hasSnapshotRef.current = false;
      previewTabIdRef.current = -1;
      if (sub && typeof sub.cancel === 'function') sub.cancel();
    };
  }, []);

  useEffect(() => {
    if (!url) {
      setFileSize('');
      setFormatLabel('');
      setIsTiffPreview(false);
      hasBackendInfoRef.current = false;
      return;
    }

    if (hasBackendInfoRef.current) {
      return;
    }

    const isRemote = url.startsWith('http://') || url.startsWith('https://');
    if (isRemote) {
      return;
    }

    if (url.startsWith('data:')) {
      const base64Str = url.split(',')[1] || '';
      const bytes = Math.floor((base64Str.length * 3) / 4) - (base64Str.endsWith('==') ? 2 : base64Str.endsWith('=') ? 1 : 0);
      if (bytes < 1024) setFileSize(`${bytes} B`);
      else if (bytes < 1024 * 1024) setFileSize(`${(bytes / 1024).toFixed(1)} KB`);
      else setFileSize(`${(bytes / (1024 * 1024)).toFixed(1)} MB`);
      return;
    }

    setFileSize('Loading...');
    const applySizeLabel = (label) => {
      if (hasBackendInfoRef.current) return;
      setFileSize(label);
    };

    const fallbackJsFetch = (targetUrl) => {
      fetch(targetUrl, { method: 'HEAD' })
        .then(res => {
          const len = res.headers.get('content-length');
          if (len) {
            const bytes = parseInt(len, 10);
            if (bytes < 1024) applySizeLabel(`${bytes} B`);
            else if (bytes < 1024 * 1024) applySizeLabel(`${(bytes / 1024).toFixed(1)} KB`);
            else applySizeLabel(`${(bytes / (1024 * 1024)).toFixed(1)} MB`);
          } else {
            fetch(targetUrl)
              .then(r => r.blob())
              .then(blob => {
                const bytes = blob.size;
                if (bytes < 1024) applySizeLabel(`${bytes} B`);
                else if (bytes < 1024 * 1024) applySizeLabel(`${(bytes / 1024).toFixed(1)} KB`);
                else applySizeLabel(`${(bytes / (1024 * 1024)).toFixed(1)} MB`);
              })
              .catch(() => applySizeLabel('Unknown'));
          }
        })
        .catch(() => {
          fetch(targetUrl)
            .then(r => r.blob())
            .then(blob => {
              const bytes = blob.size;
              if (bytes < 1024) applySizeLabel(`${bytes} B`);
              else if (bytes < 1024 * 1024) applySizeLabel(`${(bytes / 1024).toFixed(1)} KB`);
              else applySizeLabel(`${(bytes / (1024 * 1024)).toFixed(1)} MB`);
            })
            .catch(() => applySizeLabel('Unknown'));
        });
    };

    if (window.cefQuery) {
      window.cefQuery({
        request: `get-image-size:${url}`,
        onSuccess: (sizeStr) => {
          if (hasBackendInfoRef.current) {
            return;
          }
          const bytes = parseInt(sizeStr, 10);
          if (isNaN(bytes) || bytes <= 0) {
            fallbackJsFetch(url);
          } else {
            applySizeLabel(bytes < 1024 ? `${bytes} B`
              : bytes < 1024 * 1024 ? `${(bytes / 1024).toFixed(1)} KB`
              : `${(bytes / (1024 * 1024)).toFixed(1)} MB`);
          }
        },
        onFailure: () => {
          fallbackJsFetch(url);
        }
      });
    } else {
      fallbackJsFetch(url);
    }
  }, [url]);

  useEffect(() => {
    if (!url) return;
    const isRemote = url.startsWith('http://') || url.startsWith('https://');
    if (!isTiffPreview && !isRemote) return;
    if (previewError) return;
    if (displayUrl.startsWith('data:')) {
      if (!isTiffPreview || displayedPageRef.current === currentPage) {
        return;
      }
    }
    if (!window.cefQuery) return;
    let cancelled = false;
    let toastTimer = null;
    setIsDecoding(true);
    setDownloadProgress(isRemote ? { percent: 0, receivedBytes: 0, totalBytes: -1 } : null);
    const tabIdPrefix = previewTabIdRef.current >= 0 ? `:${previewTabIdRef.current}` : '';
    window.cefQuery({
      request: `preview-image:${decodeNonce}:${currentPage}${tabIdPrefix}:${url}`,
      onSuccess: (json) => {
        if (cancelled) return;
        try {
          const res = JSON.parse(json);
          if (res && res.stale) {
            setIsDecoding(false);
            return;
          }
          if (res && res.error) {
            setDisplayUrl('');
            setPreviewError(res.error);
            setToast(res.error);
            toastTimer = setTimeout(() => setToast(''), 3000);
            setIsDecoding(false);
            return;
          }
          const newPage = typeof res.currentPage === 'number' ? res.currentPage : currentPage;
          displayedPageRef.current = newPage;
          hasSnapshotRef.current = true;
          setDownloadProgress(null);
          setDisplayUrl(res.displayUrl);
          setPreviewError('');
          setToast('');
          if (typeof res.fileSizeBytes === 'number' && res.fileSizeBytes >= 0) {
            hasBackendInfoRef.current = true;
            setFileSize(formatBytes(res.fileSizeBytes));
          }
          if (typeof res.format === 'string' && res.format) {
            setFormatLabel(res.format);
          }
          setPageCount(res.pageCount || 1);
          setCurrentPage(newPage);
          setIsDecoding(false);
        } catch (e) {
          console.error("Failed to parse TIFF response:", e);
          setIsDecoding(false);
        }
      },
      onFailure: (_, msg) => {
        if (cancelled) return;
        console.error("Image preview fetch failed:", msg);
        setDisplayUrl('');
        setDownloadProgress(null);
        setToast(msg || "Failed to render image");
        toastTimer = setTimeout(() => setToast(''), 3000);
        setIsDecoding(false);
      }
    });
    return () => {
      cancelled = true;
      if (toastTimer) clearTimeout(toastTimer);
    };
  }, [url, currentPage, decodeNonce, previewError, isTiffPreview]);

  const handleClose = () => {
    setUrl('');
    setDisplayUrl('');
    hasSnapshotRef.current = false;
    hasBackendInfoRef.current = false;
    setPageCount(1);
    setCurrentPage(0);
    setDownloadProgress(null);
    setPreviewError('');
    setFileSize('');
    setFormatLabel('');
    setIsTiffPreview(false);
    decodeNonceRef.current = 0;
    previewTabIdRef.current = -1;
    if (window.cefQuery) {
      const request = previewTabIdRef.current >= 0
        ? `close-imagepreview:${previewTabIdRef.current}`
        : 'close-imagepreview';
      window.cefQuery({ request });
    }
  };

  const nextPage = () => {
    if (currentPage < pageCount - 1) {
      setPreviewError('');
      setDisplayUrl('');
      setCurrentPage(prev => prev + 1);
    }
  };

  const prevPage = () => {
    if (currentPage > 0) {
      setPreviewError('');
      setDisplayUrl('');
      setCurrentPage(prev => prev - 1);
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
    if (!window.cefQuery) return;
    const width = e.target.naturalWidth || 0;
    const height = e.target.naturalHeight || 0;
    window.cefQuery({
      request: `image-preview-meta:${width}:${height}`,
      onSuccess: (json) => {
        try {
          const ev = JSON.parse(json);
          if (ev && ev.key === 'load-image') {
            applyLoadedImageMeta(ev);
          }
        } catch (_) {}
      },
    });
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

  const setInfoVisible = (visible) => {
    if (!window.cefQuery) return;
    window.cefQuery({
      request: `image-preview-info-visible:${visible ? 1 : 0}`,
      onSuccess: (json) => {
        try {
          const ev = JSON.parse(json);
          if (ev && ev.key === 'load-image') {
            applyLoadedImageMeta(ev);
          }
        } catch (_) {}
      },
    });
  };

  const handleSave = () => {
    if (window.cefQuery) {
      window.cefQuery({ request: 'download-image:' + url });
      showToast("Starting image download...");
    }
  };

  const handleCopyImage = async () => {
    try {
      const source = displayUrl || url;
      const response = await fetch(source);
      const blob = await response.blob();
      await navigator.clipboard.write([
        new ClipboardItem({ [blob.type]: blob })
      ]);
      showToast("Copied image to clipboard!");
    } catch (err) {
      navigator.clipboard.writeText(displayUrl || url);
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
        .btn-nav-page {
          width: 32px;
          height: 32px;
          border-radius: 50%;
          background: rgba(255, 255, 255, 0.08);
          border: 1px solid rgba(255, 255, 255, 0.12);
          color: rgba(255, 255, 255, 0.85);
          display: flex;
          align-items: center;
          justify-content: center;
          cursor: pointer;
          transition: all 0.15s ease;
        }
        .btn-nav-page:hover:not(:disabled) {
          background: #f97316;
          border-color: #fdba74;
          color: white;
          box-shadow: 0 0 12px rgba(249, 115, 22, 0.4);
          transform: scale(1.05);
        }
        .btn-nav-page:active:not(:disabled) {
          transform: scale(0.95);
        }
        .btn-nav-page:disabled {
          opacity: 0.3;
          cursor: not-allowed;
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
          src={displayUrl}
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
            <button onClick={() => setInfoVisible(false)} style={infoCloseBtnStyle} title="Hide info panel">×</button>
          </div>
          <div style={{ fontSize: '11px', color: 'rgba(255,255,255,0.7)', lineHeight: '1.5', wordBreak: 'break-all' }}>
            <strong>Resolution:</strong> {naturalWidth} × {naturalHeight} px<br />
            <strong>Size:</strong> {fileSize}<br />
            <strong>Format:</strong> {formatLabel || guessPreviewFormat(url) || 'Unknown'}<br />
            {pageCount > 1 && (
              <>
                <strong>Pages:</strong> {pageCount}<br />
                <strong>Current Page:</strong> {currentPage + 1}<br />
              </>
            )}
            <strong>Source:</strong> {url.length > 45 ? url.substring(0, 45) + '...' : url}
          </div>
        </div>
      )}
      {!showInfo && naturalWidth > 0 && (
        <button onClick={() => setInfoVisible(true)} style={infoOpenBtnStyle} title="Show image info">
          <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="16" x2="12" y2="12"></line><line x1="12" y1="8" x2="12.01" y2="8"></line></svg>
          Info
        </button>
      )}

      {/* Bottom Page Controls Overlay */}
      {pageCount > 1 && (
        <div style={bottomOverlayStyle}>
          <button
            onClick={prevPage}
            disabled={currentPage === 0 || isDecoding}
            className="btn-nav-page"
            title="Previous Page"
          >
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><polyline points="15 18 9 12 15 6"></polyline></svg>
          </button>

          <span style={pageInfoStyle}>
            {isDecoding ? 'Rendering...' : `${currentPage + 1} / ${pageCount}`}
          </span>

          <button
            onClick={nextPage}
            disabled={currentPage === pageCount - 1 || isDecoding}
            className="btn-nav-page"
            title="Next Page"
          >
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg>
          </button>
        </div>
      )}

      {/* Toast Notification */}
      {toast && (
        <div style={toastStyle}>
          {toast}
        </div>
      )}

      {isDecoding && (
        <div style={decodeOverlayStyle}>
          <div style={decodeLabelStyle}>
            {(() => {
              const isRemoteSource = url.startsWith('http://') || url.startsWith('https://');
              if (downloadProgress && downloadProgress.totalBytes > 0) {
                return `Downloading ${downloadProgress.percent >= 0 ? `${downloadProgress.percent}%` : ''}`.trim();
              }
              if (downloadProgress && downloadProgress.receivedBytes > 0) {
                return `Downloading ${formatBytes(downloadProgress.receivedBytes)}`;
              }
              return isRemoteSource ? 'Downloading image...' : 'Rendering page...';
            })()}
          </div>
          {downloadProgress && downloadProgress.totalBytes > 0 && (
            <div style={progressTrackStyle}>
              <div
                style={{
                  ...progressFillStyle,
                  width: `${Math.max(0, Math.min(100, downloadProgress.percent))}%`,
                }}
              />
            </div>
          )}
          {downloadProgress && downloadProgress.totalBytes <= 0 && downloadProgress.receivedBytes > 0 && (
            <div style={progressSubtextStyle}>
              {formatBytes(downloadProgress.receivedBytes)} received
            </div>
          )}
        </div>
      )}
    </div>
  );
};

const formatBytes = (bytes) => {
  if (!bytes || bytes <= 0) return '0 B';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
};

const guessPreviewFormat = (value) => {
  const source = (value || '').split('?')[0].split('#')[0];
  const dot = source.lastIndexOf('.');
  if (dot === -1) return '';
  const ext = source.slice(dot + 1).toUpperCase();
  return ['TIF', 'TIFF', 'PNG', 'JPG', 'JPEG', 'GIF', 'WEBP', 'BMP', 'ICO'].includes(ext)
    ? ext
    : '';
};

const isTiffSource = (format, value) => {
  const normalized = typeof format === 'string' ? format.trim().toUpperCase() : '';
  if (normalized === 'TIFF' || normalized === 'TIF') {
    return true;
  }
  const guessed = guessPreviewFormat(value);
  return guessed === 'TIF' || guessed === 'TIFF';
};

const bottomOverlayStyle = {
  position: 'absolute',
  bottom: '32px',
  left: '50%',
  transform: 'translateX(-50%)',
  background: 'rgba(15, 23, 42, 0.75)',
  backdropFilter: 'blur(16px)',
  border: '1px solid rgba(255, 255, 255, 0.12)',
  padding: '6px 12px',
  borderRadius: '9999px',
  zIndex: 30,
  display: 'flex',
  alignItems: 'center',
  gap: '12px',
  boxShadow: '0 12px 24px rgba(0, 0, 0, 0.5)',
  animation: 'fadeIn 0.25s ease-out',
};

const pageInfoStyle = {
  color: 'rgba(255, 255, 255, 0.9)',
  fontSize: '12px',
  fontWeight: '700',
  padding: '0 4px',
  userSelect: 'none',
  fontFamily: 'system-ui, -apple-system, sans-serif',
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
  bottom: '96px',
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

const decodeOverlayStyle = {
  position: 'absolute',
  inset: '50% auto auto 50%',
  transform: 'translate(-50%, -50%)',
  minWidth: '220px',
  padding: '12px 14px',
  borderRadius: '12px',
  background: 'rgba(15, 23, 42, 0.82)',
  border: '1px solid rgba(255, 255, 255, 0.12)',
  color: 'rgba(255, 255, 255, 0.9)',
  fontSize: '12px',
  fontWeight: '700',
  zIndex: 40,
  boxShadow: '0 12px 24px rgba(0,0,0,0.35)',
  display: 'flex',
  flexDirection: 'column',
  gap: '8px',
};

const decodeLabelStyle = {
  fontSize: '12px',
  fontWeight: '700',
  lineHeight: '1.2',
};

const progressTrackStyle = {
  width: '100%',
  height: '6px',
  borderRadius: '9999px',
  background: 'rgba(255,255,255,0.12)',
  overflow: 'hidden',
};

const progressFillStyle = {
  height: '100%',
  borderRadius: '9999px',
  background: 'linear-gradient(90deg, #fb923c, #f97316)',
  transition: 'width 120ms linear',
};

const progressSubtextStyle = {
  fontSize: '11px',
  fontWeight: '600',
  color: 'rgba(255,255,255,0.68)',
};

const btnStyle = {};

export default ImagePreview;
