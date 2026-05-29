import React, { useEffect, useState, useRef } from 'react';
import TextViewer from './TextViewer';
import CsvViewer from './CsvViewer';

const formatBytes = (bytes) => {
  if (bytes < 0) return '';
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
};

const DocPreview = () => {
  const [url, setUrl] = useState('');
  const [displayUrl, setDisplayUrl] = useState('');
  const [contentUrl, setContentUrl] = useState('');
  const [mimeType, setMimeType] = useState('text/plain');
  const [fileSize, setFileSize] = useState('');
  const [toast, setToast] = useState('');
  const [error, setError] = useState('');
  const [textContent, setTextContent] = useState('');
  const [isLoading, setIsLoading] = useState(false);

  const hasSnapshotRef = useRef(false);
  const previewTabIdRef = useRef(-1);
  const applyLoadDocRef = useRef(null);
  const pdfNavigatedRef = useRef(false);
  const retryCountRef = useRef(0);
  const MAX_RETRIES = 20;

  const applyLoadedDocMeta = (ev) => {
    if (!ev || ev.key !== 'load-doc') return;
    if (typeof ev.fileSizeBytes === 'number' && ev.fileSizeBytes >= 0) {
      setFileSize(formatBytes(ev.fileSizeBytes));
    }
    if (typeof ev.mimeType === 'string' && ev.mimeType) {
      setMimeType(ev.mimeType);
    }
  };

  const handleClose = () => {
    const sourceTabId = previewTabIdRef.current;
    if (window.cefQuery) {
      const request = sourceTabId >= 0
        ? `close-docpreview:${sourceTabId}`
        : 'close-docpreview';
      window.cefQuery({ request });
    }
  };

  const handleDownload = () => {
    if (window.cefQuery) {
      window.cefQuery({ request: 'download-doc:' + url });
      setToast('Saving document...');
      setTimeout(() => setToast(''), 2000);
    }
  };

  useEffect(() => {
    if (!window.cefQuery) return;

    const applyLoadDoc = (ev) => {
      if (!ev || ev.key !== 'load-doc') return;
      if (ev.error) {
        setError(ev.error);
        setToast(ev.error);
        setTimeout(() => setToast(''), 3000);
      } else {
        setError('');
      }
      const incomingDisplay = ev.error ? '' : (ev.displayUrl || '');
      const incomingContent = ev.error ? '' : (ev.contentUrl || '');
      const incomingUrl = ev.url || '';
      const isRemoteSource = incomingUrl.startsWith('http://') || incomingUrl.startsWith('https://');
      hasSnapshotRef.current = !!incomingDisplay || !!incomingContent;
      previewTabIdRef.current = typeof ev.tabId === 'number' ? ev.tabId : -1;
      setUrl(incomingUrl);
      setDisplayUrl(incomingDisplay);
      setContentUrl(incomingContent);
      setMimeType(typeof ev.mimeType === 'string' ? ev.mimeType : 'text/plain');
      if (typeof ev.fileSizeBytes === 'number' && ev.fileSizeBytes >= 0) {
        setFileSize(formatBytes(ev.fileSizeBytes));
      } else {
        setFileSize('');
      }

      if (incomingDisplay && incomingDisplay.startsWith('data:') &&
          !incomingDisplay.startsWith('data:application/pdf;')) {
        try {
          const base64 = incomingDisplay.split(',')[1];
          const binaryStr = atob(base64);
          const bytes = Uint8Array.from(binaryStr, (c) => c.charCodeAt(0));
          const decoded = new TextDecoder('utf-8').decode(bytes);
          setTextContent(decoded);
        } catch (_) {
          setTextContent('');
        }
      } else {
        setTextContent('');
      }

      // Show loading indicator for remote sources that haven't received content yet
      setIsLoading(isRemoteSource && !incomingDisplay && !incomingContent && !ev.error);
    };
    applyLoadDocRef.current = applyLoadDoc;

    window.__otfApplyDocPreview = applyLoadDoc;

    const sub = window.cefQuery({
      request: 'doc-preview-subscribe',
      persistent: true,
      onSuccess: (json) => {
        try {
          const ev = JSON.parse(json);
          if (ev && ev.key === 'load-doc') {
            applyLoadDoc(ev);
          }
        } catch (_) {}
      },
    });

    let retryTimer = null;
    let disposed = false;
    const scheduleRetry = () => {
      if (disposed || hasSnapshotRef.current || retryTimer) return;
      if (retryCountRef.current >= MAX_RETRIES) return;
      retryCountRef.current += 1;
      retryTimer = setTimeout(() => {
        retryTimer = null;
        refresh();
      }, 500);
    };
    const refresh = () => {
      if (hasSnapshotRef.current) return;
      if (!window.cefQuery) return;
      window.cefQuery({
        request: 'doc-preview-refresh',
        onSuccess: (json) => {
          try {
            const ev = JSON.parse(json);
            if (ev && ev.key === 'load-doc') {
              applyLoadDoc(ev);
              if (!ev.displayUrl && !ev.contentUrl && !ev.error && ev.url) {
                scheduleRetry();
              }
            }
          } catch (_) {
            scheduleRetry();
          }
        },
        onFailure: scheduleRetry,
      });
    };
    const onVis = () => { if (document.visibilityState === 'visible') refresh(); };
    document.addEventListener('visibilitychange', onVis);
    setTimeout(refresh, 0);

    const onKeyDown = (event) => {
      if (event.key === 'Escape' && window.cefQuery) {
        event.preventDefault();
        const request = previewTabIdRef.current >= 0
          ? `close-docpreview:${previewTabIdRef.current}`
          : 'close-docpreview';
        window.cefQuery({ request });
      }
    };
    window.addEventListener('keydown', onKeyDown);

    return () => {
      disposed = true;
      document.removeEventListener('visibilitychange', onVis);
      window.removeEventListener('keydown', onKeyDown);
      if (retryTimer) clearTimeout(retryTimer);
      if (sub && sub.cancel) sub.cancel();
      window.__otfApplyDocPreview = null;
    };
  }, []);

  const isPdf = mimeType === 'application/pdf';
  const fileName = url.split('/').pop().split('?')[0] || '';
  const isCsv = mimeType === 'text/csv' || fileName.toLowerCase().endsWith('.csv');

  const renderContent = () => {
    if (!displayUrl && !contentUrl && !textContent) {
      if (isLoading) {
        return (
          <div style={emptyStyle}>
            <div style={{ fontSize: '48px', marginBottom: '16px' }}>&#128196;</div>
            <div style={{ fontSize: '16px', fontWeight: '600', color: 'rgba(255,255,255,0.9)', marginBottom: '12px' }}>
              Downloading document...
            </div>
            <div style={{ width: '200px', height: '4px', background: 'rgba(255,255,255,0.1)', borderRadius: '2px', overflow: 'hidden' }}>
              <div style={progressBarStyle} />
            </div>
          </div>
        );
      }
      return (
        <div style={emptyStyle}>
          <div style={{ fontSize: '48px', marginBottom: '16px' }}>&#128196;</div>
          <div style={{ fontSize: '16px', fontWeight: '600', color: 'rgba(255,255,255,0.9)' }}>
            No document loaded
          </div>
        </div>
      );
    }

    if (isPdf) {
      // PDFs can't use data: URIs (blocked by page security policy) or
      // cross-origin iframes. Navigate the whole tab to the content URL
      // so CEF's built-in PDF viewer takes over.
      if (contentUrl && !pdfNavigatedRef.current) {
        pdfNavigatedRef.current = true;
        window.location.href = contentUrl;
      }
      return (
        <div style={emptyStyle}>
          <div style={{ fontSize: '48px', marginBottom: '16px' }}>&#128196;</div>
          <div style={{ fontSize: '16px', fontWeight: '600', color: 'rgba(255,255,255,0.9)' }}>
            Loading PDF...
          </div>
        </div>
      );
    }

    if (isCsv) {
      return <CsvViewer content={textContent} fileName={fileName} />;
    }

    return (
      <div style={textContainerStyle}>
        <TextViewer content={textContent} mimeType={mimeType} fileName={fileName} />
      </div>
    );
  };

  return (
    <>
      <style>{`
        @keyframes docPreviewProgress {
          0% { transform: translateX(-100%); }
          50% { transform: translateX(0%); }
          100% { transform: translateX(100%); }
        }
      `}</style>
      <div style={containerStyle}>
      <div style={titleBarStyle}>
        <span style={titleNameStyle}>{fileName}</span>
        <div style={titleRightStyle}>
          {fileSize && <span style={sizeTextStyle}>{fileSize}</span>}
          {url && (
            <button onClick={handleDownload} style={btnSmallStyle} title="Save document">
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
            </button>
          )}
          <button onClick={handleClose} style={btnCloseSmallStyle} title="Close preview">
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
          </button>
        </div>
      </div>
      {renderContent()}
      {toast && <div style={toastStyle}>{toast}</div>}
    </div>
    </>
  );
};

const containerStyle = {
  width: '100%',
  height: '100%',
  display: 'flex',
  flexDirection: 'column',
  background: '#1a1a2e',
  color: '#e2e8f0',
  position: 'relative',
};

const titleBarStyle = {
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'space-between',
  padding: '8px 14px',
  background: 'rgba(15, 23, 42, 0.9)',
  borderBottom: '1px solid rgba(255, 255, 255, 0.08)',
  minHeight: '36px',
  flexShrink: 0,
};

const titleNameStyle = {
  fontSize: '13px',
  fontWeight: '600',
  color: 'rgba(255, 255, 255, 0.9)',
  overflow: 'hidden',
  textOverflow: 'ellipsis',
  whiteSpace: 'nowrap',
  marginRight: '12px',
};

const titleRightStyle = {
  display: 'flex',
  alignItems: 'center',
  gap: '8px',
  flexShrink: 0,
};

const emptyStyle = {
  flex: 1,
  display: 'flex',
  flexDirection: 'column',
  alignItems: 'center',
  justifyContent: 'center',
  color: 'rgba(255,255,255,0.6)',
};

const btnSmallStyle = {
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  width: '24px',
  height: '24px',
  borderRadius: '4px',
  border: '1px solid rgba(255, 255, 255, 0.15)',
  background: 'rgba(255, 255, 255, 0.06)',
  color: 'rgba(255, 255, 255, 0.7)',
  cursor: 'pointer',
  padding: 0,
  transition: 'background 0.15s ease, color 0.15s ease',
};

const btnCloseSmallStyle = {
  ...btnSmallStyle,
  border: '1px solid rgba(239, 68, 68, 0.25)',
  background: 'rgba(239, 68, 68, 0.08)',
  color: '#fca5a5',
};

const textContainerStyle = {
  flex: 1,
  overflow: 'hidden',
};

const sizeTextStyle = {
  color: 'rgba(255, 255, 255, 0.5)',
  fontWeight: '500',
  fontSize: '11px',
};

const toastStyle = {
  position: 'absolute',
  bottom: '48px',
  left: '50%',
  transform: 'translateX(-50%)',
  padding: '8px 16px',
  borderRadius: '8px',
  background: 'rgba(15, 23, 42, 0.9)',
  border: '1px solid rgba(255, 255, 255, 0.12)',
  color: 'rgba(255, 255, 255, 0.9)',
  fontSize: '12px',
  fontWeight: '600',
  zIndex: 40,
  boxShadow: '0 4px 12px rgba(0,0,0,0.4)',
};

const progressBarStyle = {
  height: '100%',
  width: '100%',
  background: '#FF7A00',
  borderRadius: '2px',
  animation: 'docPreviewProgress 1.5s ease-in-out infinite',
};

export default DocPreview;
