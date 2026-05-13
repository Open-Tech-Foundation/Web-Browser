import React, { useState, useEffect, useCallback, useMemo } from 'react';

export default function Bookmarks() {
  const [items, setItems] = useState([]);
  const [searchQuery, setSearchQuery] = useState('');

  const load = useCallback(() => {
    window.cefQuery?.({
      request: 'get-bookmarks',
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
    if (!searchQuery.trim()) return items;
    const q = searchQuery.toLowerCase();
    return items.filter(item => 
      item.title?.toLowerCase().includes(q) || 
      item.url?.toLowerCase().includes(q)
    );
  }, [items, searchQuery]);

  const navigateTo = (url) => {
    window.cefQuery?.({ request: `navigate-current:${url}` });
  };

  const removeBookmark = (id) => {
    window.cefQuery?.({ 
      request: `remove-bookmark:${id}`, 
      onSuccess: load 
    });
  };

  return (
    <div className="flex h-screen bg-[#020617] text-slate-200 overflow-hidden font-sans selection:bg-orange-500/30">
      {/* Sidebar */}
      <aside className="w-72 bg-[#020617] border-r border-white/5 flex flex-col">
        <div className="p-8">
          <div className="flex items-center gap-3 mb-10">
            <div className="w-10 h-10 bg-orange-500 rounded-xl flex items-center justify-center text-white shadow-lg shadow-orange-500/20">
              <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <path d="m19 21-7-4-7 4V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v16z"/>
              </svg>
            </div>
            <h1 className="text-xl font-bold tracking-tight">Bookmarks</h1>
          </div>
          
          <nav className="space-y-1">
            <button className="w-full flex items-center gap-3 px-4 py-3 rounded-xl bg-white/5 text-orange-500 font-bold text-sm transition-all border border-orange-500/20">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="m19 21-7-4-7 4V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v16z"/></svg>
              All Bookmarks
            </button>
            <button className="w-full flex items-center gap-3 px-4 py-3 rounded-xl hover:bg-white/5 text-slate-400 font-medium text-sm transition-all" onClick={() => window.cefQuery?.({ request: 'navigate-current:browser://history' })}>
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
              History
            </button>
          </nav>
        </div>

        <div className="mt-auto p-8">
           <div className="p-4 rounded-2xl bg-white/5 border border-white/5 text-[10px] text-slate-500 uppercase tracking-widest font-bold leading-relaxed">
             Safe & Secure Storage
           </div>
        </div>
      </aside>

      {/* Main Content */}
      <main className="flex-1 flex flex-col min-w-0 bg-[#020617]">
        {/* Header/Search */}
        <header className="h-24 border-b border-white/5 flex items-center px-12 gap-8 sticky top-0 bg-[#020617]/80 backdrop-blur-md z-10">
          <div className="flex-1 relative group">
            <div className="absolute inset-y-0 left-4 flex items-center pointer-events-none text-slate-500 group-focus-within:text-orange-500 transition-colors">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>
            </div>
            <input
              type="text"
              placeholder="Search bookmarks..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="w-full h-12 bg-white/5 border border-white/5 rounded-2xl pl-12 pr-6 text-sm outline-none focus:border-orange-500/30 focus:bg-white/10 transition-all placeholder-slate-600"
            />
          </div>
          <div className="flex gap-3">
             <button onClick={load} className="p-3 rounded-xl bg-white/5 hover:bg-white/10 text-slate-400 transition-all border border-white/5">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.85.83 6.72 2.38L21 8"/><path d="M21 3v5h-5"/></svg>
             </button>
          </div>
        </header>

        {/* Scrollable Content */}
        <div className="flex-1 overflow-y-auto p-12">
          {filteredItems.length === 0 ? (
            <div className="flex flex-col items-center justify-center h-full text-slate-500">
               <div className="w-16 h-16 rounded-full bg-white/5 flex items-center justify-center mb-4">
                  <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"><path d="m19 21-7-4-7 4V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v16z"/></svg>
               </div>
               <p className="text-sm font-medium">{searchQuery ? 'No bookmarks match your search' : 'No bookmarks saved yet'}</p>
            </div>
          ) : (
            <div className="grid grid-cols-1 xl:grid-cols-2 gap-4">
              {filteredItems.map((item) => (
                <div key={item.id} className="group flex items-center gap-4 p-4 rounded-2xl bg-white/5 border border-white/5 hover:border-orange-500/30 hover:bg-white/10 transition-all">
                  <div className="w-12 h-12 rounded-xl bg-slate-800/50 flex items-center justify-center text-slate-400 group-hover:text-orange-400 transition-colors shrink-0">
                    <div className="text-lg font-bold">
                       {(item.title || item.url)[0].toUpperCase()}
                    </div>
                  </div>
                  
                  <div className="flex-1 min-w-0">
                    <h3 className="text-sm font-bold truncate text-white group-hover:text-orange-400 transition-colors">
                      {item.title || 'Untitled'}
                    </h3>
                    <p className="text-xs text-slate-500 truncate mt-1">
                      {item.url}
                    </p>
                  </div>

                  <div className="flex gap-1 opacity-0 group-hover:opacity-100 transition-opacity pr-2">
                    <button 
                      onClick={() => navigateTo(item.url)}
                      className="p-2.5 rounded-lg hover:bg-orange-500/20 text-slate-400 hover:text-orange-400 transition-all"
                      title="Open in current tab"
                    >
                      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></svg>
                    </button>
                    <button 
                      onClick={() => removeBookmark(item.id)}
                      className="p-2.5 rounded-lg hover:bg-red-500/20 text-slate-400 hover:text-red-400 transition-all"
                      title="Delete bookmark"
                    >
                      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M3 6h18"/><path d="M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6"/><path d="M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2"/></svg>
                    </button>
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
      </main>
    </div>
  );
}
