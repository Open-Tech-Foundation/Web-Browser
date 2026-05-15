import React, { useState, useEffect } from 'react';

const QuickLink = ({ name, url, faviconUrl, onRemove }) => {
  const handleClick = () => {
    if (window.cefQuery) {
      window.cefQuery({ request: `navigate-current:${url}` });
    }
  };

  return (
    <div className="relative group">
      <button
        onClick={handleClick}
        className="flex flex-col items-center gap-3 transition-all duration-300 hover:-translate-y-1"
      >
        <div className="w-16 h-16 bg-card border border-main rounded-2xl flex items-center justify-center transition-all duration-300 
                      group-hover:bg-card group-hover:border-orange-500/30 group-hover:shadow-[0_0_20px_-5px_rgba(249,115,22,0.3)] backdrop-blur-sm overflow-hidden">
          {faviconUrl ? (
            <img src={faviconUrl} alt="" className="w-8 h-8 object-contain" />
          ) : (
            <div className="w-8 h-8 bg-main/5 rounded-lg flex items-center justify-center text-sm font-bold text-muted group-hover:text-orange-400 transition-colors">
              {name[0].toUpperCase()}
            </div>
          )}
        </div>
        <span className="text-xs font-medium text-muted group-hover:text-main transition-colors truncate w-20 text-center">{name}</span>
      </button>
      
      <button 
        onClick={(e) => { e.stopPropagation(); onRemove(); }}
        className="absolute -top-1 -right-1 w-5 h-5 bg-card border border-main rounded-full flex items-center justify-center 
                   opacity-0 group-hover:opacity-100 transition-opacity hover:bg-red-500/20 hover:border-red-500/50 text-muted hover:text-red-400 z-10"
      >
        <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
      </button>
    </div>
  );
};

const QuickLinks = () => {
  const [links, setLinks] = useState([]);
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [newName, setNewName] = useState('');
  const [newUrl, setNewUrl] = useState('');

  const fetchLinks = () => {
    if (window.cefQuery) {
      window.cefQuery({
        request: 'get-bookmarks',
        onSuccess: (json) => {
          try {
            setLinks(JSON.parse(json));
          } catch (e) {
            setLinks([]);
          }
        }
      });
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
    
    if (window.cefQuery) {
      const request = `add-bookmark:${url.length}:${url}:${newName.length}:${newName}`;
      window.cefQuery({
        request,
        onSuccess: () => {
          fetchLinks();
          setNewName('');
          setNewUrl('');
          setIsModalOpen(false);
        }
      });
    }
  };

  const handleRemove = (id) => {
    if (window.cefQuery && id) {
      window.cefQuery({
        request: `remove-bookmark:${id}`,
        onSuccess: () => fetchLinks()
      });
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
          />
        ))}
        
        <button 
          onClick={() => setIsModalOpen(true)}
          className="group flex flex-col items-center gap-3 transition-all duration-300 hover:-translate-y-1"
        >
          <div className="w-16 h-16 bg-main/5 border border-main border-dashed rounded-2xl flex items-center justify-center transition-all duration-300 group-hover:bg-main/10 group-hover:border-muted backdrop-blur-sm">
            <svg className="w-6 h-6 text-muted group-hover:text-main" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
          </div>
          <span className="text-xs font-medium text-muted group-hover:text-main transition-colors">Add Site</span>
        </button>
      </div>

      {/* Add Link Modal */}
      {isModalOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center p-6 bg-black/60 backdrop-blur-sm animate-in fade-in duration-300">
          <div className="w-full max-w-md bg-card border border-main rounded-3xl p-8 shadow-2xl animate-in zoom-in-95 duration-300">
            <h3 className="text-xl font-bold text-main mb-6">Add Quick Link</h3>
            <form onSubmit={handleAdd} className="space-y-5">
              <div>
                <label className="block text-xs font-bold text-muted uppercase tracking-wider mb-2">Name</label>
                <input 
                  type="text" 
                  value={newName}
                  onChange={(e) => setNewName(e.target.value)}
                  placeholder="e.g. Google"
                  className="w-full bg-main/5 border border-main rounded-xl px-4 py-3 text-main outline-none focus:border-orange-500/50 transition-all"
                  autoFocus
                />
              </div>
              <div>
                <label className="block text-xs font-bold text-muted uppercase tracking-wider mb-2">URL</label>
                <input 
                  type="text" 
                  value={newUrl}
                  onChange={(e) => setNewUrl(e.target.value)}
                  placeholder="e.g. google.com"
                  className="w-full bg-main/5 border border-main rounded-xl px-4 py-3 text-main outline-none focus:border-orange-500/50 transition-all"
                />
              </div>
              <div className="flex gap-4 pt-4">
                <button 
                  type="button"
                  onClick={() => setIsModalOpen(false)}
                  className="flex-1 px-4 py-3 bg-main/5 hover:bg-main/10 text-muted font-bold rounded-xl transition-all"
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
    </div>
  );
};

export default QuickLinks;
