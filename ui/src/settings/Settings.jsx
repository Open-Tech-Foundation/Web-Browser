import React, { useState, useEffect } from 'react';
import * as Icons from './Icons';

const engines = [
  { id: 'google', name: 'Google', Icon: Icons.GoogleIcon },
  { id: 'bing', name: 'Microsoft Bing', Icon: Icons.BingIcon },
  { id: 'yahoo', name: 'Yahoo', Icon: Icons.YahooIcon },
  { id: 'duckduckgo', name: 'DuckDuckGo', Icon: Icons.DuckDuckGoIcon },
  { id: 'baidu', name: 'Baidu', Icon: Icons.BaiduIcon },
  { id: 'yandex', name: 'Yandex', Icon: Icons.YandexIcon },
  { id: 'ecosia', name: 'Ecosia', Icon: Icons.EcosiaIcon },
  { id: 'naver', name: 'Naver', Icon: Icons.NaverIcon },
  { id: 'startpage', name: 'Startpage', Icon: Icons.StartpageIcon }
];

const Settings = () => {
  const [selectedEngine, setSelectedEngine] = useState('');
  const [activeMenu, setActiveMenu] = useState('search');

  useEffect(() => {
    if (window.cefQuery) {
      window.cefQuery({
        request: 'get-settings',
        onSuccess: (response) => {
          try {
            const settings = JSON.parse(response);
            setSelectedEngine(settings.searchEngine || '');
          } catch (e) {
            console.error('Failed to parse settings:', e);
          }
        }
      });
    }
  }, []);

  const selectEngine = (id) => {
    setSelectedEngine(id);
    if (window.cefQuery) {
      window.cefQuery({
        request: `set-settings:{"searchEngine":"${id}"}`,
        onSuccess: () => console.log('Settings saved')
      });
    }
  };

  return (
    <div className="flex h-screen bg-[#0f172a] text-slate-100 font-sans overflow-hidden">
      {/* Sidebar */}
      <aside className="w-64 bg-[#1e293b] border-r border-white/10 flex flex-col py-6 shrink-0">
        <div className="px-6 pb-6 text-xl font-bold text-orange-500 tracking-tight">
          OTF Settings
        </div>
        <nav className="flex-grow">
          <button 
            onClick={() => setActiveMenu('search')}
            className={`w-full text-left px-6 py-3 flex items-center gap-3 transition-all text-sm font-medium ${activeMenu === 'search' ? 'bg-orange-500/10 text-slate-100 border-r-2 border-orange-500' : 'text-slate-400 hover:text-slate-100 hover:bg-white/5'}`}
          >
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg>
            Search Engine
          </button>
          <button 
            onClick={() => setActiveMenu('appearance')}
            className={`w-full text-left px-6 py-3 flex items-center gap-3 transition-all text-sm font-medium ${activeMenu === 'appearance' ? 'bg-orange-500/10 text-slate-100 border-r-2 border-orange-500' : 'text-slate-400 hover:text-slate-100 hover:bg-white/5'}`}
          >
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect><line x1="3" y1="9" x2="21" y2="9"></line><line x1="9" y1="21" x2="9" y2="9"></line></svg>
            Appearance
          </button>
          <button 
            onClick={() => setActiveMenu('privacy')}
            className={`w-full text-left px-6 py-3 flex items-center gap-3 transition-all text-sm font-medium ${activeMenu === 'privacy' ? 'bg-orange-500/10 text-slate-100 border-r-2 border-orange-500' : 'text-slate-400 hover:text-slate-100 hover:bg-white/5'}`}
          >
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path></svg>
            Privacy & Security
          </button>
        </nav>
      </aside>

      {/* Content Area */}
      <main className="flex-grow p-10 md:p-16 overflow-y-auto bg-[#0f172a]">
        {activeMenu === 'search' && (
          <div className="max-w-4xl animate-in fade-in slide-in-from-bottom-4 duration-500">
            <header className="mb-10">
              <h1 className="text-3xl font-bold mb-2">Search Engine</h1>
              <p className="text-slate-400">Choose the search engine used in the address bar.</p>
            </header>

            <section>
              <h2 className="text-lg font-semibold mb-6 pb-3 border-b border-white/10 text-slate-200">
                Default Search Engine
              </h2>
              <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4">
                {engines.map(({ id, name, Icon }) => (
                  <button
                    key={id}
                    onClick={() => selectEngine(id)}
                    className={`relative group flex items-center gap-4 p-5 rounded-xl border transition-all duration-200 text-left ${
                      selectedEngine === id 
                        ? 'bg-orange-500/5 border-orange-500 shadow-[0_0_0_1px_rgba(249,115,22,1)]' 
                        : 'bg-[#1e293b] border-white/10 hover:border-white/20 hover:-translate-y-0.5 hover:bg-[#334155]'
                    }`}
                  >
                    {selectedEngine === id && (
                      <div className="absolute -top-2 -right-2 w-6 h-6 bg-green-500 text-white rounded-full flex items-center justify-center text-xs font-bold shadow-lg shadow-green-500/40 border-2 border-[#0f172a] z-10 animate-in zoom-in duration-300">
                        ✓
                      </div>
                    )}
                    <div className="w-10 h-10 shrink-0 bg-white/5 p-2 rounded-lg flex items-center justify-center">
                      <Icon className="w-full h-full object-contain" />
                    </div>
                    <div className="flex-grow overflow-hidden">
                      <span className="block font-semibold text-sm truncate">{name}</span>
                    </div>
                  </button>
                ))}
              </div>
            </section>
          </div>
        )}

        {activeMenu !== 'search' && (
          <div className="flex flex-col items-center justify-center h-full text-slate-400 space-y-4">
             <div className="w-16 h-16 bg-white/5 rounded-full flex items-center justify-center">
                <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83"/></svg>
             </div>
             <p className="text-lg">Section under development</p>
          </div>
        )}
      </main>
    </div>
  );
};

export default Settings;
