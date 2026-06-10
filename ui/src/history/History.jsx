import React, { useState, useEffect, useCallback, useMemo } from 'react';
import { getNativeSettings } from '../shared/nativeRequest';

const formatTime = (timestamp) => {
  if (!timestamp) return '';
  const date = new Date(Number(timestamp) * 1000);
  return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
};

const formatDateGroup = (timestamp) => {
  const date = new Date(Number(timestamp) * 1000);
  const now = new Date();
  const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  const yesterday = new Date(today);
  yesterday.setDate(yesterday.getDate() - 1);

  if (date >= today) return 'Today';
  if (date >= yesterday) return 'Yesterday';
  
  return date.toLocaleDateString([], { weekday: 'long', month: 'long', day: 'numeric' });
};

export default function History() {
  const [items, setItems] = useState([]);
  const [searchQuery, setSearchQuery] = useState('');
  const [activeTab, setActiveTab] = useState('all');
  const [appearanceMode, setAppearanceMode] = useState('auto');
  const [ctxMenu, setCtxMenu] = useState(null);
  const [selectedDate, setSelectedDate] = useState(null);
  const dateInputRef = React.useRef(null);
  const pickerRef = React.useRef(null);

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
    if (window.cefQuery) {
      getNativeSettings()
        .then((settings) => setAppearanceMode(settings.appearanceMode || 'auto'))
        .catch(() => {});
    }
  }, []);

  const load = useCallback(() => {
    window.cefQuery?.({
      request: 'get-history',
      onSuccess: (json) => {
        try {
          setItems(JSON.parse(json));
        } catch {
          setItems([]);
        }
      },
    });
  }, []);

  useEffect(() => {
    load();
  }, [load]);

  const filteredItems = useMemo(() => {
    const now = Date.now();
    return items.filter(item => {
      if (selectedDate) {
        const itemDate = new Date(Number(item.lastVisitAt) * 1000);
        const itemDay = `${itemDate.getFullYear()}-${String(itemDate.getMonth() + 1).padStart(2, '0')}-${String(itemDate.getDate()).padStart(2, '0')}`;
        if (itemDay !== selectedDate) return false;
      } else {
        const sevenDaysAgo = now - 7 * 24 * 60 * 60 * 1000;
        if (Number(item.lastVisitAt) * 1000 < sevenDaysAgo) return false;
      }
      return item.title?.toLowerCase().includes(searchQuery.toLowerCase()) ||
             item.url?.toLowerCase().includes(searchQuery.toLowerCase());
    });
  }, [items, searchQuery, selectedDate]);

  const groupedItems = useMemo(() => {
    const groups = {};
    filteredItems.forEach(item => {
      const group = formatDateGroup(item.lastVisitAt);
      if (!groups[group]) groups[group] = [];
      groups[group].push(item);
    });
    return groups;
  }, [filteredItems]);

  const clearHistory = () => {
    if (confirm("Are you sure you want to clear this workspace's browsing history?")) {
      window.cefQuery?.({ request: 'clear-history', onSuccess: load });
    }
  };

  const deleteItem = (id) => {
    window.cefQuery?.({ request: `delete-history-item:${id}`, onSuccess: load });
  };

  const navigateTo = (url) => {
    window.cefQuery?.({ request: `navigate-current:${url}` });
  };

  useEffect(() => {
    if (!ctxMenu) return;
    const close = () => setCtxMenu(null);
    window.addEventListener('click', close);
    window.addEventListener('scroll', close, true);
    return () => {
      window.removeEventListener('click', close);
      window.removeEventListener('scroll', close, true);
    };
  }, [ctxMenu]);

  return (
    <div className="flex h-screen bg-main text-main font-sans overflow-hidden">
      {/* Sidebar */}
      <aside className="w-72 bg-card/50 backdrop-blur-xl border-r border-main flex flex-col py-8 shrink-0">
        <div className="px-8 pb-10 flex items-center gap-3">
          <div className="w-8 h-8 bg-orange-500 rounded-lg flex items-center justify-center shadow-lg shadow-orange-500/20">
             <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12 8v5l3 3"/><circle cx="12" cy="12" r="9"/></svg>
          </div>
          <span className="text-xl font-bold bg-gradient-to-r from-main to-muted bg-clip-text text-transparent">History</span>
        </div>
        
        <nav className="flex-grow px-4 space-y-1">
          <button 
            onClick={() => setActiveTab('all')}
            className={`w-full text-left px-4 py-3 rounded-xl flex items-center gap-4 transition-all duration-200 text-sm font-medium group ${
              activeTab === 'all' 
                ? 'bg-orange-500/10 text-orange-500 shadow-[inset_0_0_0_1px_rgba(249,115,22,0.2)]' 
                : 'text-muted hover:text-main hover:bg-card/50'
            }`}
          >
            <span className={`transition-colors duration-200 ${activeTab === 'all' ? 'text-orange-400' : 'text-muted group-hover:text-main'}`}>
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 8v5l3 3"/><circle cx="12" cy="12" r="9"/></svg>
            </span>
            Workspace History
          </button>
          
          <button 
            onClick={clearHistory}
            className="w-full text-left px-4 py-3 rounded-xl flex items-center gap-4 transition-all duration-200 text-sm font-medium text-red-400/80 hover:text-red-400 hover:bg-red-400/5 group"
          >
            <span className="text-red-400/60 group-hover:text-red-400">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M3 6h18"/><path d="M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6"/><path d="M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
            </span>
            Clear Workspace History
          </button>
        </nav>
      </aside>

      {/* Content Area */}
      <main className="flex-grow p-12 md:p-20 overflow-y-auto">
        <div className="max-w-5xl mx-auto">
          <div className="flex items-center gap-3 mb-12">
            <div className="flex items-center bg-card border border-main rounded-2xl
                            focus-within:border-orange-500/40 focus-within:bg-card
                            transition-all duration-300 flex-1 max-w-xl shadow-2xl backdrop-blur-md">
              <svg className="w-5 h-5 ml-4 text-muted shrink-0" xmlns="http://www.w3.org/2000/svg"
                   width="24" height="24" viewBox="0 0 24 24" fill="none"
                   stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>
              </svg>
              <input
                type="text"
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                placeholder="Search history..."
                className="w-full bg-transparent border-none outline-none text-main text-base
                           placeholder-muted py-4 pl-4 pr-3"
              />
              {searchQuery && (
                <button
                  onClick={() => setSearchQuery('')}
                  className="mr-2 p-1 cursor-pointer text-muted hover:text-main rounded-lg hover:bg-main/10 transition-colors"
                >
                  <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                </button>
              )}
            </div>

            <button
              ref={pickerRef}
              onClick={() => {
                if (!dateInputRef.current) return;
                const rect = pickerRef.current.getBoundingClientRect();
                const input = dateInputRef.current;
                input.style.position = 'fixed';
                input.style.left = rect.left + 'px';
                input.style.top = rect.top + 'px';
                input.style.width = rect.width + 'px';
                input.style.height = rect.height + 'px';
                input.style.opacity = '0.01';
                requestAnimationFrame(() => input.showPicker());
              }}
              className={`shrink-0 flex items-center gap-2 h-11 px-3.5 rounded-xl border transition-all duration-200 cursor-pointer ${
                selectedDate
                  ? 'bg-orange-500/10 border-orange-500/30 text-orange-500'
                  : 'bg-card border-main text-muted hover:text-main hover:border-main/60'
              }`}
            >
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="shrink-0"><rect x="3" y="4" width="18" height="18" rx="2" ry="2"/><line x1="16" y1="2" x2="16" y2="6"/><line x1="8" y1="2" x2="8" y2="6"/><line x1="3" y1="10" x2="21" y2="10"/></svg>
              <span className="text-sm font-medium tabular-nums">
                {selectedDate
                  ? new Date(selectedDate + 'T00:00:00').toLocaleDateString([], { month: 'short', day: 'numeric' })
                  : 'Last 7 days'}
              </span>
            </button>
          </div>

          <input
            ref={dateInputRef}
            type="date"
            onChange={(e) => setSelectedDate(e.target.value || null)}
            style={{ position: 'fixed', left: 0, top: 0, width: 1, height: 1, padding: 0, border: 'none', margin: 0, opacity: 0, pointerEvents: 'none' }}
          />

          <div className="animate-in fade-in slide-in-from-bottom-6 duration-700">
            {items.length === 0 ? (
              <div className="flex flex-col items-center justify-center py-32 text-center">
                 <div className="w-24 h-24 bg-main/5 rounded-3xl flex items-center justify-center mb-8 border border-main">
                    <svg className="text-muted" width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12 8v5l3 3"/><circle cx="12" cy="12" r="9"/></svg>
                 </div>
                 <h3 className="text-2xl font-bold text-main mb-2">No Workspace History Yet</h3>
                 <p className="text-muted max-w-sm">Websites you visit in this workspace will be shown here for easy access later.</p>
              </div>
            ) : filteredItems.length === 0 ? (
              <div className="text-center py-20 text-muted">
                {selectedDate
                  ? `No history for ${new Date(selectedDate + 'T00:00:00').toLocaleDateString([], { weekday: 'long', month: 'long', day: 'numeric' })}`
                  : searchQuery
                    ? `No results found for "${searchQuery}"`
                    : 'No history in the last 7 days'}
              </div>
            ) : (
              Object.entries(groupedItems).map(([date, groupItems]) => (
                <section key={date} className="mb-12">
                  <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">{date}</h2>
                  <div className="space-y-1 bg-main/5 rounded-3xl border border-main overflow-hidden backdrop-blur-sm">
                    {groupItems.map((item) => (
                      <div 
                        key={item.id} 
                        className="group flex items-center gap-4 px-6 py-4 hover:bg-main/5 transition-all duration-200"
                        onContextMenu={(e) => {
                          e.preventDefault();
                          setCtxMenu({ x: e.clientX, y: e.clientY, url: item.url, title: item.title || item.url });
                        }}
                      >
                        <div className="w-4 h-4 text-muted group-hover:text-orange-400 transition-colors shrink-0">
                          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/></svg>
                        </div>
                        <div className="flex-grow min-w-0">
                          <div className="flex items-center gap-2">
                            <button 
                              onClick={() => navigateTo(item.url)}
                              className="truncate text-sm font-bold text-main hover:text-orange-400 transition-colors text-left"
                            >
                              {item.title || item.url}
                            </button>
                            <span className="text-[10px] text-muted shrink-0 tabular-nums">{formatTime(item.lastVisitAt)}</span>
                          </div>
                          <button 
                            onClick={() => navigateTo(item.url)}
                            className="truncate text-xs text-muted mt-0.5 hover:text-orange-400 transition-colors block text-left"
                          >
                            {item.url}
                          </button>
                        </div>
                        <button
                          onClick={() => deleteItem(item.id)}
                          className="opacity-0 group-hover:opacity-100 p-2 text-muted hover:text-red-400 transition-all rounded-lg hover:bg-red-400/10"
                          title="Remove from history"
                        >
                          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M3 6h18"/><path d="M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6"/><path d="M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2"/></svg>
                        </button>
                      </div>
                    ))}
                  </div>
                </section>
              ))
            )}
          </div>

          {ctxMenu && (
            <div
              style={{ left: ctxMenu.x, top: ctxMenu.y, position: 'fixed' }}
              className="z-50 min-w-[200px] bg-[var(--bg-card)] border border-[var(--border-main)] rounded-lg shadow-[0_8px_30px_rgba(0,0,0,0.25)] overflow-hidden py-1"
              onClick={(e) => e.stopPropagation()}
            >
              <button
                className="w-full text-left px-4 py-2 text-sm text-[var(--text-main)] hover:text-white hover:bg-orange-500 flex items-center gap-3"
                onClick={() => {
                  window.cefQuery?.({ request: `new-tab:${ctxMenu.url}` });
                  setCtxMenu(null);
                }}
              >
                <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round" className="shrink-0"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></svg>
                Open in new tab
              </button>
              <div className="h-px bg-[var(--border-main)] mx-3" />
              <button
                className="w-full text-left px-4 py-2 text-sm text-[var(--text-main)] hover:text-white hover:bg-orange-500 flex items-center gap-3"
                onClick={() => {
                  navigator.clipboard?.writeText(ctxMenu.url);
                  window.cefQuery?.({ request: 'toast:copy:Link copied' });
                  setCtxMenu(null);
                }}
              >
                <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round" className="shrink-0"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>
                Copy link
              </button>
            </div>
          )}
        </div>
      </main>
    </div>
  );
}
