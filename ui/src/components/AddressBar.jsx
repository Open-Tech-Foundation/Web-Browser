import React, { useState, useRef, useEffect, forwardRef, useImperativeHandle } from 'react';

const AddressBar = forwardRef(({ url: initialUrl, tabId, onNavigate, isBookmarked, onToggleBookmark }, ref) => {
  const [url, setUrl] = useState(initialUrl);
  const inputRef = useRef(null);

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
    }
  };

  return (
    <div className="flex flex-1 items-center h-7 bg-input-light dark:bg-input-dark rounded px-2 border border-transparent focus-within:border-brand-orange transition-all duration-200 mx-2 group">
      <input
        ref={inputRef}
        type="text"
        className="flex-1 bg-transparent border-none outline-none text-slate-900 dark:text-slate-100 text-xs placeholder-slate-400 min-w-0"
        value={url}
        onChange={(e) => setUrl(e.target.value)}
        onKeyPress={handleKeyPress}
        placeholder="Search or enter address..."
      />
      {url && !url.startsWith('browser://') && (
        <button
          onClick={onToggleBookmark}
          className={`ml-1 p-1 rounded-md transition-all active:scale-90 ${
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
