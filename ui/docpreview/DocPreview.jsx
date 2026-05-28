import React, { useEffect, useState, useRef } from 'react';

const formatBytes = (bytes) => {
  if (bytes < 0) return '';
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
};

const guessLanguage = (mimeType, url) => {
  if (mimeType === 'application/json') return 'json';
  if (mimeType === 'application/xml' || mimeType === 'text/xml') return 'xml';
  if (mimeType === 'text/html') return 'html';
  if (mimeType === 'text/css') return 'css';
  if (mimeType === 'text/javascript') return 'javascript';
  if (mimeType === 'text/typescript') return 'typescript';
  if (mimeType === 'text/x-python') return 'python';
  if (mimeType === 'text/x-shellscript') return 'shell';
  if (mimeType === 'text/x-sql') return 'sql';
  if (mimeType === 'text/x-c') return 'c';
  if (mimeType === 'text/x-rust') return 'rust';
  if (mimeType === 'text/x-go') return 'go';
  if (mimeType === 'text/x-java') return 'java';
  if (mimeType === 'text/x-ruby') return 'ruby';
  if (mimeType === 'text/x-lua') return 'lua';
  if (mimeType === 'text/x-php') return 'php';
  if (mimeType === 'text/x-tex') return 'tex';
  if (mimeType === 'text/markdown') return 'markdown';
  const ext = (url || '').split('.').pop().split('?')[0].toLowerCase();
  const extMap = {
    json: 'json', xml: 'xml', html: 'html', css: 'css', js: 'javascript',
    ts: 'typescript', py: 'python', sh: 'shell', bash: 'shell', sql: 'sql',
    c: 'c', cpp: 'c', h: 'c', hpp: 'c', rs: 'rust', go: 'go',
    java: 'java', rb: 'ruby', lua: 'lua', php: 'php', tex: 'tex',
    md: 'markdown', yaml: 'yaml', yml: 'yaml', toml: 'toml',
  };
  return extMap[ext] || 'text';
};

const DocPreview = () => {
  const [url, setUrl] = useState('');
  const [displayUrl, setDisplayUrl] = useState('');
  const [contentUrl, setContentUrl] = useState('');
  const [mimeType, setMimeType] = useState('text/plain');
  const [fileSize, setFileSize] = useState('');
  const [formatLabel, setFormatLabel] = useState('');
  const [toast, setToast] = useState('');
  const [error, setError] = useState('');
  const [textContent, setTextContent] = useState('');

  const hasSnapshotRef = useRef(false);
  const previewTabIdRef = useRef(-1);
  const applyLoadDocRef = useRef(null);
  const pdfNavigatedRef = useRef(false);

  const applyLoadedDocMeta = (ev) => {
    if (!ev || ev.key !== 'load-doc') return;
    if (typeof ev.fileSizeBytes === 'number' && ev.fileSizeBytes >= 0) {
      setFileSize(formatBytes(ev.fileSizeBytes));
    }
    if (typeof ev.format === 'string' && ev.format) {
      setFormatLabel(ev.format);
    }
    if (typeof ev.mimeType === 'string' && ev.mimeType) {
      setMimeType(ev.mimeType);
    }
  };

  useEffect(() => {
    if (!window.cefQuery) return;

    const applyLoadDoc = (ev) => {
      console.log('[DOCPREVIEW] applyLoadDoc called with:', JSON.stringify(ev).substring(0, 500));
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
      hasSnapshotRef.current = !!incomingDisplay || !!incomingContent;
      previewTabIdRef.current = typeof ev.tabId === 'number' ? ev.tabId : -1;
      setUrl(ev.url || '');
      setDisplayUrl(incomingDisplay);
      setContentUrl(incomingContent);
      setMimeType(typeof ev.mimeType === 'string' ? ev.mimeType : 'text/plain');
      if (typeof ev.fileSizeBytes === 'number' && ev.fileSizeBytes >= 0) {
        setFileSize(formatBytes(ev.fileSizeBytes));
      } else {
        setFileSize('');
      }
      setFormatLabel(typeof ev.format === 'string' ? ev.format : '');

      if (incomingDisplay && incomingDisplay.startsWith('data:') &&
          !incomingDisplay.startsWith('data:application/pdf;')) {
        try {
          const base64 = incomingDisplay.split(',')[1];
          const decoded = atob(base64);
          setTextContent(decoded);
        } catch (_) {
          setTextContent('');
        }
      } else {
        setTextContent('');
      }
    };
    applyLoadDocRef.current = applyLoadDoc;

    window.__otfApplyDocPreview = applyLoadDoc;

    console.log('[DOCPREVIEW] subscribing to doc-preview-subscribe');
    const sub = window.cefQuery({
      request: 'doc-preview-subscribe',
      persistent: true,
      onSuccess: (json) => {
        console.log('[DOCPREVIEW] subscription event:', json.substring(0, 300));
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
      retryTimer = setTimeout(() => {
        retryTimer = null;
        refresh();
      }, 250);
    };
    const refresh = () => {
      if (hasSnapshotRef.current) return;
      if (!window.cefQuery) return;
      console.log('[DOCPREVIEW] refresh called');
      window.cefQuery({
        request: 'doc-preview-refresh',
        onSuccess: (json) => {
          console.log('[DOCPREVIEW] refresh response:', json.substring(0, 300));
          try {
            const ev = JSON.parse(json);
            if (ev && ev.key === 'load-doc') {
              applyLoadDoc(ev);
              if (!ev.displayUrl && !ev.contentUrl && !ev.error) scheduleRetry();
            } else {
              scheduleRetry();
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

    return () => {
      disposed = true;
      document.removeEventListener('visibilitychange', onVis);
      if (retryTimer) clearTimeout(retryTimer);
      if (sub && sub.cancel) sub.cancel();
      window.__otfApplyDocPreview = null;
    };
  }, []);

  const isPdf = mimeType === 'application/pdf';
  const isText = !isPdf;
  const lang = guessLanguage(mimeType, url);

  const renderContent = () => {
    if (!displayUrl && !contentUrl && !textContent) {
      return (
        <div style={emptyStyle}>
          <div style={{ fontSize: '48px', marginBottom: '16px' }}>&#128196;</div>
          <div style={{ fontSize: '16px', fontWeight: '600', color: 'rgba(255,255,255,0.9)' }}>
            No document loaded
          </div>
          <div style={{ fontSize: '13px', color: 'rgba(255,255,255,0.5)', marginTop: '8px' }}>
            Open a document from downloads to preview it here
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

    return (
      <div style={textContainerStyle}>
        <pre style={preStyle}>
          <code>{textContent}</code>
        </pre>
      </div>
    );
  };

  const fileName = url.split('/').pop().split('?')[0] || 'Document';

  return (
    <div style={containerStyle}>
      <div style={titleBarStyle}>
        <span style={titleNameStyle}>{fileName}</span>
        <div style={titleRightStyle}>
          <span style={formatBadgeStyle}>{formatLabel || lang.toUpperCase()}</span>
          {fileSize && <span style={sizeTextStyle}>{fileSize}</span>}
        </div>
      </div>
      {renderContent()}
      {toast && <div style={toastStyle}>{toast}</div>}
    </div>
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

const pdfFrameStyle = {
  flex: 1,
  width: '100%',
  border: 'none',
};

const textContainerStyle = {
  flex: 1,
  overflow: 'auto',
  padding: '16px',
};

const preStyle = {
  margin: 0,
  fontFamily: "'JetBrains Mono', 'Fira Code', 'Cascadia Code', 'Consolas', monospace",
  fontSize: '13px',
  lineHeight: '1.6',
  color: '#e2e8f0',
  whiteSpace: 'pre-wrap',
  wordBreak: 'break-word',
  tabSize: 2,
};

const formatBadgeStyle = {
  padding: '2px 6px',
  borderRadius: '4px',
  background: 'rgba(255, 122, 0, 0.15)',
  color: '#FF7A00',
  fontWeight: '700',
  fontSize: '10px',
  letterSpacing: '0.5px',
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

export default DocPreview;
