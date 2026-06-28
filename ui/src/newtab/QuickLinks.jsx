import React, { useState, useEffect } from 'react';
import { isBridgeAvailable, nativeRequest } from '../shared/nativeRequest';

const GlobeIcon = ({ isPrivate }) => (
  <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" className={`${isPrivate ? 'text-violet-300' : 'text-muted'}`}>
    <circle cx="12" cy="12" r="10"/><path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/><path d="M2 12h20"/>
  </svg>
);

const QuickLink = ({ name, url, faviconUrl, onRemove, isPrivate }) => {
  const handleClick = () => {
    if (isBridgeAvailable()) {
      nativeRequest({ method: 'navigation.current', params: { url } }).catch(() => {});
    }
  };

  const cardBg = isPrivate ? 'bg-violet-950/60 border-violet-500/20 group-hover:bg-violet-950/80' : 'bg-card border-main group-hover:bg-card';
  const cardBorder = isPrivate ? 'border-violet-500/20 group-hover:border-violet-400/40' : 'border-main group-hover:border-orange-500/30';

  return (
    <div className="relative group">
      <button
        onClick={handleClick}
        className="flex flex-col items-center gap-3 transition-all duration-300 hover:-translate-y-1"
      >
        <div className={`w-16 h-16 rounded-2xl flex items-center justify-center transition-all duration-300 backdrop-blur-sm overflow-hidden ${cardBg} border ${cardBorder} ${isPrivate ? 'group-hover:shadow-[0_0_20px_-5px_rgba(139,92,246,0.3)]' : 'group-hover:shadow-[0_0_20px_-5px_rgba(249,115,22,0.3)]'}`}>
          {faviconUrl
            ? <img src={faviconUrl} alt="" className="w-8 h-8 object-contain" />
            : <GlobeIcon isPrivate={isPrivate} />
          }
        </div>
        <span className={`text-xs font-medium transition-colors truncate w-20 text-center ${isPrivate ? 'text-violet-200 group-hover:text-white' : 'text-muted group-hover:text-main'}`}>{name}</span>
      </button>
      
      <button 
        onClick={(e) => { e.stopPropagation(); onRemove(); }}
        className={`absolute -top-1 -right-1 w-5 h-5 rounded-full flex items-center justify-center 
                   opacity-0 group-hover:opacity-100 transition-opacity hover:bg-red-500/20 hover:border-red-500/50 z-10 ${isPrivate ? 'bg-violet-950/60 border-violet-500/20 text-violet-300 hover:text-red-400' : 'bg-card border-main text-muted hover:text-red-400'}`}
      >
        <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
      </button>
    </div>
  );
};

const QuickLinks = ({ isPrivate }) => {
  const [links, setLinks] = useState([]);
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [newName, setNewName] = useState('');
  const [newUrl, setNewUrl] = useState('');
  const [linkToRemove, setLinkToRemove] = useState(null);

  const fetchLinks = () => {
    if (isBridgeAvailable()) {
      nativeRequest({ method: 'bookmarks.list' })
        .then((bookmarks) => setLinks(Array.isArray(bookmarks) ? bookmarks : []))
        .catch(() => setLinks([]));
    }
  };

  useEffect(() => {
    fetchLinks();
  }, []);

  const handleAdd = (e) => {
    e.preventDefault();
    if (!newName || !newUrl) return;
    
    let url = newUrl.trim();
    if (!url.startsWith('http')) url = 'https://' + url;
    
    if (isBridgeAvailable()) {
      nativeRequest({
        method: 'bookmarks.add',
        params: { url, title: newName },
      })
        .then(() => {
          fetchLinks();
          setNewName('');
          setNewUrl('');
          setIsModalOpen(false);
        })
        .catch(() => {});
    }
  };

  const handleRemove = (id) => {
    setLinkToRemove(id);
  };

  const confirmRemove = () => {
    if (isBridgeAvailable() && linkToRemove) {
      nativeRequest({ method: 'bookmarks.remove', params: { id: linkToRemove } })
        .then(() => {
          fetchLinks();
          setLinkToRemove(null);
        })
        .catch(() => {});
    }
  };

  return (
    <div className="mt-16 w-full max-w-3xl">
      <div className="grid grid-cols-4 sm:grid-cols-6 md:grid-cols-8 gap-8 animate-in fade-in slide-in-from-bottom-8 duration-1000 delay-300 justify-items-center">
        {links.map((link) => (
          <QuickLink 
            key={link.id} 
            name={link.title || 'Link'} 
            url={link.url} 
            faviconUrl={link.faviconUrl}
            onRemove={() => handleRemove(link.id)}
            isPrivate={isPrivate}
          />
        ))}
        
        <button 
          onClick={() => setIsModalOpen(true)}
          className="group flex flex-col items-center gap-3 transition-all duration-300 hover:-translate-y-1"
        >
          <div className={`w-16 h-16 border border-dashed rounded-2xl flex items-center justify-center transition-all duration-300 backdrop-blur-sm ${isPrivate ? 'bg-white/5 border-violet-500/30 group-hover:bg-white/10 group-hover:border-violet-300' : 'bg-main/5 border-main group-hover:bg-main/10 group-hover:border-muted'}`}>
            <svg className={`w-6 h-6 transition-colors ${isPrivate ? 'text-violet-300 group-hover:text-white' : 'text-muted group-hover:text-main'}`} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
          </div>
          <span className={`text-xs font-medium transition-colors ${isPrivate ? 'text-violet-200 group-hover:text-white' : 'text-muted group-hover:text-main'}`}>Add Site</span>
        </button>
      </div>

      {/* Add Link Modal */}
      {isModalOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center p-6 bg-black/60 backdrop-blur-sm animate-in fade-in duration-300">
          <div className="w-full max-w-md bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 rounded-3xl p-8 shadow-2xl animate-in zoom-in-95 duration-300">
            <h3 className="text-xl font-bold text-slate-900 dark:text-white mb-6">Add Quick Link</h3>
            <form onSubmit={handleAdd} className="space-y-5">
              <div>
                <label className="block text-xs font-bold text-slate-500 dark:text-slate-400 uppercase tracking-wider mb-2">Name</label>
                <input 
                  type="text" 
                  value={newName}
                  onChange={(e) => setNewName(e.target.value)}
                  placeholder="e.g. Google"
                  className="w-full bg-slate-50 dark:bg-slate-800 border border-slate-200 dark:border-slate-700 rounded-xl px-4 py-3 text-slate-900 dark:text-white outline-none focus:border-orange-500/50 transition-all"
                  autoFocus
                />
              </div>
              <div>
                <label className="block text-xs font-bold text-slate-500 dark:text-slate-400 uppercase tracking-wider mb-2">URL</label>
                <input 
                  type="text" 
                  value={newUrl}
                  onChange={(e) => setNewUrl(e.target.value)}
                  placeholder="e.g. google.com"
                  className="w-full bg-slate-50 dark:bg-slate-800 border border-slate-200 dark:border-slate-700 rounded-xl px-4 py-3 text-slate-900 dark:text-white outline-none focus:border-orange-500/50 transition-all"
                />
              </div>
              <div className="flex gap-4 pt-4">
                <button 
                  type="button"
                  onClick={() => setIsModalOpen(false)}
                  className="flex-1 px-4 py-3 bg-slate-100 hover:bg-slate-200 dark:bg-slate-800 dark:hover:bg-slate-700 text-slate-600 dark:text-slate-300 font-bold rounded-xl transition-all"
                >
                  Cancel
                </button>
                <button 
                  type="submit"
                  className="flex-1 px-4 py-3 bg-orange-500 hover:bg-orange-600 text-white font-bold rounded-xl shadow-lg shadow-orange-500/20 transition-all"
                >
                  Add Link
                </button>
              </div>
            </form>
          </div>
        </div>
      )}

      {/* Remove Link Modal */}
      {linkToRemove && (
        <div className="fixed inset-0 z-50 flex items-center justify-center p-6 bg-black/60 backdrop-blur-sm animate-in fade-in duration-300">
          <div className="w-full max-w-sm bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 rounded-3xl p-8 shadow-2xl animate-in zoom-in-95 duration-300">
            <h3 className="text-xl font-bold text-slate-900 dark:text-white mb-4">Remove Quick Link?</h3>
            <p className="text-slate-500 dark:text-slate-400 text-sm mb-6">Are you sure you want to remove this quick link? This action cannot be undone.</p>
            <div className="flex gap-4">
              <button 
                onClick={() => setLinkToRemove(null)}
                className="flex-1 px-4 py-3 bg-slate-100 hover:bg-slate-200 dark:bg-slate-800 dark:hover:bg-slate-700 text-slate-600 dark:text-slate-300 font-bold rounded-xl transition-all"
              >
                Cancel
              </button>
              <button 
                onClick={confirmRemove}
                className="flex-1 px-4 py-3 bg-red-500 hover:bg-red-600 text-white font-bold rounded-xl shadow-lg shadow-red-500/20 transition-all"
              >
                Remove
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default QuickLinks;
