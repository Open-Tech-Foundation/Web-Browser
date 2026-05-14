import React, { useCallback, useEffect, useState } from 'react';

const CertificateViewer = () => {
  const [certData, setCertData] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [tabId, setTabId] = useState(-1);

  const handleClose = () => {
    if (window.cefQuery) {
      window.cefQuery({ request: 'hide-certificate' });
    }
  };

  const loadCertificate = useCallback((nextTabId) => {
    if (!window.cefQuery || nextTabId < 0) {
      setLoading(false);
      setError('No certificate available for current tab');
      setCertData(null);
      return;
    }

    setLoading(true);
    setError(null);
    setCertData(null);

    window.cefQuery({
      request: `get-certificate-by-tab-id:${nextTabId}`,
      onSuccess: (response) => {
        try {
          const data = JSON.parse(response);
          if (data.ok) {
            setCertData(data);
          } else {
            setError(data.reason || 'Failed to load certificate');
          }
        } catch (e) {
          setError('Failed to parse certificate data');
        }
        setLoading(false);
      },
      onFailure: (code, msg) => {
        setError(msg || 'Failed to fetch certificate');
        setLoading(false);
      }
    });
  }, []);

  const [appearanceMode, setAppearanceMode] = useState('auto');
  const isSecure = Boolean(certData && !error && !certData.hasCertificateError);
  const isInsecure = !isSecure;

  const applyTheme = (mode) => {
    const root = document.documentElement;
    if (mode === 'light') {
      root.classList.remove('dark');
    } else if (mode === 'dark') {
      root.classList.add('dark');
    } else {
      if (window.matchMedia('(prefers-color-scheme: dark)').matches) {
        root.classList.add('dark');
      } else {
        root.classList.remove('dark');
      }
    }
  };

  useEffect(() => {
    applyTheme(appearanceMode);
  }, [appearanceMode]);

  useEffect(() => {
    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const handleChange = () => {
      if (appearanceMode === 'auto') applyTheme('auto');
    };
    mediaQuery.addEventListener('change', handleChange);
    return () => mediaQuery.removeEventListener('change', handleChange);
  }, [appearanceMode]);

  useEffect(() => {
    const onBlur = () => handleClose();
    const onKeyDown = (e) => {
      if (e.key === 'Escape') handleClose();
    };

    window.addEventListener('blur', onBlur);
    window.addEventListener('keydown', onKeyDown);

    if (window.cefQuery) {
      // Get initial settings for theme
      window.cefQuery({
        request: 'get-settings',
        onSuccess: (response) => {
          try {
            const settings = JSON.parse(response);
            setAppearanceMode(settings.appearanceMode || 'auto');
          } catch (e) {}
        }
      });

      window.cefQuery({
        request: 'certificate-subscribe',
        persistent: true,
        onSuccess: (response) => {
          try {
            const event = JSON.parse(response);
            if (event.key === 'certificate-restore') {
              const nextTabId = Number.parseInt(event.tabId ?? '', 10);
              setTabId(Number.isInteger(nextTabId) ? nextTabId : -1);
              loadCertificate(Number.isInteger(nextTabId) ? nextTabId : -1);
            } else if (event.key === 'settings-changed') {
              setAppearanceMode(event.settings?.appearanceMode || 'auto');
            }
          } catch (e) {
            setError('Failed to parse certificate data');
            setLoading(false);
          }
        }
      });
    }

    return () => {
      window.removeEventListener('blur', onBlur);
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [loadCertificate]);

  const formatDate = (seconds) => {
    return new Date(seconds * 1000).toLocaleString(undefined, {
      year: 'numeric',
      month: 'long',
      day: 'numeric',
    });
  };

  return (
    <div className="w-full h-full bg-card border-r border-main shadow-2xl flex flex-col overflow-hidden relative rounded-r-2xl text-main transition-colors duration-200">
      {/* Header */}
      <div className="p-5 border-b border-main flex items-center justify-between bg-main/30 backdrop-blur-md">
        <div className="flex items-center gap-3">
          <div className={`p-2 rounded-xl ${isInsecure ? 'bg-red-500/10 text-red-500' : 'bg-green-500/10 text-green-500'}`}>
            <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
              <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z" />
              {isInsecure ? <line x1="12" y1="8" x2="12" y2="12" /> : <polyline points="9 12 11 14 15 10" />}
            </svg>
          </div>
          <div>
            <h2 className="text-sm font-black text-main uppercase tracking-tight">Security</h2>
            <p className="text-[9px] font-bold text-muted uppercase tracking-widest leading-none mt-0.5">Certificate Details</p>
          </div>
        </div>
        <button onClick={handleClose} className="w-8 h-8 flex items-center justify-center rounded-lg hover:bg-main/10 text-muted hover:text-main transition-all active:scale-90">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
        </button>
      </div>

      <div className="flex-grow p-5 space-y-5 overflow-y-auto custom-scrollbar">
        {loading ? (
          <div className="py-12 flex flex-col items-center justify-center gap-4">
            <div className="w-10 h-10 border-[3px] border-orange-500/20 border-t-orange-500 rounded-full animate-spin" />
            <span className="text-[10px] font-black text-muted uppercase tracking-[0.2em]">Verifying...</span>
          </div>
        ) : error ? (
          <div className="py-8 text-center animate-in fade-in zoom-in duration-500">
            <div className="w-14 h-14 bg-red-500/10 text-red-500 rounded-2xl flex items-center justify-center mx-auto mb-4 border border-red-500/20">
               <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5"><path d="m21 21-4.3-4.3"/><circle cx="11" cy="11" r="8"/><path d="m11 8 0 4"/><path d="m11 16 0 .01"/></svg>
            </div>
            <p className="text-xs text-main font-black uppercase tracking-tight mb-1">Information Not Available</p>
            <p className="text-[10px] text-muted font-medium leading-relaxed px-4">{error}</p>
          </div>
        ) : (
          <div className="animate-in fade-in slide-in-from-bottom-4 duration-500">
            <section className="mb-5">
              <h3 className="text-[9px] font-black text-orange-500 uppercase tracking-[0.2em] mb-3 ml-1">Issued To</h3>
              <div className="bg-main/5 border border-main rounded-2xl p-4 space-y-3">
                <div>
                  <label className="text-[8px] font-black text-muted uppercase tracking-widest block mb-1">Common Name</label>
                  <p className="text-xs font-mono font-bold text-main truncate">{certData.issuedTo.commonName}</p>
                </div>
                <div className="pt-2 border-t border-main">
                  <label className="text-[8px] font-black text-muted uppercase tracking-widest block mb-1">Organization</label>
                  <p className="text-xs font-bold text-main">{certData.issuedTo.organization?.join(', ') || 'Private Domain'}</p>
                </div>
              </div>
            </section>

            <section className="mb-5">
              <h3 className="text-[9px] font-black text-orange-500 uppercase tracking-[0.2em] mb-3 ml-1">Issued By</h3>
              <div className="bg-main/5 border border-main rounded-2xl p-4 space-y-3">
                <div>
                  <label className="text-[8px] font-black text-muted uppercase tracking-widest block mb-1">Common Name</label>
                  <p className="text-xs font-mono font-bold text-main truncate">{certData.issuedBy.commonName}</p>
                </div>
                <div className="pt-2 border-t border-main">
                  <label className="text-[8px] font-black text-muted uppercase tracking-widest block mb-1">Organization</label>
                  <p className="text-xs font-bold text-main">{certData.issuedBy.organization?.join(', ') || 'Certificate Authority'}</p>
                </div>
              </div>
            </section>

            <section className="mb-6">
              <h3 className="text-[9px] font-black text-orange-500 uppercase tracking-[0.2em] mb-3 ml-1">Validity Period</h3>
              <div className="bg-main/5 border border-main rounded-2xl p-4">
                <div className="flex justify-between items-center mb-3">
                  <span className="text-[8px] font-black text-muted uppercase tracking-widest">Starts</span>
                  <span className="text-xs font-bold text-main">{formatDate(certData.validity.notBefore)}</span>
                </div>
                <div className="flex justify-between items-center pt-3 border-t border-main">
                  <span className="text-[8px] font-black text-muted uppercase tracking-widest">Expires</span>
                  <span className="text-xs font-bold text-main">{formatDate(certData.validity.notAfter)}</span>
                </div>
              </div>
            </section>

            <div className={`p-4 rounded-2xl border flex gap-3 ${isInsecure ? 'bg-red-500/5 border-red-500/20' : 'bg-green-500/5 border-green-500/20'}`}>
              <div className={`shrink-0 mt-0.5 ${isInsecure ? 'text-red-500' : 'text-green-500'}`}>
                 <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3"><circle cx="12" cy="12" r="10"/><path d="m12 16 0-4"/><path d="m12 8 0 .01"/></svg>
              </div>
              <p className={`text-[10px] leading-relaxed font-bold ${isInsecure ? 'text-red-600/80' : 'text-green-600/80'}`}>
                {isInsecure 
                  ? 'Warning: This connection is not secure. The certificate is invalid or untrusted.' 
                  : 'Your connection is private. Information like passwords or credit cards is secure.'}
              </p>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};

export default CertificateViewer;
