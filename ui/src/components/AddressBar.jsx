import React, { useState, useRef, useEffect, forwardRef, useImperativeHandle } from 'react';

const AddressBar = forwardRef(({ url: initialUrl, tabId, onNavigate, isBookmarked, onToggleBookmark, sslError }, ref) => {
  const [url, setUrl] = useState(initialUrl);
  const [isFocused, setIsFocused] = useState(false);
  const inputRef = useRef(null);

  const getDisplayUrl = (value) => {
    if (!value) return '';
    return value.replace(/^https?:\/\//, '').replace(/^browser:\/\//, 'browser://');
  };

  useImperativeHandle(ref, () => ({
    focus: () => {
      inputRef.current?.focus();
    }
  }));

  useEffect(() => {
    setUrl(initialUrl);
  }, [initialUrl, tabId]);

  const handleKeyPress = (e) => {
    if (e.key === 'Enter') {
      onNavigate(url);
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
  const isSecure = (url?.startsWith('https://') || isLocalHttp) && !sslError;
  const isInsecure = Boolean(sslError || isBlockedHttp);
  const displayUrl = isFocused ? url : getDisplayUrl(url);

  return (
    <div className="flex flex-1 items-center h-8 bg-input-light dark:bg-input-dark rounded-lg px-3 border border-transparent focus-within:border-brand-orange transition-all duration-200 mx-2 group relative">
      {/* Security Icon */}
      {((isSecure || isInsecure) && !isFocused) && (
        <div className={`mr-2 animate-in fade-in zoom-in duration-300 ${isInsecure ? 'text-red-500' : 'text-green-500'}`}>
          <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
            <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z" />
            {isInsecure ? (
              <>
                <line x1="12" y1="8" x2="12" y2="12" />
                <line x1="12" y1="16" x2="12.01" y2="16" />
              </>
            ) : (
              <polyline points="9 12 11 14 15 10" />
            )}
          </svg>
        </div>
      )}
      
      <input
        ref={inputRef}
        type="text"
        className="flex-1 bg-transparent border-none outline-none text-slate-900 dark:text-slate-100 text-[13px] font-medium placeholder-slate-400 min-w-0"
        value={displayUrl}
        onFocus={handleFocus}
        onBlur={handleBlur}
        onChange={(e) => setUrl(e.target.value)}
        onKeyPress={handleKeyPress}
        placeholder="Search or enter address..."
      />
      
      {url && !url.startsWith('browser://') && (
        <button
          onClick={onToggleBookmark}
          className={`ml-2 p-1 rounded-md transition-all active:scale-90 ${
            isBookmarked ? 'text-[#D4AF37]' : 'text-slate-400 hover:text-slate-600 dark:hover:text-slate-200'
          }`}
          title={isBookmarked ? 'Remove bookmark' : 'Bookmark this page'}
        >
          <svg 
            xmlns="http://www.w3.org/2000/svg" 
            width="14" 
            height="14" 
            viewBox="0 0 24 24" 
            fill={isBookmarked ? "currentColor" : "none"} 
            stroke="currentColor" 
            strokeWidth="2.5" 
            strokeLinecap="round" 
            strokeLinejoin="round"
          >
            <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2" />
          </svg>
        </button>
      )}
    </div>
  );
});

export default AddressBar;
