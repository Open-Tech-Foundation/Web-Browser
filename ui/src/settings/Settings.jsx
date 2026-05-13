import React, { useState, useEffect } from 'react';
import * as Icons from './Icons';
import { resolveUrl } from '../shared/search';

const GenericIcon = () => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="w-full h-full text-slate-400">
    <circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>
  </svg>
);

const EngineLogo = ({ id, name }) => {
  const [error, setError] = useState(false);
  return error ? <GenericIcon /> : (
    <img 
      src={`/assets/logos/${id}.svg`} 
      alt={name} 
      className="w-full h-full object-contain" 
      onError={() => setError(true)} 
    />
  );
};

const engines = [
  { id: 'baidu', name: 'Baidu' },
  { id: 'bing', name: 'Bing' },
  { id: 'duckduckgo', name: 'DuckDuckGo' },
  { id: 'ecosia', name: 'Ecosia' },
  { id: 'google', name: 'Google' },
  { id: 'naver', name: 'Naver' },
  { id: 'startpage', name: 'Startpage' },
  { id: 'yahoo', name: 'Yahoo' },
  { id: 'yandex', name: 'Yandex' }
];

const searchStateByTab = {};

const Settings = () => {
  const [selectedEngine, setSelectedEngine] = useState('');
  const [activeMenu, setActiveMenu] = useState('search');
  const [tabId, setTabId] = useState(null);

  const cached = tabId != null ? searchStateByTab[tabId] : null;
  const [searchQuery, setSearchQuery] = useState(cached ? cached.query : '');
  const [searchEngine, setSearchEngine] = useState(cached ? cached.engine : '');

  useEffect(() => {
    if (tabId != null) {
      searchStateByTab[tabId] = { query: searchQuery, engine: searchEngine };
    }
  }, [searchQuery, searchEngine, tabId]);

  useEffect(() => {
    if (tabId != null) {
      const s = searchStateByTab[tabId];
      setSearchQuery(s ? s.query || '' : '');
      setSearchEngine(s ? s.engine || '' : '');
    }
  }, [tabId]);

  useEffect(() => {
    if (window.cefQuery) {
      window.cefQuery({
        request: 'get-my-tab-id',
        onSuccess: (id) => setTabId(parseInt(id))
      });
    }
  }, []);

  useEffect(() => {
    if (window.cefQuery) {
      window.cefQuery({
        request: 'get-settings',
        onSuccess: (response) => {
          try {
            const settings = JSON.parse(response);
            setSelectedEngine(settings.searchEngine || '');
            setSearchEngine(settings.searchEngine || '');
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

  const handleSearchKeyDown = (e) => {
    if (e.key === 'Enter' && searchQuery.trim()) {
      window.cefQuery({ request: `navigate-current:${resolveUrl(searchQuery.trim(), searchEngine)}` });
    }
  };

  const menuItems = [
    { id: 'search', label: 'Search Engine', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg> },
    { id: 'appearance', label: 'Appearance', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect><line x1="3" y1="9" x2="21" y2="9"></line><line x1="9" y1="21" x2="9" y2="9"></line></svg> },
    { id: 'privacy', label: 'Privacy & Security', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path></svg> },
    { id: 'shortcuts', label: 'Keyboard Shortcuts', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M15 9h4a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2z"/><path d="M5 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M15 3h4a2 2 0 0 1 2 2v4a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/></svg> },
    { id: 'about', label: 'About', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="16" x2="12" y2="12"></line><line x1="12" y1="8" x2="12.01" y2="8"></line></svg> },
  ];

  return (
    <div className="flex h-screen bg-[#020617] text-slate-100 font-sans overflow-hidden">
      {/* Sidebar */}
      <aside className="w-72 bg-[#0f172a]/50 backdrop-blur-xl border-r border-white/5 flex flex-col py-8 shrink-0">
        <div className="px-8 pb-10 flex items-center gap-3">
          <div className="w-8 h-8 bg-orange-500 rounded-lg flex items-center justify-center shadow-lg shadow-orange-500/20">
             <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3"/></svg>
          </div>
          <span className="text-xl font-bold bg-gradient-to-r from-white to-slate-400 bg-clip-text text-transparent">Settings</span>
        </div>
        
        <nav className="flex-grow px-4 space-y-1">
          {menuItems.map(item => (
            <button 
              key={item.id}
              onClick={() => setActiveMenu(item.id)}
              className={`w-full text-left px-4 py-3 rounded-xl flex items-center gap-4 transition-all duration-200 text-sm font-medium group ${
                activeMenu === item.id 
                  ? 'bg-orange-500/10 text-orange-400 shadow-[inset_0_0_0_1px_rgba(249,115,22,0.2)]' 
                  : 'text-slate-400 hover:text-slate-200 hover:bg-white/5'
              }`}
            >
              <span className={`transition-colors duration-200 ${activeMenu === item.id ? 'text-orange-400' : 'text-slate-500 group-hover:text-slate-300'}`}>
                {item.icon}
              </span>
              {item.label}
            </button>
          ))}
        </nav>
        
      </aside>

      {/* Content Area */}
      <main className="flex-grow p-12 md:p-20 overflow-y-auto bg-gradient-to-br from-[#020617] to-[#0f172a]">
        <div className="max-w-5xl mx-auto">
          <div className="animate-in fade-in slide-in-from-bottom-6 duration-700">
            {activeMenu === 'search' && (
              <>
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-white">Search Engine</h1>
                  <p className="text-lg text-slate-400 max-w-2xl">Configure the default provider for your address bar searches and how they behave.</p>
                </header>

                <section>
                  <div className="flex items-center justify-between mb-8">
                    <h2 className="text-xl font-semibold text-slate-100">Default Search Provider</h2>
                  </div>
                  
                  <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-6">
                    {engines.map(({ id, name, Icon }) => (
                      <button
                        key={id}
                        onClick={() => selectEngine(id)}
                        className={`relative group flex items-center gap-5 p-6 rounded-2xl border transition-all duration-300 text-left ${
                          selectedEngine === id 
                            ? 'bg-orange-500/10 border-orange-500/50 shadow-[0_0_20px_-5px_rgba(249,115,22,0.3)]' 
                            : 'bg-white/5 border-white/5 hover:border-white/20 hover:bg-white/[0.08] hover:-translate-y-1'
                        }`}
                      >
                        {selectedEngine === id && (
                          <div className="absolute top-4 right-4 w-5 h-5 bg-orange-500 text-white rounded-full flex items-center justify-center text-[10px] font-bold shadow-lg shadow-orange-500/40 z-10 animate-in zoom-in duration-300">
                            ✓
                          </div>
                        )}
                        <div className={`w-16 h-12 shrink-0 p-1 rounded-xl flex items-center justify-center transition-transform duration-300 group-hover:scale-110 ${selectedEngine === id ? 'bg-orange-500/20' : 'bg-white/5'}`}>
                          <EngineLogo id={id} name={name} />
                        </div>
                        <div className="flex-grow overflow-hidden">
                          <span className={`block font-bold text-base truncate transition-colors duration-200 ${selectedEngine === id ? 'text-orange-400' : 'text-slate-200 group-hover:text-white'}`}>
                            {name}
                          </span>
                          <span className="text-xs text-slate-500 mt-0.5 block truncate">Default Provider</span>
                        </div>
                      </button>
                    ))}
                  </div>
                </section>

                <footer className="mt-20 pt-8 border-t border-white/5">
                  <p className="text-[11px] text-slate-500 text-center tracking-wide uppercase opacity-60">
                    Note: The logos used here are for illustration purposes only.
                  </p>
                </footer>
              </>
            )}

            {activeMenu === 'shortcuts' && (
              <div className="max-w-4xl">
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-white">Keyboard Shortcuts</h1>
                  <p className="text-lg text-slate-400">Master the OTF Browser with these intuitive shortcuts.</p>
                </header>
                
                <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
                  {[
                    ['Navigation', [
                      ['Alt + ←', 'Go back'],
                      ['Alt + →', 'Go forward'],
                      ['F5 / Ctrl + R', 'Reload'],
                      ['Escape', 'Stop loading'],
                      ['Ctrl + L / F6', 'Focus address bar'],
                    ]],
                    ['Tabs', [
                      ['Ctrl + T', 'Open a new tab'],
                      ['Ctrl + W', 'Close current tab'],
                      ['Ctrl + Shift + T', 'Reopen closed tab'],
                      ['Ctrl + Tab', 'Next tab'],
                      ['Ctrl + Shift + Tab', 'Previous tab'],
                    ]],
                    ['Page', [
                      ['Ctrl + F', 'Find in page'],
                      ['Ctrl + P', 'Print'],
                      ['Ctrl + +', 'Zoom in'],
                      ['Ctrl + −', 'Zoom out'],
                      ['Ctrl + 0', 'Reset zoom'],
                    ]],
                    ['Browser', [
                      ['Space', 'Scroll down'],
                      ['Shift + Space', 'Scroll up'],
                    ]],
                  ].map(([section, items]) => (
                    <section key={section} className="mb-4">
                      <h2 className="text-sm font-bold text-orange-500 mb-4 uppercase tracking-[0.2em]">{section}</h2>
                      <div className="bg-white/5 border border-white/5 rounded-2xl overflow-hidden backdrop-blur-sm">
                        <table className="w-full text-sm">
                          <tbody className="divide-y divide-white/5">
                            {items.map(([key, desc]) => (
                              <tr key={key} className="group hover:bg-white/[0.02] transition-colors">
                                <td className="px-6 py-4 w-48">
                                  <kbd className="px-2.5 py-1 bg-white/10 border border-white/10 rounded-md text-[11px] font-mono text-slate-300 shadow-sm group-hover:border-orange-500/30 group-hover:text-orange-400 transition-all">
                                    {key}
                                  </kbd>
                                </td>
                                <td className="px-6 py-4 text-slate-400 group-hover:text-slate-200 transition-colors">{desc}</td>
                              </tr>
                            ))}
                          </tbody>
                        </table>
                      </div>
                    </section>
                  ))}
                </div>
              </div>
            )}

            {(activeMenu === 'appearance' || activeMenu === 'privacy') && (
              <div className="flex flex-col items-center justify-center py-24 text-center">
                 <div className="w-24 h-24 bg-orange-500/10 rounded-3xl flex items-center justify-center mb-8 border border-orange-500/20 shadow-2xl shadow-orange-500/5">
                    <svg className="text-orange-500" width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83"/></svg>
                 </div>
                 <h3 className="text-2xl font-bold text-white mb-2">Section Coming Soon</h3>
                 <p className="text-slate-400 max-w-sm">We're working hard to bring you more customization and privacy controls.</p>
              </div>
            )}

            {activeMenu === 'about' && (
              <div className="max-w-3xl animate-in fade-in slide-in-from-bottom-6 duration-700">
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-2 text-white">OTF Browser</h1>
                  <p className="text-sm font-medium text-white/80 mb-6 tracking-wide">
                    Part of the <span className="text-orange-500 font-bold">Open Tech Foundation</span> ecosystem
                  </p>
                  <p className="text-lg text-slate-400">A high-performance, privacy-focused browser built on top of the Chromium Embedded Framework.</p>
                </header>

                <div className="space-y-6">
                  <section className="bg-white/5 border border-white/5 rounded-3xl p-8 backdrop-blur-sm">
                    <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Version Information</h2>
                    <div className="grid grid-cols-1 gap-4">
                      <div className="py-4 border-b border-white/5">
                        <div className="text-slate-400 text-[10px] font-bold uppercase tracking-[0.1em] mb-1.5">Browser Version</div>
                        <div className="text-slate-200 font-mono text-sm">1.0.0 (Official Build)</div>
                      </div>
                      <div className="py-4 border-b border-white/5">
                        <div className="text-slate-400 text-[10px] font-bold uppercase tracking-[0.1em] mb-1.5">Chromium Version</div>
                        <div className="text-slate-200 font-mono text-sm">147.0.7727.118</div>
                      </div>
                      <div className="py-4">
                        <div className="text-slate-400 text-[10px] font-bold uppercase tracking-[0.1em] mb-1.5">CEF Version</div>
                        <div className="text-slate-200 font-mono text-sm break-all leading-relaxed">147.0.10+gd58e84d+chromium-147.0.7727.118</div>
                      </div>
                    </div>
                  </section>

                  <section className="bg-white/5 border border-white/5 rounded-3xl p-8 backdrop-blur-sm">
                    <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Legal & Open Source</h2>
                    <p className="text-slate-400 text-sm leading-relaxed mb-6">
                      OTF Browser is made possible by the Chromium open source project and other open source software.
                    </p>
                    <div className="flex flex-wrap gap-4">
                      <button 
                        onClick={() => window.cefQuery?.({ request: 'navigate-current:https://github.com/Open-Tech-Foundation/Web-Browser' })}
                        className="px-4 py-2 bg-orange-500/10 hover:bg-orange-500/20 border border-orange-500/20 text-orange-400 rounded-xl text-xs font-semibold transition-all flex items-center gap-2"
                      >
                        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M9 19c-5 1.5-5-2.5-7-3m14 6v-3.87a3.37 3.37 0 0 0-.94-2.61c3.14-.35 6.44-1.54 6.44-7A5.44 5.44 0 0 0 20 4.77 5.07 5.07 0 0 0 19.91 1S18.73.65 16 2.48a13.38 13.38 0 0 0-7 0C6.27.65 5.09 1 5.09 1A5.07 5.07 0 0 0 5 4.77a5.44 5.44 0 0 0-1.5 3.78c0 5.42 3.3 6.61 6.44 7A3.37 3.37 0 0 0 9 18.13V22"></path></svg>
                        GitHub Repository
                      </button>
                      <button className="px-4 py-2 bg-white/5 hover:bg-white/10 border border-white/5 rounded-xl text-xs font-semibold transition-all">Terms of Service</button>
                      <button className="px-4 py-2 bg-white/5 hover:bg-white/10 border border-white/5 rounded-xl text-xs font-semibold transition-all">Privacy Policy</button>
                    </div>
                  </section>
                </div>

                <div className="mt-12 text-center">
                  <p className="text-slate-600 text-xs">© 2026 Open Tech Foundation. All rights reserved.</p>
                </div>
              </div>
            )}
          </div>
        </div>
      </main>
    </div>
  );
};

export default Settings;
