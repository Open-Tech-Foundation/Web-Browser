import React, { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';

const PinnedBadge = () => (
  <svg className="w-3.5 h-3.5 shrink-0 text-slate-500 dark:text-slate-400" viewBox="0 0 24 24" fill="currentColor" aria-label="Pinned tab">
    <path d="M7 2v11l-2 2v2h6v5l1 1 1-1v-5h6v-2l-2-2V2H7zm2 2h6v9.17l1.17 1.17H7.83L9 13.17V4z"/>
  </svg>
);

const PrivateBadge = () => (
  <svg className="w-[18px] h-[18px] shrink-0 text-violet-500 dark:text-violet-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" aria-label="Private tab">
    <path d="M2 13h20" /><path d="M5 13l1.5-5.5A2 2 0 0 1 8.4 6h7.2a2 2 0 0 1 1.9 1.5L19 13" /><circle cx="7" cy="16" r="2.5" /><circle cx="17" cy="16" r="2.5" /><path d="M9.5 16h5" />
  </svg>
);

const getDomain = (url) => {
  if (!url) return '';
  try {
    const u = new URL(url);
    return u.hostname;
  } catch {
    return '';
  }
};

const getTabTooltip = (tab) => {
  const title = tab.title || 'New Tab';
  const domain = getDomain(tab.url);
  return domain ? `${title}\n${domain}` : title;
};

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
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" /><polyline points="14 2 14 8 20 8" />
    </svg>
  );
};

const SplitBadge = () => (
  <svg className="w-3 h-3 shrink-0 text-brand-orange" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.3" strokeLinecap="round" strokeLinejoin="round" aria-label="Split tab">
    <rect x="3" y="4" width="18" height="16" rx="2" />
    <path d="M12 4v16" />
  </svg>
);

const TabStrip = ({ tabs, onSwitch, onClose, onNew, onSplit, splitActive = false, splitView = {} }) => {
  if (tabs.length === 0) return null;
  const pinnedTabs = useMemo(() => tabs.filter((t) => t.pinned), [tabs]);
  const unpinnedTabs = useMemo(() => tabs.filter((t) => !t.pinned), [tabs]);
  const sortedTabs = useMemo(() => [...pinnedTabs, ...unpinnedTabs], [pinnedTabs, unpinnedTabs]);
  const splitLeftId = Number(splitView.leftTabId ?? -1);
  const splitRightId = Number(splitView.rightTabId ?? -1);
  const splitLeftTab = tabs.find((tab) => tab.id === splitLeftId);
  const splitRightTab = tabs.find((tab) => tab.id === splitRightId);
  const displayUnpinnedTabs = useMemo(() => {
    if (!splitActive || splitLeftId < 0 || splitRightId < 0) return unpinnedTabs;
    return unpinnedTabs.filter((tab) => tab.id !== splitRightId);
  }, [splitActive, splitLeftId, splitRightId, unpinnedTabs]);

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

    let leftCount = 0;
    let rightCount = 0;
    const viewportRect = viewport.getBoundingClientRect();

    // Account for the sticky new-tab button (w-9 = 36px) that overlays tabs at the right edge
    const stickyBtnWidth = 36;

    tabRefs.current.forEach((el) => {
      if (!el || !viewport.contains(el)) return;
      const rect = el.getBoundingClientRect();
      if (rect.right < viewportRect.left + 5) {
        leftCount++;
      } else if (rect.left > viewportRect.right - stickyBtnWidth - 5) {
        rightCount++;
      }
    });

    setIsOverflowing(maxScrollLeft > 1);
    setCanScrollLeft(leftCount > 0);
    setCanScrollRight(rightCount > 0);
    setHiddenLeft(leftCount);
    setHiddenRight(rightCount);
  };

  useLayoutEffect(() => {
    const activeTab = displayUnpinnedTabs.find((tab) => tab.active || tab.id === splitLeftId);
    if (!activeTab) {
      measureOverflow();
      return;
    }

    const tabEl = tabRefs.current.get(activeTab.id);
    if (tabEl) {
      tabEl.scrollIntoView({ behavior: 'auto', block: 'nearest', inline: 'nearest' });
    }
    measureOverflow();

    const viewport = viewportRef.current;
    if (viewport) {
      const lastTab = displayUnpinnedTabs[displayUnpinnedTabs.length - 1];
      if (lastTab && activeTab.id === lastTab.id) {
        const maxScrollLeft = Math.max(0, viewport.scrollWidth - viewport.clientWidth);
        viewport.scrollLeft = maxScrollLeft;
        measureOverflow();
      }
    }
  }, [tabs, displayUnpinnedTabs, splitLeftId]);

  useEffect(() => {
    const viewport = viewportRef.current;
    if (!viewport) return;
    const onScroll = () => measureOverflow();
    viewport.addEventListener('scroll', onScroll, { passive: true });
    measureOverflow();
    return () => viewport.removeEventListener('scroll', onScroll);
  }, [tabs]);

  useEffect(() => {
    const onResize = () => measureOverflow();
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, []);

  const scrollTabs = (direction) => {
    const viewport = viewportRef.current;
    if (!viewport) return;
    viewport.scrollBy({ left: direction * Math.max(160, viewport.clientWidth * 0.75), behavior: 'smooth' });
  };

  const handleWheel = (e) => {
    const viewport = viewportRef.current;
    if (!viewport) return;
    if (Math.abs(e.deltaY) < Math.abs(e.deltaX)) return;
    e.preventDefault();
    viewport.scrollBy({ left: e.deltaY, behavior: 'auto' });
  };

  const renderSplitSegment = (tab, side) => {
    if (!tab) return null;
    const isActive = tab.active;
    return (
      <button
        type="button"
        title={getTabTooltip(tab)}
        onClick={(e) => {
          e.preventDefault();
          e.stopPropagation();
          onSwitch(tab.id);
        }}
        className={`
          flex min-w-0 flex-1 items-center gap-1.5 px-2 h-full transition-all
          ${side === 'left' ? 'rounded-tl-lg' : 'rounded-tr-lg'}
          ${isActive ? 'bg-bar-light text-slate-900 dark:bg-bar-dark dark:text-slate-100' : 'text-slate-500 hover:bg-white/50 dark:hover:bg-white/5'}
        `}
      >
        {side === 'left' && <SplitBadge />}
        {getTabIcon(tab)}
        <span className="truncate font-medium">{tab.title || tab.url || 'New Tab'}</span>
      </button>
    );
  };

  const renderSplitGroup = (index) => (
    <div
      key={`split-${splitLeftId}-${splitRightId}`}
      ref={(el) => {
        if (el) {
          tabRefs.current.set(splitLeftId, el);
        } else {
          tabRefs.current.delete(splitLeftId);
        }
      }}
      className="group relative flex h-[29px] min-w-[260px] max-w-[420px] shrink-0 overflow-hidden rounded-t-lg ring-1 ring-inset ring-brand-orange/40 bg-brand-orange/5 dark:bg-brand-orange/10"
    >
      <div className="absolute left-0 right-0 top-0 h-0.5 bg-brand-orange" />
      {renderSplitSegment(splitLeftTab, 'left')}
      <div className="my-1.5 w-px shrink-0 bg-brand-orange/40" />
      {renderSplitSegment(splitRightTab, 'right')}
      <button
        onClick={(e) => {
          e.stopPropagation();
          e.preventDefault();
          window.cefQuery({ request: 'close-split' });
        }}
        title="Close split view"
        className="mx-1 my-auto flex h-4 w-4 shrink-0 items-center justify-center rounded-full opacity-0 transition-all hover:bg-slate-300 group-hover:opacity-100 dark:hover:bg-white/20"
      >
        <svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>
      </button>
      {index < sortedTabs.length - 1 && (
        <div className="absolute right-[-1.5px] top-1.5 bottom-1.5 w-[1px] bg-slate-500/40 dark:bg-white/20 group-hover:opacity-0 transition-opacity" />
      )}
    </div>
  );

  const renderTab = (tab, index) => (
    <a
      key={tab.id}
      href={`tab-context-menu:${tab.id}`}
      title={getTabTooltip(tab)}
      onClick={(e) => {
        e.preventDefault();
        onSwitch(tab.id);
      }}
      ref={(el) => {
        if (el) {
          tabRefs.current.set(tab.id, el);
        } else {
          tabRefs.current.delete(tab.id);
        }
      }}
      className={`
        group relative flex items-center h-[29px] rounded-t-lg text-[12px] cursor-pointer transition-all duration-150 shrink-0 select-none no-underline
        ${tab.pinned
          ? 'min-w-[36px] max-w-[36px] px-0 justify-center mx-0.5'
          : 'min-w-[140px] max-w-[220px] px-3'}
        ${tab.private
          ? 'bg-violet-500/5 dark:bg-violet-500/10 ring-1 ring-inset ring-violet-500/40'
          : ''}
        ${tab.splitPane
          ? 'bg-brand-orange/5 dark:bg-brand-orange/10'
          : ''}
        ${tab.splitPane
          ? 'ring-1 ring-inset ring-brand-orange/40'
          : ''}
        ${tab.active
          ? 'bg-bar-light dark:bg-bar-dark text-slate-900 dark:text-slate-100 shadow-[0_-1px_3px_rgba(0,0,0,0.1)]'
          : 'text-slate-500 hover:bg-white/50 dark:hover:bg-white/5'}
      `}
    >
      {tab.active && (
        <div className={`absolute top-0 left-0 right-0 h-0.5 rounded-t-lg ${tab.private ? 'bg-violet-500' : 'bg-brand-orange'}`} />
      )}
      {tab.pinned ? (
        <div className="flex items-center justify-center w-full">
          {tab.favicon ? (
            <img src={tab.favicon} className="w-3.5 h-3.5 object-contain" alt="" />
          ) : (
            <svg className="w-3.5 h-3.5 shrink-0 text-slate-500 dark:text-slate-400" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" /><polyline points="14 2 14 8 20 8" />
            </svg>
          )}
        </div>
      ) : (
        <div className="flex items-center gap-1.5 flex-1 min-w-0 mr-2">
          {tab.private && <PrivateBadge />}
          {tab.splitPane && <SplitBadge />}
          {getTabIcon(tab)}
          {tab.muted && (
            <button
              onClick={(e) => { e.stopPropagation(); e.preventDefault(); window.cefQuery({ request: `unmute-tab:${tab.id}` }); }}
              title="Unmute tab"
              className="w-4 h-4 shrink-0 flex items-center justify-center rounded-full hover:bg-slate-300 dark:hover:bg-white/20 transition-all text-slate-500 hover:text-slate-800 dark:text-slate-400 dark:hover:text-slate-100"
            >
              <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M11 5L6 9H2v6h4l5 4V5z"/><line x1="23" y1="9" x2="17" y2="15"/><line x1="17" y1="9" x2="23" y2="15"/></svg>
            </button>
          )}
          <span className="truncate font-medium">{tab.title || tab.url || 'New Tab'}</span>
        </div>
      )}
      {!tab.pinned && (
        <button
          onClick={(e) => { e.stopPropagation(); e.preventDefault(); onClose(tab.id); }}
          title="Close tab"
          className={`ml-2 w-4 h-4 flex items-center justify-center rounded-full hover:bg-slate-300 dark:hover:bg-white/20 transition-all ${tab.active ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'}`}
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>
        </button>
      )}
      {!tab.pinned && splitActive && onSplit && !tab.splitPane && (
        <button
          onClick={(e) => { e.stopPropagation(); e.preventDefault(); onSplit(tab.id); }}
          title="Open in split view"
          className={`ml-1 w-4 h-4 flex items-center justify-center rounded-full hover:bg-slate-300 dark:hover:bg-white/20 transition-all ${tab.active ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'}`}
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M12 4v16"/><path d="M4 8h6"/><path d="M14 8h6"/></svg>
        </button>
      )}

      {/* Tab Separator */}
      {!tab.pinned && index < sortedTabs.length - 1 && !tab.active && !sortedTabs[index + 1].active && (
        <div className="absolute right-[-1.5px] top-1.5 bottom-1.5 w-[1px] bg-slate-500/40 dark:bg-white/20 group-hover:opacity-0 transition-opacity" />
      )}
    </a>
  );

  return (
    <div className="flex items-end h-[29px] bg-slate-300/50 dark:bg-[#020617] overflow-hidden">
      {/* Pinned tabs: fixed, never scroll */}
      {pinnedTabs.length > 0 && (
        <div className="flex items-end shrink-0 px-1 gap-1">
          {pinnedTabs.map((tab, i) => renderTab(tab, i))}
        </div>
      )}

      {/* Separator between pinned and unpinned */}
      {pinnedTabs.length > 0 && unpinnedTabs.length > 0 && (
        <div className="w-px h-4 shrink-0 bg-slate-500/50 dark:bg-white/20 mb-1.5 mx-1" />
      )}

      {/* Scroll arrows left */}
      {isOverflowing && hiddenLeft > 0 && (
        <button
          onClick={() => scrollTabs(-1)}
          aria-label="Scroll tabs left"
          title="Scroll left"
          className="h-[29px] min-w-[32px] px-1 flex items-center justify-center shrink-0 border-r border-slate-400/20 dark:border-white/5 text-slate-600 dark:text-slate-400 bg-white dark:bg-[#1a1a20] hover:text-brand-orange transition-all"
        >
          <div className="flex items-center gap-0.5">
            <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
              <path d="m15 18-6-6 6-6" />
            </svg>
            {hiddenLeft > 0 && <span key={`left-${hiddenLeft}`} className="text-[11px] font-black leading-none animate-tab-count">{hiddenLeft}</span>}
          </div>
        </button>
      )}

      {/* Unpinned tabs: scrollable */}
      <div ref={viewportRef} onWheel={handleWheel} className="flex-1 min-w-0 overflow-x-auto no-scrollbar px-1 gap-1 flex items-end flex-nowrap">
        {displayUnpinnedTabs.map((tab, i) => (
          splitActive && tab.id === splitLeftId && splitLeftTab && splitRightTab
            ? renderSplitGroup(pinnedTabs.length + i)
            : renderTab(tab, pinnedTabs.length + i)
        ))}
        <a
          href="tab-context-menu:newtab"
          onClick={(e) => {
            e.preventDefault();
            onNew();
          }}
          title="New tab"
          className={`
            h-[29px] w-9 ml-4 flex items-center justify-center shrink-0 text-slate-600 dark:text-slate-400 bg-white dark:bg-[#1a1a20] hover:text-brand-orange transition-all rounded-md shadow-sm cursor-pointer
            ${isOverflowing ? 'sticky right-0 z-10 bg-white dark:bg-[#1a1a20] shadow-[-6px_0_10px_rgba(15,23,42,0.08)] dark:shadow-[-6px_0_10px_rgba(0,0,0,0.25)]' : ''}
          `}
          aria-label="New tab"
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
            <path d="M5 12h14"/><path d="M12 5v14"/>
          </svg>
        </a>
      </div>

      {/* Scroll arrows right */}
      {isOverflowing && hiddenRight > 0 && (
        <button
          onClick={() => scrollTabs(1)}
          aria-label="Scroll tabs right"
          title="Scroll right"
          className="h-[29px] min-w-[32px] px-1 flex items-center justify-center shrink-0 border-l border-slate-300/70 dark:border-white/10 text-slate-600 dark:text-slate-400 bg-white dark:bg-[#1a1a20] hover:text-brand-orange transition-all"
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
