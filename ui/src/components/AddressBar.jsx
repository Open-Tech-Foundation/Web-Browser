import React, { useState, useRef, useEffect, forwardRef, useImperativeHandle } from 'react';
import SecurityIconButton from './SecurityIconButton';

const AddressBar = forwardRef(({ url: initialUrl, tabId, onNavigate, isBookmarked, onToggleBookmark, sslError, onShowCertificate, onShowClearSiteData, onShowQr, blockedPopupOrigin, onShowBlockedPopup }, ref) => {
  const [url, setUrl] = useState(initialUrl);
  const [isFocused, setIsFocused] = useState(false);
  const inputRef = useRef(null);

  const getDisplayUrl = (value) => {
    if (!value) return '';
    const stripped = value
      .replace(/^https?:\/\//, '')
      .replace(/^browser:\/\//, 'browser://');
    try {
      return decodeURIComponent(stripped);
    } catch (_) {
      return stripped;
    }
  };

  useImperativeHandle(ref, () => ({
    focus: () => {
      inputRef.current?.focus();
    },
    blur: () => {
      inputRef.current?.blur();
    }
  }));

  useEffect(() => {
    setUrl(initialUrl);
  }, [initialUrl, tabId]);

  const handleKeyDown = (e) => {
    if (e.key === 'Enter') {
      let targetUrl = url;
      if (e.ctrlKey && url) {
        if (e.shiftKey) {
          targetUrl = `https://${url}.org`;
        } else {
          targetUrl = `https://${url}.com`;
        }
      }
      onNavigate(targetUrl);
      inputRef.current?.blur();
    }
  };

  const handleFocus = (e) => {
    setIsFocused(true);
    // Use setTimeout to ensure selection happens after browser's default focus behavior
    setTimeout(() => {
      e.target.select();
    }, 0);
  };

  const handleBlur = () => {
    setIsFocused(false);
  };

  const isLocalHttp = url?.startsWith('http://localhost') ||
                      url?.startsWith('http://127.0.0.1');
  const isBlockedHttp = url?.startsWith('http://') && !isLocalHttp;
  const isInsecureBlockPage = url?.startsWith('browser://insecure-blocked') ||
                              url?.includes('/insecure-blocked.html') ||
                              url?.startsWith('chrome-error://') ||
                              url?.startsWith('data:text/html');
  const isSecure = (url?.startsWith('https://') || isLocalHttp) && !sslError;
  const isInsecure = Boolean(sslError || isBlockedHttp || isInsecureBlockPage);
  const showSecurityIcon = isSecure || isInsecure;
  const visibleIsBookmarked = Boolean(isBookmarked);
  const displayUrl = isFocused ? url : getDisplayUrl(url);
  const currentOrigin = (() => { try { return new URL(url || '', window.location.origin).origin; } catch (_) { return ''; } })();
  const hasBlockedPopup = Boolean(blockedPopupOrigin) && blockedPopupOrigin === currentOrigin;

  return (
    <div className="flex flex-1 items-center h-8 bg-input-light dark:bg-input-dark rounded-full px-4 border border-slate-200 dark:border-slate-800 focus-within:border-brand-orange transition-all duration-200 mx-2 group relative">
      {showSecurityIcon && (
        <SecurityIconButton
          insecure={isInsecure}
          onClick={onShowCertificate}
        />
      )}

      <input
        ref={inputRef}
        type="text"
        className={`flex-1 bg-transparent border-none outline-none text-slate-900 dark:text-slate-100 text-[13px] font-medium placeholder-slate-400 min-w-0 relative z-10 ${
          showSecurityIcon ? 'pl-8' : ''
        }`}
        value={displayUrl}
        onFocus={handleFocus}
        onBlur={handleBlur}
        onChange={(e) => setUrl(e.target.value)}
        onKeyDown={handleKeyDown}
        placeholder="Search or enter address..."
      />
      
      {url && !url.startsWith('browser://') && hasBlockedPopup && (
        <div className="relative ml-2">
          <button
            onMouseDown={(e) => e.preventDefault()}
            onClick={onShowBlockedPopup}
            className="p-1 rounded-md transition-all active:scale-90 relative z-10 text-amber-500 hover:text-amber-600 dark:hover:text-amber-400"
            title="Pop-up blocked"
          >
            <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
              <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
              <circle cx="12" cy="12" r="3"/>
            </svg>
          </button>
          <span className="absolute -right-1 -top-1 min-w-[14px] rounded-full bg-amber-500 px-1 text-center text-[9px] font-semibold leading-[14px] text-white">!</span>
        </div>
      )}
      {url && !url.startsWith('browser://') && (
        <button
          onMouseDown={(e) => e.preventDefault()}
          onClick={onToggleBookmark}
          className={`ml-2 p-1 rounded-md transition-all active:scale-90 relative z-10 ${
            visibleIsBookmarked ? 'text-[#D4AF37]' : 'text-slate-400 hover:text-slate-600 dark:hover:text-slate-200'
          }`}
          title={visibleIsBookmarked ? 'Remove bookmark' : 'Bookmark this page'}
        >
          <svg
            xmlns="http://www.w3.org/2000/svg"
            width="14"
            height="14"
            viewBox="0 0 24 24"
            fill={visibleIsBookmarked ? "currentColor" : "none"}
            stroke="currentColor"
            strokeWidth="2.5"
            strokeLinecap="round"
            strokeLinejoin="round"
          >
            <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2" />
          </svg>
        </button>
      )}
      {url && !url.startsWith('browser://') && (
        <button
          onMouseDown={(e) => e.preventDefault()}
          onClick={onShowQr}
          className="ml-2 p-1 rounded-md transition-all active:scale-90 relative z-10 text-slate-400 hover:text-slate-600 dark:hover:text-slate-200"
          title="Share via QR Code"
        >
          <svg
            xmlns="http://www.w3.org/2000/svg"
            width="14"
            height="14"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2.2"
            strokeLinecap="round"
            strokeLinejoin="round"
          >
            <rect x="3" y="3" width="7" height="7" rx="1"/>
            <rect x="14" y="3" width="7" height="7" rx="1"/>
            <rect x="3" y="14" width="7" height="7" rx="1"/>
            <rect x="14" y="14" width="3" height="3"/>
            <path d="M14 17h3v4"/>
            <path d="M20 14v3"/>
          </svg>
        </button>
      )}
      {url && !url.startsWith('browser://') && (
        <button
          onMouseDown={(e) => e.preventDefault()}
          onClick={onShowClearSiteData}
          className="ml-2 p-1 rounded-md transition-all active:scale-90 relative z-10 text-slate-400 hover:text-slate-600 dark:hover:text-slate-200"
          title="Clear site data"
        >
          <svg
            xmlns="http://www.w3.org/2000/svg"
            width="14"
            height="14"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2.5"
            strokeLinecap="round"
            strokeLinejoin="round"
          >
            <path d="M3 6h18" />
            <path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
            <path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6" />
          </svg>
        </button>
      )}
    </div>
  );
});

export default AddressBar;
