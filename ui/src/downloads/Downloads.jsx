import React, { useState, useEffect, useCallback, useMemo } from 'react';

export default function Downloads() {
  const [downloads, setDownloads] = useState([]);
  const [searchQuery, setSearchQuery] = useState('');

  const load = useCallback(() => {
    window.cefQuery?.({
      request: 'get-downloads',
      onSuccess: (json) => {
        try {
          setDownloads(JSON.parse(json));
        } catch {
          setDownloads([]);
        }
      },
    });
  }, []);

  useEffect(() => {
    load();
    // Subscribe to download updates
    window.cefQuery?.({
      request: 'downloads-subscribe',
      persistent: true,
      onSuccess: (json) => {
        try {
          const event = JSON.parse(json);
          if (event.key === 'downloads-update' && event.downloads) {
            setDownloads(event.downloads);
          } else if (event.key === 'downloads-refresh') {
            load();
          }
        } catch (e) {}
      }
    });
  }, [load]);

  const filteredDownloads = useMemo(() => {
    if (!searchQuery.trim()) return downloads;
    const q = searchQuery.toLowerCase();
    return downloads.filter(item => 
      item.suggestedName?.toLowerCase().includes(q) || 
      item.url?.toLowerCase().includes(q)
    );
  }, [downloads, searchQuery]);

  const handleAction = (action, id) => {
    window.cefQuery?.({ request: `${action}-download:${id}` });
  };

  const clearFinished = () => {
    window.cefQuery?.({ 
      request: 'clear-finished-downloads',
      onSuccess: load
    });
  };

  const formatSize = (bytes) => {
    if (!bytes) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  return (
    <div className="flex h-screen bg-main text-main overflow-hidden font-sans selection:bg-orange-500/30">
      {/* Sidebar */}
      <aside className="w-72 bg-card/50 backdrop-blur-xl border-r border-main flex flex-col py-8 shrink-0">
        <div className="px-8 pb-10 flex items-center gap-3">
          <div className="w-10 h-10 bg-orange-500 rounded-xl flex items-center justify-center text-white shadow-lg shadow-orange-500/20">
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
               <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>
            </svg>
          </div>
          <h1 className="text-xl font-bold tracking-tight">Downloads</h1>
        </div>
        
        <nav className="flex-grow px-4 space-y-1">
          <button className="w-full flex items-center gap-3 px-4 py-3 rounded-xl bg-orange-500/10 text-orange-500 font-bold text-sm transition-all border border-orange-500/20">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
            All Downloads
          </button>
          <button className="w-full flex items-center gap-3 px-4 py-3 rounded-xl hover:bg-main/5 text-muted font-medium text-sm transition-all" onClick={() => window.cefQuery?.({ request: 'navigate-current:browser://history' })}>
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
            History
          </button>
        </nav>

        <div className="mt-auto p-8">
           <button 
             onClick={clearFinished}
             className="w-full p-4 rounded-2xl bg-main/5 border border-main hover:bg-main/10 hover:text-main text-[10px] text-muted uppercase tracking-widest font-bold transition-all"
           >
             Clear Finished
           </button>
        </div>
      </aside>

      {/* Main Content */}
      <main className="flex-1 flex flex-col min-w-0">
        {/* Header/Search */}
        <header className="h-24 border-b border-main flex items-center px-12 gap-8 sticky top-0 bg-main/80 backdrop-blur-md z-10">
          <div className="flex-1 relative group">
            <div className="absolute inset-y-0 left-4 flex items-center pointer-events-none text-muted group-focus-within:text-orange-500 transition-colors">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>
            </div>
            <input
              type="text"
              placeholder="Search downloads..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="w-full h-12 bg-main/5 border border-main rounded-2xl pl-12 pr-6 text-sm outline-none focus:border-orange-500/30 focus:bg-main/10 transition-all placeholder-muted"
            />
          </div>
          <div className="flex gap-3">
             <button onClick={load} className="p-3 rounded-xl bg-main/5 hover:bg-main/10 text-muted transition-all border border-main">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.85.83 6.72 2.38L21 8"/><path d="M21 3v5h-5"/></svg>
             </button>
          </div>
        </header>

        {/* Scrollable Content */}
        <div className="flex-1 overflow-y-auto p-12">
          {filteredDownloads.length === 0 ? (
            <div className="flex flex-col items-center justify-center h-full text-muted">
               <div className="w-16 h-16 rounded-full bg-main/5 flex items-center justify-center mb-4">
                  <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
               </div>
               <p className="text-sm font-medium">{searchQuery ? 'No downloads match your search' : 'No downloads yet'}</p>
            </div>
          ) : (
            <div className="space-y-4">
              {filteredDownloads.map((item) => (
                <div key={item.id} className="group flex items-center gap-6 p-6 rounded-2xl bg-card border border-main hover:border-orange-500/30 hover:bg-card/80 transition-all relative overflow-hidden">
                  {/* Progress Background */}
                  {item.isInProgress && (
                    <div 
                      className="absolute bottom-0 left-0 h-1 bg-orange-500/20 transition-all duration-300" 
                      style={{ width: `${item.percent}%` }}
                    />
                  )}

                  <div className="w-14 h-14 rounded-xl bg-main/5 flex items-center justify-center text-muted group-hover:text-orange-400 transition-colors shrink-0 border border-main">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                      <path d="M14.5 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7.5L14.5 2z"/><polyline points="14 2 14 8 20 8"/>
                    </svg>
                  </div>
                  
                  <div className="flex-1 min-w-0">
                    <div className="flex items-center gap-3">
                      <h3 className="text-sm font-bold truncate text-main group-hover:text-orange-400 transition-colors">
                        {item.suggestedName || 'Download'}
                      </h3>
                      {item.status && (
                        <span className={`text-[10px] uppercase tracking-widest font-black px-2 py-0.5 rounded-md ${
                          item.isComplete ? 'bg-green-500/10 text-green-500' : 
                          item.isCanceled || item.isInterrupted ? 'bg-red-500/10 text-red-500' :
                          'bg-orange-500/10 text-orange-500'
                        }`}>
                          {item.status}
                        </span>
                      )}
                    </div>
                    <div className="flex items-center gap-3 mt-1.5 text-xs text-muted">
                      <span className="truncate max-w-[300px]">{item.url}</span>
                      <span>•</span>
                      <span>{item.isComplete ? formatSize(item.totalBytes) : `${formatSize(item.receivedBytes)} / ${formatSize(item.totalBytes)}`}</span>
                      {item.isInProgress && (
                        <>
                          <span>•</span>
                          <span className="text-orange-500 font-bold">{item.percent}%</span>
                        </>
                      )}
                    </div>
                  </div>

                  <div className="flex gap-2 pr-2">
                    {item.canOpen && (
                      <button 
                        onClick={() => handleAction('open', item.id)}
                        className="px-4 py-2 rounded-xl bg-orange-500 text-white text-xs font-bold hover:bg-orange-600 transition-all shadow-lg shadow-orange-500/20"
                      >
                        Open
                      </button>
                    )}
                    {item.canShowInFolder && (
                      <button 
                        onClick={() => handleAction('show-download-in-folder', item.id)}
                        className="p-2.5 rounded-xl bg-main/5 hover:bg-main/10 text-muted hover:text-main transition-all border border-main"
                        title="Show in folder"
                      >
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/></svg>
                      </button>
                    )}
                    {item.canPause && (
                      <button 
                        onClick={() => handleAction('pause', item.id)}
                        className="p-2.5 rounded-xl bg-main/5 hover:bg-main/10 text-muted hover:text-main transition-all border border-main"
                        title="Pause"
                      >
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="6" y="4" width="4" height="16"/><rect x="14" y="4" width="4" height="16"/></svg>
                      </button>
                    )}
                    {item.canResume && (
                      <button 
                        onClick={() => handleAction('resume', item.id)}
                        className="p-2.5 rounded-xl bg-main/5 hover:bg-main/10 text-muted hover:text-main transition-all border border-main"
                        title="Resume"
                      >
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><polygon points="5 3 19 12 5 21 5 3"/></svg>
                      </button>
                    )}
                    {item.canCancel && (
                      <button 
                        onClick={() => handleAction('cancel', item.id)}
                        className="p-2.5 rounded-xl bg-main/5 hover:bg-red-500/20 text-muted hover:text-red-500 transition-all border border-main"
                        title="Cancel"
                      >
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                      </button>
                    )}
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
