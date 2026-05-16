import React, { useEffect, useLayoutEffect, useRef, useState } from 'react';

const getTabIcon = (tab) => {
  const url = tab.url || '';
  const title = (tab.title || '').toLowerCase();
  
  if (url.startsWith('browser://settings') || title === 'settings') {
    return (
      <svg className="w-3.5 h-3.5 mr-2 shrink-0 text-brand-orange" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1-1-1.73l.43.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z" />
        <circle cx="12" cy="12" r="3" />
      </svg>
    );
  }
  if (url.startsWith('browser://bookmarks') || title === 'bookmarks') {
    return (
      <svg className="w-3.5 h-3.5 mr-2 shrink-0 text-brand-orange" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <path d="m19 21-7-4-7 4V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v16z" />
      </svg>
    );
  }
  if (url.startsWith('browser://history') || title === 'history') {
    return (
      <svg className="w-3.5 h-3.5 mr-2 shrink-0 text-brand-orange" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="12" cy="12" r="10" />
        <polyline points="12 6 12 12 16 14" />
      </svg>
    );
  }
  if (url.startsWith('browser://security') || title === 'security') {
    return (
      <svg className="w-3.5 h-3.5 mr-2 shrink-0 text-brand-orange" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z" />
      </svg>
    );
  }
  if (url.startsWith('browser://fingerprints') || title.includes('fingerprint')) {
    return (
      <svg className="w-3.5 h-3.5 mr-2 shrink-0 text-brand-orange" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z" />
        <circle cx="12" cy="12" r="3" />
      </svg>
    );
  }
  if (url.startsWith('browser://downloads') || title === 'downloads') {
    return (
      <svg className="w-3.5 h-3.5 mr-2 shrink-0 text-brand-orange" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <path d="M12 3v11" /><path d="m7 9 5 5 5-5" /><path d="M5 21h14" />
      </svg>
    );
  }

  if (tab.favicon) {
    return <img src={tab.favicon} className="w-3.5 h-3.5 mr-2 shrink-0 object-contain" alt="" />;
  }

  return (
    <svg className="w-3.5 h-3.5 mr-2 shrink-0 text-slate-400" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <circle cx="12" cy="12" r="10" /><line x1="2" y1="12" x2="22" y2="12" /><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z" />
    </svg>
  );
};

const TabStrip = ({ tabs, onSwitch, onClose, onNew }) => {
  const viewportRef = useRef(null);
  const tabRefs = useRef(new Map());
  const [isOverflowing, setIsOverflowing] = useState(false);
  const [canScrollLeft, setCanScrollLeft] = useState(false);
  const [canScrollRight, setCanScrollRight] = useState(false);
  const [hiddenLeft, setHiddenLeft] = useState(0);
  const [hiddenRight, setHiddenRight] = useState(0);

  const measureOverflow = () => {
    const viewport = viewportRef.current;
    if (!viewport) return;

    const maxScrollLeft = Math.max(0, viewport.scrollWidth - viewport.clientWidth);
    const scrollLeft = viewport.scrollLeft;
    const nextOverflowing = maxScrollLeft > 1;
    const nextCanScrollLeft = scrollLeft > 1;
    const nextCanScrollRight = scrollLeft < maxScrollLeft - 1;

    setIsOverflowing(nextOverflowing);
    setCanScrollLeft(nextCanScrollLeft);
    setCanScrollRight(nextCanScrollRight);

    // Calculate hidden counts
    let leftCount = 0;
    let rightCount = 0;
    const viewportRect = viewport.getBoundingClientRect();

    tabRefs.current.forEach((el) => {
      if (!el) return;
      const rect = el.getBoundingClientRect();
      // We use a small buffer (5px) to avoid flickering on sub-pixel positions
      if (rect.right < viewportRect.left + 5) {
        leftCount++;
      } else if (rect.left > viewportRect.right - 5) {
        rightCount++;
      }
    });

    setHiddenLeft(leftCount);
    setHiddenRight(rightCount);
  };

  useLayoutEffect(() => {
    const activeTab = tabs.find((tab) => tab.active);
    if (!activeTab) {
      measureOverflow();
      return;
    }

    const tabEl = tabRefs.current.get(activeTab.id);
    if (tabEl) {
      tabEl.scrollIntoView({ behavior: 'auto', block: 'nearest', inline: 'nearest' });
    }
    measureOverflow();
  }, [tabs]);

  useEffect(() => {
    const viewport = viewportRef.current;
    if (!viewport) return;

    const onScroll = () => measureOverflow();
    const onResize = () => measureOverflow();

    viewport.addEventListener('scroll', onScroll, { passive: true });
    window.addEventListener('resize', onResize);
    measureOverflow();

    return () => {
      viewport.removeEventListener('scroll', onScroll);
      window.removeEventListener('resize', onResize);
    };
  }, [tabs]);

  const scrollTabs = (direction) => {
    const viewport = viewportRef.current;
    if (!viewport) return;
    viewport.scrollBy({ left: direction * Math.max(160, viewport.clientWidth * 0.75), behavior: 'smooth' });
  };

  return (
    <div className="flex items-end h-[29px] bg-slate-300/50 dark:bg-[#020617] overflow-hidden">
      {isOverflowing && (
        <button
          onClick={() => scrollTabs(-1)}
          disabled={!canScrollLeft}
          aria-label="Scroll tabs left"
          title="Scroll left"
          className="h-[29px] min-w-[32px] px-1 flex items-center justify-center shrink-0 border-r border-slate-400/20 dark:border-white/5 text-slate-600 dark:text-slate-400 bg-white dark:bg-[#1a1a20] hover:text-brand-orange transition-all disabled:opacity-30"
        >
          <div className="flex items-center gap-0.5">
            <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
              <path d="m15 18-6-6 6-6" />
            </svg>
            {hiddenLeft > 0 && <span key={`left-${hiddenLeft}`} className="text-[11px] font-black leading-none animate-tab-count">{hiddenLeft}</span>}
          </div>
        </button>
      )}
      <div ref={viewportRef} className="flex-1 min-w-0 overflow-x-auto no-scrollbar px-1 gap-1 flex items-end flex-nowrap">
        {tabs.map((tab, index) => (
          <div 
            key={tab.id}
            onClick={() => onSwitch(tab.id)}
            ref={(el) => {
              if (el) {
                tabRefs.current.set(tab.id, el);
              } else {
                tabRefs.current.delete(tab.id);
              }
            }}
            className={`
              group relative flex items-center h-[29px] px-3 min-w-[140px] max-w-[220px] rounded-t-lg text-[12px] cursor-pointer transition-all duration-150 shrink-0
              ${tab.active 
                ? 'bg-bar-light dark:bg-bar-dark text-slate-900 dark:text-slate-100 shadow-[0_-1px_3px_rgba(0,0,0,0.1)]' 
                : 'text-slate-500 hover:bg-white/50 dark:hover:bg-white/5'}
            `}
          >
            {tab.active && (
              <div className="absolute top-0 left-0 right-0 h-0.5 bg-brand-orange rounded-t-lg" />
            )}
            <div className="flex items-center flex-1 min-w-0 mr-2">
              {getTabIcon(tab)}
              <span className="truncate font-medium">{tab.title || tab.url || 'New Tab'}</span>
            </div>
            <button 
              onClick={(e) => { e.stopPropagation(); onClose(tab.id); }}
              title="Close tab"
              className={`ml-2 w-4 h-4 flex items-center justify-center rounded-full hover:bg-slate-300 dark:hover:bg-white/20 transition-all ${tab.active ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'}`}
            >
              <svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>
            </button>

            {/* Tab Separator */}
            {index < tabs.length - 1 && !tab.active && !tabs[index + 1].active && (
              <div className="absolute right-[-1.5px] top-1.5 bottom-1.5 w-[1px] bg-slate-500/40 dark:bg-white/20 group-hover:opacity-0 transition-opacity" />
            )}
          </div>
        ))}
        <button 
          onClick={() => onNew()}
          title="New tab"
          className={`
            h-[29px] w-9 ml-4 flex items-center justify-center shrink-0 text-slate-600 dark:text-slate-400 bg-white dark:bg-[#1a1a20] hover:text-brand-orange transition-all rounded-md shadow-sm
            ${isOverflowing ? 'sticky right-0 z-10 bg-white dark:bg-[#1a1a20] shadow-[-6px_0_10px_rgba(15,23,42,0.08)] dark:shadow-[-6px_0_10px_rgba(0,0,0,0.25)]' : ''}
          `}
          aria-label="New tab"
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
            <path d="M5 12h14"/><path d="M12 5v14"/>
          </svg>
        </button>
      </div>
      {isOverflowing && (
        <button
          onClick={() => scrollTabs(1)}
          disabled={!canScrollRight}
          aria-label="Scroll tabs right"
          title="Scroll right"
          className="h-[29px] min-w-[32px] px-1 flex items-center justify-center shrink-0 border-l border-slate-300/70 dark:border-white/10 text-slate-600 dark:text-slate-400 bg-white dark:bg-[#1a1a20] hover:text-brand-orange transition-all disabled:opacity-30"
        >
          <div className="flex items-center gap-0.5">
            {hiddenRight > 0 && <span key={`right-${hiddenRight}`} className="text-[11px] font-black leading-none animate-tab-count">{hiddenRight}</span>}
            <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
              <path d="m9 18 6-6-6-6" />
            </svg>
          </div>
        </button>
      )}
    </div>
  );
};

export default TabStrip;
