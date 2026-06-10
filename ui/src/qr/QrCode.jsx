import React, { useEffect, useRef, useState } from 'react';
import QRCode from 'qrcode';
import Popup, { usePopupRestore } from '../components/Popup';
import { getNativeSettings } from '../shared/nativeRequest';

const stripUtmParams = (rawUrl) => {
  try {
    const u = new URL(rawUrl);
    const toDelete = [...u.searchParams.keys()].filter((k) => k.startsWith('utm_'));
    toDelete.forEach((k) => u.searchParams.delete(k));
    // Remove trailing '?' when no params remain
    return u.searchParams.size === 0
      ? u.origin + u.pathname + (u.hash || '')
      : u.toString();
  } catch (_) {
    return rawUrl;
  }
};

const QrCodeViewer = () => {
  const canvasRef = useRef(null);
  const [url, setUrl] = useState('');
  const [inputUrl, setInputUrl] = useState('');
  const [copiedUrl, setCopiedUrl] = useState(false);
  const [copiedImg, setCopiedImg] = useState(false);
  const [downloaded, setDownloaded] = useState(false);
  const [appearanceMode, setAppearanceMode] = useState('auto');

  const applyTheme = (mode) => {
    const root = document.documentElement;
    if (mode === 'light') root.classList.remove('dark');
    else if (mode === 'dark') root.classList.add('dark');
    else {
      if (window.matchMedia('(prefers-color-scheme: dark)').matches) root.classList.add('dark');
      else root.classList.remove('dark');
    }
  };

  useEffect(() => { applyTheme(appearanceMode); }, [appearanceMode]);

  useEffect(() => {
    const mq = window.matchMedia('(prefers-color-scheme: dark)');
    const handler = () => { if (appearanceMode === 'auto') applyTheme('auto'); };
    mq.addEventListener('change', handler);
    return () => mq.removeEventListener('change', handler);
  }, [appearanceMode]);

  useEffect(() => {
    if (!window.cefQuery) return;
    getNativeSettings()
      .then((settings) => setAppearanceMode(settings.appearanceMode || 'auto'))
      .catch(() => {});
  }, []);

  // Receive URL from C++ restore producer on every Show()
  usePopupRestore('qr', (payload) => {
    if (payload.url) {
      const clean = stripUtmParams(payload.url);
      setUrl(clean);
      setInputUrl(clean);
    }
    setCopiedUrl(false);
    setCopiedImg(false);
    setDownloaded(false);
  });

  // Re-render QR whenever the editable URL or theme changes
  useEffect(() => {
    if (!inputUrl || !canvasRef.current) return;
    const isDark = document.documentElement.classList.contains('dark');
    QRCode.toCanvas(canvasRef.current, inputUrl, {
      width: 200,
      margin: 2,
      color: {
        dark: isDark ? '#f1f5f9' : '#0f172a',
        light: isDark ? '#161921' : '#ffffff',
      },
    }).catch(() => {});
  }, [inputUrl, appearanceMode]);

  const handleCopyUrl = () => {
    if (!inputUrl) return;
    navigator.clipboard?.writeText(inputUrl).then(() => {
      setCopiedUrl(true);
      setTimeout(() => setCopiedUrl(false), 2000);
    });
  };

  const handleCopyImage = () => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    canvas.toBlob((blob) => {
      if (!blob) return;
      try {
        navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]).then(() => {
          setCopiedImg(true);
          setTimeout(() => setCopiedImg(false), 2000);
        });
      } catch (_) {}
    }, 'image/png');
  };

  const handleDownload = () => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dataUrl = canvas.toDataURL('image/png');
    const a = document.createElement('a');
    let name = 'qrcode';
    try { name = new URL(url).hostname.replace(/^www\./, '') || 'qrcode'; } catch (_) {}
    a.href = dataUrl;
    a.download = `${name}-qr.png`;
    a.click();
    setDownloaded(true);
    setTimeout(() => setDownloaded(false), 2000);
  };

  const displayUrl = inputUrl.length > 50 ? inputUrl.slice(0, 47) + '…' : inputUrl;

  const CheckIcon = () => (
    <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" strokeWidth="3" viewBox="0 0 24 24" strokeLinecap="round" strokeLinejoin="round">
      <polyline points="20 6 9 17 4 12" />
    </svg>
  );

  return (
    <Popup name="qr" title="Share via QR Code">
      <div className="flex flex-col items-center gap-3 py-1">

        {/* QR Canvas */}
        <div className="p-3 rounded-xl border border-slate-200/80 dark:border-slate-700/50 bg-white dark:bg-[#0f1117] shadow-sm">
          <canvas ref={canvasRef} />
        </div>

        {/* Image actions: Copy Image + Download */}
        <div className="w-full flex gap-2">
          <button
            onClick={handleCopyImage}
            disabled={!url}
            className={`flex-1 flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg text-[11px] font-semibold transition-all duration-150 cursor-pointer border
              ${copiedImg
                ? 'bg-emerald-500/10 border-emerald-500/30 text-emerald-600 dark:text-emerald-400'
                : 'bg-slate-100 dark:bg-slate-800/60 border-slate-200/80 dark:border-slate-700/50 text-slate-600 dark:text-slate-300 hover:bg-slate-200/70 dark:hover:bg-slate-700/50 active:scale-95'
              } disabled:opacity-40 disabled:cursor-default`}
          >
            {copiedImg ? <CheckIcon /> : (
              <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24" strokeLinecap="round" strokeLinejoin="round">
                <rect x="8" y="8" width="12" height="12" rx="2"/>
                <path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2"/>
              </svg>
            )}
            {copiedImg ? 'Copied!' : 'Copy Image'}
          </button>

          <button
            onClick={handleDownload}
            disabled={!url}
            className={`flex-1 flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg text-[11px] font-semibold transition-all duration-150 cursor-pointer border
              ${downloaded
                ? 'bg-emerald-500/10 border-emerald-500/30 text-emerald-600 dark:text-emerald-400'
                : 'bg-slate-100 dark:bg-slate-800/60 border-slate-200/80 dark:border-slate-700/50 text-slate-600 dark:text-slate-300 hover:bg-slate-200/70 dark:hover:bg-slate-700/50 active:scale-95'
              } disabled:opacity-40 disabled:cursor-default`}
          >
            {downloaded ? <CheckIcon /> : (
              <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24" strokeLinecap="round" strokeLinejoin="round">
                <path d="M12 3v11"/><path d="m7 9 5 5 5-5"/><path d="M5 21h14"/>
              </svg>
            )}
            {downloaded ? 'Saved!' : 'Download'}
          </button>
        </div>

        {/* Editable URL input */}
        <div className="w-full flex items-center gap-2 px-3 py-2 rounded-lg bg-slate-100 dark:bg-slate-800/60 border border-slate-200/80 dark:border-slate-700/50 focus-within:border-brand-orange/50 transition-colors">
          <svg className="w-3.5 h-3.5 shrink-0 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
            <path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71" />
            <path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71" />
          </svg>
          <input
            type="text"
            value={inputUrl}
            onChange={(e) => setInputUrl(e.target.value)}
            placeholder="Enter URL..."
            className="flex-1 bg-transparent border-none outline-none text-[11px] font-medium text-slate-700 dark:text-slate-200 placeholder-slate-400 min-w-0"
          />
          {inputUrl !== url && (
            <button
              onClick={() => setInputUrl(url)}
              title="Reset to page URL"
              className="shrink-0 text-slate-400 hover:text-slate-600 dark:hover:text-slate-200 transition-colors cursor-pointer"
            >
              <svg className="w-3 h-3" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24" strokeLinecap="round" strokeLinejoin="round">
                <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/>
              </svg>
            </button>
          )}
        </div>

        {/* Copy URL button */}
        <button
          onClick={handleCopyUrl}
          disabled={!url}
          className={`w-full flex items-center justify-center gap-2 px-4 py-2.5 rounded-lg text-[12px] font-semibold transition-all duration-150 cursor-pointer
            ${copiedUrl
              ? 'bg-emerald-500/10 border border-emerald-500/30 text-emerald-600 dark:text-emerald-400'
              : 'bg-brand-orange/10 border border-brand-orange/30 text-brand-orange hover:bg-brand-orange/15 active:scale-95'
            } disabled:opacity-40 disabled:cursor-default`}
        >
          {copiedUrl ? (
            <>
              <CheckIcon />
              Copied!
            </>
          ) : (
            <>
              <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24" strokeLinecap="round" strokeLinejoin="round">
                <rect x="9" y="9" width="13" height="13" rx="2" ry="2" />
                <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1" />
              </svg>
              Copy URL
            </>
          )}
        </button>

        <p className="text-[10px] text-slate-400 dark:text-slate-500 text-center leading-relaxed">
          Scan with your phone camera to open this page
        </p>
      </div>
    </Popup>
  );
};

export default QrCodeViewer;
