import React, { useEffect, useLayoutEffect, useRef, useState } from 'react';

const TabStrip = ({ tabs, onSwitch, onClose, onNew }) => {
  const viewportRef = useRef(null);
  const tabRefs = useRef(new Map());
  const [isOverflowing, setIsOverflowing] = useState(false);
  const [canScrollLeft, setCanScrollLeft] = useState(false);
  const [canScrollRight, setCanScrollRight] = useState(false);

  const measureOverflow = () => {
    const viewport = viewportRef.current;
    if (!viewport) return;

    const maxScrollLeft = Math.max(0, viewport.scrollWidth - viewport.clientWidth);
    const nextOverflowing = maxScrollLeft > 1;
    const nextCanScrollLeft = viewport.scrollLeft > 1;
    const nextCanScrollRight = viewport.scrollLeft < maxScrollLeft - 1;

    setIsOverflowing((prev) => (prev === nextOverflowing ? prev : nextOverflowing));
    setCanScrollLeft((prev) => (prev === nextCanScrollLeft ? prev : nextCanScrollLeft));
    setCanScrollRight((prev) => (prev === nextCanScrollRight ? prev : nextCanScrollRight));
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
    <div className="flex items-end h-[24px] bg-slate-200 dark:bg-slate-900/80 overflow-hidden">
      {isOverflowing && (
        <button
          onClick={() => scrollTabs(-1)}
          disabled={!canScrollLeft}
          aria-label="Scroll tabs left"
          className="h-[24px] w-7 flex items-center justify-center shrink-0 border-r border-slate-300/70 dark:border-white/10 text-slate-600 dark:text-slate-400 hover:text-brand-orange hover:bg-white/50 dark:hover:bg-white/5 transition-all disabled:opacity-30 disabled:hover:bg-transparent"
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
            <path d="m15 18-6-6 6-6" />
          </svg>
        </button>
      )}
      <div ref={viewportRef} className="flex-1 min-w-0 overflow-x-auto no-scrollbar px-1 gap-0.5 flex items-end flex-nowrap">
        {tabs.map(tab => (
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
              group relative flex items-center h-[24px] px-3 min-w-[140px] max-w-[220px] rounded-t-lg text-[11px] cursor-pointer transition-all duration-150 shrink-0
              ${tab.active 
                ? 'bg-bar-light dark:bg-bar-dark text-slate-900 dark:text-slate-100 shadow-[0_-1px_3px_rgba(0,0,0,0.1)]' 
                : 'text-slate-500 hover:bg-white/50 dark:hover:bg-white/5'}
            `}
          >
            {tab.active && (
              <div className="absolute top-0 left-0 right-0 h-0.5 bg-brand-orange rounded-t-lg" />
            )}
            <div className="flex items-center flex-1 min-w-0 mr-2">
              {tab.favicon ? (
                <img src={tab.favicon} className="w-3.5 h-3.5 mr-2 shrink-0 object-contain" alt="" />
              ) : (
                <svg className="w-3.5 h-3.5 mr-2 shrink-0 text-slate-400" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/></svg>
              )}
              <span className="truncate font-medium">{tab.title || tab.url || 'New Tab'}</span>
            </div>
            <button 
              onClick={(e) => { e.stopPropagation(); onClose(tab.id); }}
              className="ml-2 w-4 h-4 flex items-center justify-center rounded-full opacity-0 group-hover:opacity-100 hover:bg-slate-300 dark:hover:bg-white/20 transition-all"
            >
              <svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>
            </button>
          </div>
        ))}
        <button 
          onClick={() => onNew()}
          className={`
            h-[24px] w-9 flex items-center justify-center shrink-0 border-l border-slate-300/70 dark:border-white/10 text-slate-600 dark:text-slate-400 hover:text-brand-orange hover:bg-white/50 dark:hover:bg-white/5 transition-all
            ${isOverflowing ? 'sticky right-0 z-10 bg-slate-200 dark:bg-slate-900/80 shadow-[-6px_0_10px_rgba(15,23,42,0.08)] dark:shadow-[-6px_0_10px_rgba(0,0,0,0.25)]' : ''}
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
          className="h-[24px] w-7 flex items-center justify-center shrink-0 border-l border-slate-300/70 dark:border-white/10 text-slate-600 dark:text-slate-400 hover:text-brand-orange hover:bg-white/50 dark:hover:bg-white/5 transition-all disabled:opacity-30 disabled:hover:bg-transparent"
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
            <path d="m9 18 6-6-6-6" />
          </svg>
        </button>
      )}
    </div>
  );
};

export default TabStrip;
