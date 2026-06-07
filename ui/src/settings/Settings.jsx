import React, { useState, useEffect } from 'react';
import * as Icons from './Icons';
import { resolveUrl, looksLikeDirectUrl } from '../shared/search';

const humanizeSize = (bytes) => {
  if (!bytes || bytes === 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  let size = bytes;
  while (size >= 1024 && i < units.length - 1) { size /= 1024; i++; }
  return `${size.toFixed(i === 0 ? 0 : 2)} ${units[i]}`;
};

const GenericIcon = () => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="w-full h-full text-slate-400">
    <circle cx="12" cy="12" r="10" /><line x1="2" y1="12" x2="22" y2="12" /><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z" />
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

const Switch = ({ label, description, checked, onChange, disabled = false }) => (
  <div className="flex items-center justify-between p-6 bg-card/50 border border-main rounded-2xl transition-all duration-300 hover:bg-card hover:border-orange-500/30 group">
    <div className="flex-grow pr-4">
      <h3 className={`text-base font-semibold transition-colors duration-200 ${checked ? 'text-main' : 'text-muted group-hover:text-main'}`}>
        {label}
      </h3>
      {description && <p className="text-sm text-muted mt-1 leading-relaxed">{description}</p>}
    </div>
    <button
      onClick={() => !disabled && onChange(!checked)}
      className={`relative inline-flex h-6 w-11 shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-all duration-300 ease-in-out focus:outline-none focus:ring-2 focus:ring-orange-500/40 focus:ring-offset-2 ${checked ? 'bg-orange-500 shadow-[0_0_15px_-3px_rgba(249,115,22,0.5)]' : 'bg-slate-700'
        } ${disabled ? 'opacity-40 cursor-not-allowed' : ''}`}
    >
      <span
        className={`pointer-events-none inline-block h-5 w-5 transform rounded-full bg-white shadow-lg transition duration-300 ease-in-out ${checked ? 'translate-x-5' : 'translate-x-0'
          }`}
      />
    </button>
  </div>
);

const Checkbox = ({ label, checked, onChange, disabled = false }) => (
  <button
    onClick={() => !disabled && onChange(!checked)}
    className={`flex items-center gap-4 p-4 rounded-xl border border-main transition-all text-left w-full group ${disabled ? 'cursor-default bg-main/5' : 'hover:border-orange-500/30 hover:bg-main/5'}`}
  >
    <div className={`w-5 h-5 rounded border-2 flex items-center justify-center transition-all ${checked ? 'bg-orange-500 border-orange-500' : 'border-muted group-hover:border-main'} ${disabled ? 'opacity-60' : ''}`}>
      {checked && (
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="4" strokeLinecap="round" strokeLinejoin="round">
          <polyline points="20 6 9 17 4 12" />
        </svg>
      )}
    </div>
    <span className={`text-sm font-medium transition-colors ${checked ? 'text-main' : 'text-muted group-hover:text-main'}`}>
      {label}
    </span>
  </button>
);

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

const explicitSchemePattern = /^https?:\/\//i;
const browserPagePattern = /^browser:\/\/(?:newtab|settings|findbar|history|bookmarks|downloads|security|insecure-blocked)(?:[/?#].*)?$/i;
const localhostPattern = /^localhost(?::\d{1,5})?(?:[/?#]|$)/i;
const ipv4Pattern = /^(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?:[/?#]|$)/;

const normalizeStartupUrl = (input) => {
  const trimmed = input.trim();
  if (!trimmed) return '';
  if (browserPagePattern.test(trimmed)) return trimmed;
  if (trimmed.startsWith('browser://')) return '';
  if (trimmed.startsWith('//')) return `https:${trimmed}`;
  if (explicitSchemePattern.test(trimmed)) return trimmed;
  if (!trimmed.includes(' ') && localhostPattern.test(trimmed)) {
    return `http://${trimmed}`;
  }
  if (!trimmed.includes(' ') && ipv4Pattern.test(trimmed)) {
    return `http://${trimmed}`;
  }
  if (!trimmed.includes(' ') && looksLikeDirectUrl(trimmed)) {
    return `https://${trimmed}`;
  }
  return '';
};

const searchStateByTab = {};

const Settings = () => {
  const [selectedEngine, setSelectedEngine] = useState('');
  const [customEngines, setCustomEngines] = useState([]);
  const [showAddCustom, setShowAddCustom] = useState(false);
  const [newCustomName, setNewCustomName] = useState('');
  const [newCustomUrl, setNewCustomUrl] = useState('');
  const [customError, setCustomError] = useState('');
  const [activeMenu, setActiveMenu] = useState('search');
  const [tabId, setTabId] = useState(null);
  const [historyEnabled, setHistoryEnabled] = useState(false);
  const [downloadsEnabled, setDownloadsEnabled] = useState(false);
  const [startupBehavior, setStartupBehavior] = useState('newtab');
  const [startupUrls, setStartupUrls] = useState([]);
  const [newUrl, setNewUrl] = useState('');
  const [startupUrlError, setStartupUrlError] = useState('');
  const [httpsOnly, setHttpsOnly] = useState(false);

  const [storagePaths, setStoragePaths] = useState(null);
  const [storageError, setStorageError] = useState('');
  const [storageSuccess, setStorageSuccess] = useState('');
  const [rowErrors, setRowErrors] = useState({});

  // Clear Data section
  const [siteUsage, setSiteUsage] = useState([]);
  const [sortAsc, setSortAsc] = useState(false);
  const [showClearConfirm, setShowClearConfirm] = useState(false);
  const [clearBusy, setClearBusy] = useState(false);
  const [clearStatus, setClearStatus] = useState('');
  const [storageTotals, setStorageTotals] = useState(null);

  const [clearItems, setClearItems] = useState({
    history: true,
    downloads: true,
    cookies: true,
    cache: true,
    siteData: true,
  });
  const [clearTimeRange, setClearTimeRange] = useState('all');
  const toggleClearItem = (key) => setClearItems((p) => ({ ...p, [key]: !p[key] }));

  const [appearanceMode, setAppearanceMode] = useState('auto');
  const [versionInfo, setVersionInfo] = useState({ browser: '', chromium: '', cef: '' });

  const clearDataSize = React.useMemo(() => {
    let total = 0;
    if (clearItems.cookies || clearItems.cache || clearItems.siteData) {
      for (const site of siteUsage) {
        if (clearItems.cookies) total += Number(site.cookies ?? 0);
        if (clearItems.cache) total += Number(site.cacheStorage ?? 0);
        if (clearItems.siteData) {
          total += Number(site.indexedDB ?? 0);
          total += Number(site.localStorage ?? 0);
        }
      }
    }
    return total;
  }, [siteUsage, clearItems]);

  // Reset Section State
  const [resetItems, setResetItems] = useState({
    startup: true,
    searchEngine: true,
    cookies: true,
    cache: true,
    ssl: true,
    serviceWorkers: true,
    permissions: true,
    storage: true,
    bookmarks: false,
    history: false,
    downloads: false,
    passwords: false
  });
  const [showResetConfirm, setShowResetConfirm] = useState(false);
  const [resetBusy, setResetBusy] = useState(false);
  const [resetStatus, setResetStatus] = useState('');
  const [restartBusy, setRestartBusy] = useState(false);

  const cached = tabId != null ? searchStateByTab[tabId] : null;
  const [searchQuery, setSearchQuery] = useState(cached ? cached.query : '');
  const [searchEngine, setSearchEngine] = useState(cached ? cached.engine : '');

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
        request: 'get-version-info',
        onSuccess: (response) => {
          try { setVersionInfo(JSON.parse(response)); } catch (_) {}
        }
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
            setCustomEngines(settings.customSearchEngines || []);
            setHistoryEnabled(settings.historyEnabled || false);
            setDownloadsEnabled(settings.downloadsEnabled || false);
            setStartupBehavior(settings.startupBehavior || 'newtab');
            setStartupUrls(settings.startupUrls || []);
            setHttpsOnly(settings.httpsOnly || false);

            setAppearanceMode(settings.appearanceMode || 'auto');
          } catch (e) {
            console.error('Failed to parse settings:', e);
          }
        }
      });
      window.cefQuery({
        request: 'get-storage-paths',
        onSuccess: (response) => {
          try {
            setStoragePaths(JSON.parse(response));
          } catch (_) {}
        }
      });
      window.cefQuery({
        request: 'get-site-usage-list',
        onSuccess: (response) => {
          try {
            setSiteUsage(JSON.parse(response));
          } catch (_) {}
        }
      });
      window.cefQuery({
        request: 'get-storage-totals',
        onSuccess: (response) => {
          try {
            setStorageTotals(JSON.parse(response));
          } catch (_) {}
        }
      });
    }
  }, []);

  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    const tab = params.get('tab');
    if (tab) setActiveMenu(tab);
  }, []);

  const selectEngine = (id) => {
    setSelectedEngine(id);
    saveSettings({ searchEngine: id });
  };

  const generateCustomId = () => '_custom_' + Date.now() + '_' + Math.random().toString(36).slice(2, 8);

  const addCustomEngine = () => {
    const name = newCustomName.trim();
    let url = newCustomUrl.trim();
    if (!name) { setCustomError('Enter a name.'); return; }
    if (!url) { setCustomError('Enter a search URL.'); return; }
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      url = 'https://' + url;
    }
    const id = generateCustomId();
    const updated = [...customEngines, { id, name, url }];
    setCustomEngines(updated);
    setCustomError('');
    setNewCustomName('');
    setNewCustomUrl('');
    setShowAddCustom(false);
    saveSettings({ customSearchEngines: updated });
  };

  const deleteCustomEngine = (id) => {
    const updated = customEngines.filter(e => e.id !== id);
    setCustomEngines(updated);
    if (selectedEngine === id) {
      setSelectedEngine('');
      saveSettings({ searchEngine: null, customSearchEngines: updated });
    } else {
      saveSettings({ customSearchEngines: updated });
    }
  };

  const allEngines = [...engines, ...customEngines.map(e => ({ id: e.id, name: e.name, custom: true }))];

  const saveSettings = (updates) => {
    if (window.cefQuery) {
      window.cefQuery({
        request: `set-settings:${JSON.stringify({
          searchEngine: selectedEngine || null,
          historyEnabled,
          downloadsEnabled,
          startupBehavior,
          startupUrls,
          httpsOnly,

          appearanceMode,
          customSearchEngines: customEngines,
          ...updates
        })}`,
        onSuccess: () => console.log('Settings saved')
      });
    }
  };

  const addStartupUrl = () => {
    const normalizedUrl = normalizeStartupUrl(newUrl);
    if (!normalizedUrl) {
      setStartupUrlError('Enter a valid URL like https://example.com or browser://newtab.');
      return;
    }
    if (startupUrls.includes(normalizedUrl)) {
      setStartupUrlError('That page is already listed.');
      return;
    }

    const newUrls = [...startupUrls, normalizedUrl];
    setStartupUrls(newUrls);
    saveSettings({ startupUrls: newUrls });
    setNewUrl('');
    setStartupUrlError('');
  };

  const toggleResetItem = (key) => {
    setResetItems(prev => ({ ...prev, [key]: !prev[key] }));
  };

  const handleReset = () => {
    if (!window.cefQuery || resetBusy) {
      return;
    }

    setResetBusy(true);
    setResetStatus('');
    setRestartBusy(false);
    window.cefQuery({
      request: `reset-browser-data:${JSON.stringify(resetItems)}`,
      onSuccess: (response) => {
        try {
          try {
            localStorage.removeItem('otf_last_engine');
          } catch (storageError) { }
          setSelectedEngine('');
          setSearchEngine('');
          setStartupBehavior('newtab');
          setStartupUrls([]);
          setNewUrl('');
          setStartupUrlError('');
          JSON.parse(response);
          setResetStatus('Reset complete.');
        } catch (e) {
          setResetStatus('Reset complete.');
        }
        setResetBusy(false);
      },
      onFailure: (code, msg) => {
        setResetBusy(false);
        setResetStatus(`Reset failed: ${msg || code}`);
      }
    });
  };

  const handleRestart = () => {
    if (!window.cefQuery || restartBusy) {
      return;
    }

    setRestartBusy(true);
    window.cefQuery({
      request: 'restart-browser',
      onFailure: (code, msg) => {
        setRestartBusy(false);
        setResetStatus(`Restart failed: ${msg || code}`);
      }
    });
  };

  const openInternalPage = (url) => {
    if (window.cefQuery) {
      window.cefQuery({ request: `navigate-current:${url}` });
      return;
    }
    window.location.href = url;
  };

  const menuItems = [
    { id: 'search', label: 'Search Engine', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg> },
    { id: 'startup', label: 'On Startup', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M18.36 6.64a9 9 0 1 1-12.73 0"></path><line x1="12" y1="2" x2="12" y2="12"></line></svg> },
    { id: 'appearance', label: 'Appearance', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect><line x1="3" y1="9" x2="21" y2="9"></line><line x1="9" y1="21" x2="9" y2="9"></line></svg> },
    { id: 'privacy', label: 'Privacy', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect><path d="M7 11V7a5 5 0 0 1 10 0v4"></path></svg> },
    { id: 'security', label: 'Security', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path></svg> },
    { id: 'storage', label: 'Config', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M22 12A10 10 0 1 1 12 2a10 10 0 0 1 10 10z" /><path d="M2 12h20" /><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z" /></svg> },
    { id: 'cleardata', label: 'Clear Data', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><polyline points="3 6 5 6 21 6" /><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" /><line x1="10" y1="11" x2="10" y2="17" /><line x1="14" y1="11" x2="14" y2="17" /></svg> },
    { id: 'shortcuts', label: 'Keyboard Shortcuts', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M15 9h4a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2z" /><path d="M5 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z" /><path d="M15 3h4a2 2 0 0 1 2 2v4a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z" /></svg> },
    { id: 'reset', label: 'Reset Settings', icon: <Icons.Reset /> },
    { id: 'about', label: 'About', icon: <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="16" x2="12" y2="12"></line><line x1="12" y1="8" x2="12.01" y2="8"></line></svg> },
  ];

  return (
    <div className="flex h-screen bg-main text-main font-sans overflow-hidden">
      {/* Sidebar */}
      <aside className="w-72 bg-card/50 backdrop-blur-xl border-r border-main flex flex-col py-8 shrink-0">
        <div className="px-8 pb-10 flex items-center gap-3">
          <div className="w-8 h-8 bg-orange-500 rounded-lg flex items-center justify-center shadow-lg shadow-orange-500/20">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1-1-1.73l.43.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z" /><circle cx="12" cy="12" r="3" /></svg>
          </div>
          <span className="text-xl font-bold bg-gradient-to-r from-main to-muted bg-clip-text text-transparent">Settings</span>
        </div>

        <nav className="flex-grow px-4 space-y-1">
          {menuItems.map(item => (
            <button
              key={item.id}
              onClick={() => setActiveMenu(item.id)}
              className={`w-full text-left px-4 py-3 rounded-xl flex items-center gap-4 transition-all duration-300 text-sm font-medium group cursor-pointer ${activeMenu === item.id
                ? 'bg-orange-500/10 text-orange-500 shadow-[inset_0_0_0_1px_rgba(249,115,22,0.2)]'
                : 'text-muted hover:text-main hover:bg-card/50 hover:translate-x-1'
                }`}
            >
              <span className={`transition-colors duration-200 ${activeMenu === item.id ? 'text-orange-400' : 'text-muted group-hover:text-main'}`}>
                {item.icon}
              </span>
              {item.label}
            </button>
          ))}
        </nav>
      </aside>

      {/* Content Area */}
      <main className="flex-grow p-12 md:p-20 overflow-y-auto">
        <div className="max-w-5xl mx-auto">
          <div className="animate-in fade-in slide-in-from-bottom-6 duration-700">
            {activeMenu === 'search' && (
              <>
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-main">Search Engine</h1>
                  <p className="text-lg text-muted max-w-2xl">Configure the default provider for your address bar searches and how they behave.</p>
                </header>

                <section>
                  <div className="flex items-center justify-between mb-8">
                    <h2 className="text-xl font-semibold text-main">Default Search Provider</h2>
                  </div>

                  {!selectedEngine && (
                    <div className="mb-8 p-6 bg-orange-500/10 border border-orange-500/20 rounded-3xl flex items-start gap-4 animate-in fade-in slide-in-from-top-4 duration-500">
                      <div className="w-10 h-10 rounded-xl bg-orange-500 flex items-center justify-center shrink-0 shadow-lg shadow-orange-500/20">
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                          <path d="m21 21-4.3-4.3" /><circle cx="11" cy="11" r="8" /><path d="M11 8v4" /><path d="M11 16h.01" />
                        </svg>
                      </div>
                      <div>
                        <h3 className="text-sm font-bold text-main mb-1">No default search engine selected</h3>
                        <p className="text-xs text-muted leading-relaxed">
                          Choose a search engine from the list below to enable address bar searches.
                          If no engine is selected, you can only navigate to direct URLs.
                        </p>
                      </div>
                    </div>
                  )}

                  <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-6">
                    {allEngines.map(({ id, name, custom }) => (
                      <button
                        key={id}
                        onClick={() => selectEngine(id)}
                        className={`relative group flex items-center gap-5 p-6 rounded-2xl border transition-all duration-300 text-left ${selectedEngine === id
                          ? 'bg-orange-500/10 border-orange-500/50 shadow-[0_0_20px_-5px_rgba(249,115,22,0.3)]'
                          : 'bg-card border-main hover:border-orange-500/30 hover:-translate-y-1'
                          }`}
                      >
                        {selectedEngine === id && (
                          <div className="absolute top-4 right-4 w-5 h-5 bg-orange-500 text-white rounded-full flex items-center justify-center text-[10px] font-bold shadow-lg shadow-orange-500/40 z-10 animate-in zoom-in duration-300">
                            ✓
                          </div>
                        )}
                        <div className={`w-16 h-12 shrink-0 p-1 rounded-xl flex items-center justify-center transition-transform duration-300 group-hover:scale-110 ${selectedEngine === id ? 'bg-orange-500/20' : 'bg-card/50'}`}>
                          {custom ? <GenericIcon /> : <EngineLogo id={id} name={name} />}
                        </div>
                        <div className="flex-grow overflow-hidden">
                          <span className={`block font-bold text-base truncate transition-colors duration-200 ${selectedEngine === id ? 'text-orange-500' : 'text-main group-hover:text-main'}`}>
                            {name}
                          </span>
                          <span className="text-xs text-muted mt-0.5 block truncate">{custom ? 'Custom Engine' : 'Default Provider'}</span>
                        </div>
                        {custom && (
                          <button
                            onClick={(e) => { e.stopPropagation(); deleteCustomEngine(id); }}
                            className="shrink-0 ml-2 w-7 h-7 rounded-lg bg-red-500/10 border border-red-500/30 flex items-center justify-center opacity-0 group-hover:opacity-100 transition-opacity hover:bg-red-500/20"
                            title="Delete custom engine"
                          >
                            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" className="text-red-400">
                              <path d="M18 6 6 18M6 6l12 12" />
                            </svg>
                          </button>
                        )}
                      </button>
                    ))}
                  </div>

                  <section className="mt-12">
                    <div className="flex items-center justify-between mb-6">
                      <h2 className="text-xl font-semibold text-main">Custom Search Engines</h2>
                      <button
                        onClick={() => setShowAddCustom(!showAddCustom)}
                        className="flex items-center gap-2 px-4 py-2 bg-orange-500 hover:bg-orange-600 text-white rounded-xl text-sm font-bold transition-all shadow-lg shadow-orange-500/10"
                      >
                        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round">
                          <path d="M12 5v14M5 12h14" />
                        </svg>
                        Add Custom Engine
                      </button>
                    </div>

                    {showAddCustom && (
                      <div className="mb-6 p-6 bg-card border border-main rounded-3xl animate-in slide-in-from-top-4 duration-500">
                        <div className="space-y-4">
                          <div>
                            <label className="block text-xs font-bold text-muted mb-1.5 uppercase tracking-wider">Name</label>
                            <input
                              type="text"
                              placeholder="e.g., My Search"
                              value={newCustomName}
                              onChange={(e) => { setNewCustomName(e.target.value); setCustomError(''); }}
                              className="w-full bg-main/5 border border-main rounded-xl px-4 py-3 text-sm text-main placeholder:text-muted focus:outline-none focus:border-orange-500/50 focus:ring-1 focus:ring-orange-500/20 transition-all"
                            />
                          </div>
                          <div>
                            <label className="block text-xs font-bold text-muted mb-1.5 uppercase tracking-wider">
                              Search URL <span className="text-muted">(query appended to URL, or use %s to place it)</span>
                            </label>
                            <input
                              type="text"
                              placeholder="https://example.com/search?q=%s"
                              value={newCustomUrl}
                              onChange={(e) => { setNewCustomUrl(e.target.value); setCustomError(''); }}
                              className="w-full bg-main/5 border border-main rounded-xl px-4 py-3 text-sm text-main placeholder:text-muted focus:outline-none focus:border-orange-500/50 focus:ring-1 focus:ring-orange-500/20 transition-all"
                            />
                          </div>
                          {customError && (
                            <p className="text-xs text-red-400 font-medium">{customError}</p>
                          )}
                          <div className="flex gap-3 pt-2">
                            <button
                              onClick={addCustomEngine}
                              className="px-6 py-2.5 bg-orange-500 hover:bg-orange-600 text-white rounded-xl text-sm font-bold transition-all shadow-lg shadow-orange-500/10"
                            >
                              Add Engine
                            </button>
                            <button
                              onClick={() => { setShowAddCustom(false); setCustomError(''); setNewCustomName(''); setNewCustomUrl(''); }}
                              className="px-6 py-2.5 bg-main/5 hover:bg-main/10 text-muted hover:text-main rounded-xl text-sm font-bold transition-all"
                            >
                              Cancel
                            </button>
                          </div>
                        </div>
                      </div>
                    )}

                    {customEngines.length === 0 && !showAddCustom && (
                      <p className="text-sm text-muted">No custom search engines added yet.</p>
                    )}
                  </section>
                </section>
              </>
            )}

            {activeMenu === 'startup' && (
              <>
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-main">On Startup</h1>
                  <p className="text-lg text-muted max-w-2xl">Choose what you see when you launch the OTF Browser.</p>
                </header>

                <div className="grid grid-cols-1 gap-4 max-w-2xl">
                  {[
                    ['newtab', 'Open the New Tab page'],
                    ['continue', 'Continue where you left off'],
                    ['specific', 'Open a specific page or set of pages'],
                  ].map(([id, label]) => (
                    <button
                      key={id}
                      onClick={() => {
                        setStartupBehavior(id);
                        saveSettings({ startupBehavior: id });
                      }}
                      className={`w-full flex items-center justify-between p-6 bg-card/50 border rounded-2xl transition-all duration-300 hover:bg-card group ${startupBehavior === id
                        ? 'border-orange-500/50 bg-orange-500/5 shadow-[0_0_20px_-10px_rgba(249,115,22,0.3)]'
                        : 'border-main hover:border-orange-500/30'
                        }`}
                    >
                      <span className={`text-base font-semibold transition-colors duration-200 ${startupBehavior === id ? 'text-main' : 'text-muted group-hover:text-main'}`}>
                        {label}
                      </span>
                      <div className={`w-6 h-6 rounded-full border-2 flex items-center justify-center transition-all duration-300 ${startupBehavior === id ? 'border-orange-500 bg-orange-500' : 'border-slate-600'}`}>
                        {startupBehavior === id && <div className="w-2.5 h-2.5 rounded-full bg-white animate-in zoom-in duration-300" />}
                      </div>
                    </button>
                  ))}
                </div>

                {startupBehavior === 'specific' && (
                  <div className="mt-8 p-8 bg-card border border-main rounded-3xl animate-in slide-in-from-top-4 duration-500">
                    <h3 className="text-sm font-bold text-muted mb-6 uppercase tracking-wider">Pages to open</h3>

                    <div className="space-y-3 mb-6">
                      {startupUrls.map((url, index) => (
                        <div key={index} className="flex items-center gap-3 p-4 bg-main/5 rounded-xl border border-main group hover:border-orange-500/30 transition-all">
                          <div className="w-8 h-8 rounded-lg bg-card flex items-center justify-center shrink-0">
                            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"></path><path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"></path></svg>
                          </div>
                          <span className="flex-grow text-sm text-main truncate font-medium">{url}</span>
                          <button
                            onClick={() => {
                              const newUrls = startupUrls.filter((_, i) => i !== index);
                              setStartupUrls(newUrls);
                              saveSettings({ startupUrls: newUrls });
                            }}
                            className="p-2 text-muted hover:text-red-400 transition-colors"
                          >
                            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M3 6h18"></path><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path></svg>
                          </button>
                        </div>
                      ))}
                    </div>

                    <div className="flex gap-3">
                      <input
                        type="text"
                        placeholder="Enter URL (e.g., https://google.com)"
                        value={newUrl}
                        onChange={(e) => {
                          setNewUrl(e.target.value);
                          if (startupUrlError) setStartupUrlError('');
                        }}
                        className="flex-grow bg-card border border-main rounded-xl px-4 py-3 text-sm text-main placeholder:text-muted focus:outline-none focus:border-orange-500/50 focus:ring-1 focus:ring-orange-500/20 transition-all"
                      />
                      <button
                        onClick={addStartupUrl}
                        disabled={!normalizeStartupUrl(newUrl) || startupUrls.includes(normalizeStartupUrl(newUrl))}
                        className="px-6 py-3 bg-orange-500 hover:bg-orange-600 disabled:opacity-50 text-white rounded-xl text-sm font-bold transition-all shadow-lg shadow-orange-500/10"
                      >
                        Add
                      </button>
                    </div>
                  </div>
                )}
              </>
            )}

            {activeMenu === 'appearance' && (
              <>
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-main">Appearance</h1>
                  <p className="text-lg text-muted max-w-2xl">Customize how OTF Browser looks and feels on your device.</p>
                </header>

                <section>
                  <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Theme Mode</h2>
                  <div className="grid grid-cols-1 sm:grid-cols-3 gap-6">
                    {[
                      { id: 'auto', label: 'System', icon: <Icons.Monitor />, desc: 'Follow OS theme' },
                      { id: 'light', label: 'Light', icon: <Icons.Sun />, desc: 'Clean & bright' },
                      { id: 'dark', label: 'Dark', icon: <Icons.Moon />, desc: 'Easy on eyes' }
                    ].map(mode => (
                      <button
                        key={mode.id}
                        onClick={() => {
                          setAppearanceMode(mode.id);
                          saveSettings({ appearanceMode: mode.id });
                        }}
                        className={`relative flex flex-col items-center gap-4 p-8 rounded-3xl border transition-all duration-300 ${appearanceMode === mode.id
                          ? 'bg-orange-500/10 border-orange-500/50 shadow-lg shadow-orange-500/5'
                          : 'bg-card/50 border-main hover:border-orange-500/30 hover:bg-card'
                          }`}
                      >
                        <div className={`w-12 h-12 rounded-2xl flex items-center justify-center mb-2 transition-transform duration-300 ${appearanceMode === mode.id ? 'bg-orange-500 text-white scale-110 shadow-lg shadow-orange-500/20' : 'bg-card/50 text-muted'
                          }`}>
                          {mode.icon}
                        </div>
                        <div className="text-center">
                          <span className={`block font-bold text-lg ${appearanceMode === mode.id ? 'text-orange-500' : 'text-main'}`}>{mode.label}</span>
                          <span className="text-xs text-muted mt-1 block">{mode.desc}</span>
                        </div>
                        {appearanceMode === mode.id && (
                          <div className="absolute top-4 right-4 w-5 h-5 bg-orange-500 text-white rounded-full flex items-center justify-center text-[10px] font-bold animate-in zoom-in duration-300">
                            ✓
                          </div>
                        )}
                      </button>
                    ))}
                  </div>
                </section>
              </>
            )}

            {activeMenu === 'cleardata' && (
              <>
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-main">Clear Browsing Data</h1>
                </header>

                <section>
                  <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Clear Browsing Data</h2>
                  <div className="flex items-center justify-center gap-2 mb-8">
                    <span className="text-xs text-muted font-medium">Time range</span>
                    <div className="flex bg-main/5 border border-main rounded-lg overflow-hidden">
                      {[
                        { key: 'hour', label: '1 hour' },
                        { key: 'day', label: '24 hours' },
                        { key: 'week', label: '7 days' },
                        { key: 'month', label: '30 days' },
                        { key: 'all', label: 'All time' },
                      ].map(({ key, label }) => (
                        <button
                          key={key}
                          onClick={() => setClearTimeRange(key)}
                          className={`px-3 py-1.5 text-xs font-semibold transition-all cursor-pointer ${
                            clearTimeRange === key
                              ? 'bg-orange-500 text-white'
                              : 'text-muted hover:text-main hover:bg-main/5'
                          }`}
                        >
                          {label}
                        </button>
                      ))}
                    </div>
                  </div>
                  <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                    <Checkbox label="Browsing history" checked={clearItems.history} onChange={() => toggleClearItem('history')} />
                    <Checkbox label="Download history" checked={clearItems.downloads} onChange={() => toggleClearItem('downloads')} />
                    <Checkbox label="Cookies and other site data" checked={clearItems.cookies} onChange={() => toggleClearItem('cookies')} />
                    <Checkbox label="Cached images and files" checked={clearItems.cache} onChange={() => toggleClearItem('cache')} />
                    <Checkbox label="Site storage (IndexedDB, Local Storage, etc.)" checked={clearItems.siteData} onChange={() => toggleClearItem('siteData')} />
                  </div>
                  <div className="flex justify-end mt-6">
                    <button
                      onClick={() => { setClearStatus(''); setShowClearConfirm(true); }}
                      disabled={!Object.values(clearItems).some(Boolean)}
                      className="px-4 py-2 text-sm font-semibold rounded-lg border border-red-500/50 text-red-500 hover:bg-red-500/10 transition-all disabled:opacity-40 disabled:cursor-not-allowed cursor-pointer"
                    >
                      {clearDataSize > 0 ? `Clear Data \u00B7 ${humanizeSize(clearDataSize)}` : 'Clear Data'}
                    </button>
                  </div>
                </section>

                <section className="mt-10">
                  <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Storage by Type</h2>
                  {!storageTotals ? (
                    <p className="text-sm text-muted">Loading storage totals...</p>
                  ) : (
                    <div className="grid grid-cols-2 sm:grid-cols-3 gap-3">
                      {[
                        { key: 'httpCache', label: 'HTTP Cache' },
                        { key: 'indexedDB', label: 'IndexedDB' },
                        { key: 'cacheStorage', label: 'Cache Storage' },
                        { key: 'localStorage', label: 'Local Storage' },
                        { key: 'sessionStorage', label: 'Session Storage' },
                        { key: 'fileSystem', label: 'File System' },
                        { key: 'blobStorage', label: 'Blob Storage' },
                        { key: 'codeCache', label: 'Code Cache' },
                        { key: 'cookies', label: 'Cookies' },
                      ].map(({ key, label }) => {
                        const bytes = Number(storageTotals[key] ?? 0);
                        return (
                          <div key={key} className="p-4 bg-card/50 border border-main rounded-2xl">
                            <div className="text-[10px] font-bold uppercase tracking-wider mb-1 text-orange-500">{label}</div>
                            <div className="text-sm font-mono font-semibold text-main">{humanizeSize(bytes)}</div>
                          </div>
                        );
                      })}
                    </div>
                  )}
                </section>

                <section className="mt-10">
                  <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">
                    Sites with Storage ({siteUsage.filter((s) => (Number(s.storageBytes) || 0) > 0).length})
                  </h2>
                  {siteUsage.length === 0 ? (
                    <p className="text-sm text-muted">No site storage data found.</p>
                  ) : (
                    <>
                  <div className="flex items-center justify-center gap-2 mb-4">
                        <span className="text-xs text-muted font-medium">Sort by size</span>
                        <button
                          onClick={() => setSortAsc(!sortAsc)}
                          className="px-3 py-1.5 bg-main/5 border border-main rounded-lg text-xs text-muted hover:text-main hover:border-orange-500/30 transition-all flex items-center gap-1.5 cursor-pointer"
                        >
                          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" className={sortAsc ? '' : 'rotate-180'}>
                            <path d="M12 5v14M5 12l7-7 7 7" />
                          </svg>
                          {sortAsc ? 'Smallest' : 'Largest'}
                        </button>
                      </div>
                      <div className="space-y-2">
                        {[...siteUsage]
                          .filter((site) => (Number(site.storageBytes) || 0) > 0)
                          .sort((a, b) => sortAsc
                            ? (Number(a.storageBytes) - Number(b.storageBytes))
                            : (Number(b.storageBytes) - Number(a.storageBytes)))
                          .map((site) => {
                            const idb = Number(site.indexedDB) || 0;
                            const cs = Number(site.cacheStorage) || 0;
                            const ls = Number(site.localStorage) || 0;
                            const cookies = Number(site.cookies) || 0;
                            const items = [];
                            if (idb > 0) items.push({ label: 'IndexedDB', bytes: idb });
                            if (cs > 0) items.push({ label: 'Cache Storage', bytes: cs });
                            if (ls > 0) items.push({ label: 'Local Storage', bytes: ls });
                            if (cookies > 0) items.push({ label: 'Cookies', bytes: cookies });
                            const hasItems = items.length > 0;
                            const Row = hasItems ? 'details' : 'div';
                            const rowProps = hasItems ? { className: 'group bg-card/50 border border-main rounded-2xl overflow-hidden transition-all hover:border-orange-500/30 open:shadow-md' } : { className: 'flex items-center justify-between p-4 bg-card/50 border border-main rounded-2xl transition-all hover:border-orange-500/30' };
                            return (
                            <Row key={site.origin} {...rowProps}>
                              {hasItems ? (
                                <summary className="flex items-center justify-between p-4 cursor-pointer list-none [&::-webkit-details-marker]:hidden transition-colors hover:bg-black/5 open:bg-black/5">
                                  <div className="flex items-center gap-3 min-w-0">
                                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" className="shrink-0 text-muted transition-transform group-open:rotate-90">
                                      <path d="M9 18l6-6-6-6" />
                                    </svg>
                                    <span className="text-sm font-medium text-main truncate">{site.origin}</span>
                                  </div>
                                  <span className="text-xs font-mono text-muted shrink-0 ml-3">{humanizeSize(Number(site.storageBytes))}</span>
                                </summary>
                              ) : (
                                <div className="flex items-center justify-between p-4 w-full">
                                  <div className="flex items-center gap-3 min-w-0">
                                    <span className="text-sm font-medium text-main truncate">{site.origin}</span>
                                  </div>
                                  <span className="text-xs font-mono text-muted shrink-0 ml-3">{humanizeSize(Number(site.storageBytes))}</span>
                                </div>
                              )}
                              {hasItems && (
                                <div className="px-4 pb-4 space-y-1">
                                  {items.map((item) => (
                                    <div key={item.label} className="flex items-center gap-2.5 text-xs pl-8 pr-2 py-1 rounded-lg hover:bg-black/5">
                                      <svg width="8" height="8" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" className="shrink-0 text-orange-400">
                                        <path d="M4 12h16" /><path d="M12 4v16" />
                                      </svg>
                                      <span className="text-orange-500 font-medium">{item.label}</span>
                                      <span className="font-mono text-muted ml-auto">{humanizeSize(item.bytes)}</span>
                                    </div>
                                  ))}
                                </div>
                              )}
                            </Row>
                            );
                          })}
                      </div>
                    </>
                  )}
                </section>
              </>
            )}

            {activeMenu === 'storage' && (
              <>
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-main">Config</h1>
                </header>

                <section>
                  <div className="space-y-4">
                    {[
                      { key: 'dataDir', label: 'Browser Data', desc: 'Settings, databases, and profile data.', readonly: true, footnote: 'This location is browser-managed and cannot be changed.' },
                      { key: 'cacheDir', label: 'Cache', desc: 'Temporary internet files and cached content.' },
                      { key: 'downloadsDir', label: 'Downloads', desc: 'Where downloaded files are saved.' },
                    ].map(({ key, label, desc, readonly, footnote }) => {
                      const active = storagePaths ? storagePaths[`active${key.charAt(0).toUpperCase() + key.slice(1)}`] : '';
                      const configured = storagePaths ? storagePaths[`configured${key.charAt(0).toUpperCase() + key.slice(1)}`] : '';
                      const pending = storagePaths ? storagePaths[`pending${key.charAt(0).toUpperCase() + key.slice(1)}`] : null;
                      const displayPath = pending || configured || active;
                      const hasPending = pending != null && pending !== '';
                      const localError = rowErrors[key] || '';
                      const sizeKey = key === 'dataDir' ? '' : (key === 'cacheDir' ? 'cacheSize' : 'downloadsSize');
                      const dirSize = sizeKey && storagePaths ? storagePaths[sizeKey] : null;
                      return (
                        <div key={key} className={`p-6 bg-card/50 border rounded-2xl transition-all ${hasPending ? 'border-orange-500/50' : localError ? 'border-red-500/50' : 'border-main'}`}>
                          <div className="flex items-start justify-between gap-4">
                            <div className="flex-grow min-w-0">
                              <h3 className="text-base font-semibold text-main flex items-center gap-2">
                                {label}
                                {dirSize != null && (
                                  <span className="inline-flex items-center px-2 py-0.5 text-[10px] font-bold font-mono uppercase tracking-wider bg-orange-500/10 text-orange-400 rounded-full border border-orange-500/20">
                                    {humanizeSize(Number(dirSize))}
                                  </span>
                                )}
                              </h3>
                              <p className="text-xs text-muted mt-1">{desc}</p>
                              <div className="mt-3 flex items-center gap-2">
                                <code className="text-xs font-mono bg-main/5 px-2 py-1 rounded border border-main truncate block max-w-full text-muted">
                                  {displayPath}
                                </code>
                                {hasPending && (
                                  <span className="shrink-0 px-2 py-0.5 text-[10px] font-bold uppercase tracking-wider bg-orange-500/10 text-orange-500 rounded-full border border-orange-500/20">
                                    Pending restart
                                  </span>
                                )}
                              </div>
                            </div>
                            {!readonly && (
                              <button
                                onClick={() => {
                                  setRowErrors(prev => ({ ...prev, [key]: '' }));
                                  setStorageSuccess('');
                                  window.cefQuery({
                                    request: 'select-folder',
                                    onSuccess: (selected) => {
                                      if (selected && selected !== 'cancelled') {
                                        window.cefQuery({
                                          request: `set-storage-path:${JSON.stringify({ path: selected, purpose: key === 'cacheDir' ? 'cache' : 'downloads' })}`,
                                          onSuccess: () => {
                                            setRowErrors(prev => ({ ...prev, [key]: '' }));
                                            setStorageSuccess(`${label} location changed successfully. Changes will apply after restart.`);
                                            window.cefQuery({
                                              request: 'get-storage-paths',
                                              onSuccess: (r) => {
                                                try { setStoragePaths(JSON.parse(r)); } catch (_) {}
                                              }
                                            });
                                          },
                                          onFailure: (code, msg) => {
                                            const errMsg = msg || 'Failed to change path.';
                                            setRowErrors(prev => ({ ...prev, [key]: errMsg }));
                                            setTimeout(() => setRowErrors(prev => ({ ...prev, [key]: '' })), 6000);
                                          }
                                        });
                                      }
                                    },
                                    onFailure: () => {}
                                  });
                                }}
                                className="shrink-0 px-4 py-2 bg-orange-500 hover:bg-orange-600 text-white rounded-xl text-sm font-bold transition-all shadow-lg shadow-orange-500/10 cursor-pointer"
                              >
                                Change
                              </button>
                            )}
                          </div>
                          {localError && (
                            <div className="mt-3 p-2 bg-red-500/10 border border-red-500/20 rounded-xl animate-in fade-in duration-300">
                              <p className="text-xs text-red-400 font-medium">{localError}</p>
                            </div>
                          )}
                          {footnote && (
                            <div className="mt-3 p-3 bg-sky-500/10 border border-sky-500/20 rounded-xl flex items-start gap-2.5">
                              <svg className="w-4 h-4 mt-0.5 shrink-0 text-sky-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                <circle cx="12" cy="12" r="10" /><line x1="12" y1="16" x2="12" y2="12" /><line x1="12" y1="8" x2="12.01" y2="8" />
                              </svg>
                              <p className="text-[11px] text-main leading-relaxed">{footnote}</p>
                            </div>
                          )}
                        </div>
                      );
                    })}
                  </div>

                  {storageSuccess && (
                    <div className="mt-4 p-5 bg-emerald-500/15 border border-emerald-500/40 rounded-2xl shadow-[0_0_20px_-8px_rgba(16,185,129,0.3)] animate-in fade-in duration-300">
                      <div className="flex items-center gap-3">
                        <div className="w-7 h-7 rounded-full bg-emerald-500 flex items-center justify-center shrink-0 shadow-lg shadow-emerald-500/30">
                          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                            <polyline points="20 6 9 17 4 12" />
                          </svg>
                        </div>
                        <p className="text-sm text-black font-semibold">{storageSuccess}</p>
                      </div>
                    </div>
                  )}

                  {storagePaths && (storagePaths.pendingCacheDir || storagePaths.pendingDownloadsDir) && (
                    <div className="mt-8 p-6 bg-orange-500/10 border border-orange-500/20 rounded-3xl flex items-start gap-4">
                      <div className="w-10 h-10 rounded-xl bg-orange-500 flex items-center justify-center shrink-0 shadow-lg shadow-orange-500/20">
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                          <path d="M18.36 6.64a9 9 0 1 1-12.73 0" /><line x1="12" y1="2" x2="12" y2="12" />
                        </svg>
                      </div>
                      <div>
                        <h3 className="text-sm font-bold text-main mb-1">Restart required</h3>
                        <p className="text-xs text-muted leading-relaxed">
                          Some storage location changes will take effect after you restart the browser.
                        </p>
                        <button
                          onClick={() => {
                            if (window.cefQuery) {
                              window.cefQuery({ request: 'restart-browser' });
                            }
                          }}
                          className="mt-3 px-4 py-2 bg-orange-500 hover:bg-orange-600 text-white rounded-xl text-sm font-bold transition-all shadow-lg shadow-orange-500/10 cursor-pointer"
                        >
                          Restart Now
                        </button>
                      </div>
                    </div>
                  )}
                </section>
              </>
            )}

            {activeMenu === 'shortcuts' && (
              <div className="max-w-4xl">
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-main">Keyboard Shortcuts</h1>
                  <p className="text-lg text-muted">Master the OTF Browser with these intuitive shortcuts.</p>
                </header>

                <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
                  {[
                    ['Navigation', [
                      ['Alt + ←', 'Go back'],
                      ['Alt + →', 'Go forward'],
                      ['F5 / Ctrl + R', 'Reload'],
                      ['Ctrl + Shift + R', 'Hard reload / ignore cache'],
                      ['Escape', 'Exit fullscreen / stop loading'],
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
                      ['Ctrl + G', 'Find next'],
                      ['Ctrl + Shift + G', 'Find previous'],
                      ['Ctrl + D', 'Bookmark current page'],
                      ['Ctrl + Home', 'Scroll to top'],
                      ['Ctrl + End', 'Scroll to bottom'],
                      ['Ctrl + P', 'Print'],
                      ['Ctrl + +', 'Zoom in'],
                      ['Ctrl + −', 'Zoom out'],
                      ['Ctrl + 0', 'Reset zoom'],
                    ]],
                    ['Browser', [
                      ['F11', 'Toggle fullscreen'],
                      ['Ctrl + H', 'Open history page'],
                      ['Ctrl + J', 'Open downloads page'],
                      ['Ctrl + Shift + J', 'Toggle page console'],
                      ['F12', 'Toggle page console'],
                      ['Space', 'Scroll down'],
                      ['Shift + Space', 'Scroll up'],
                    ]],
                  ].map(([section, items]) => (
                    <section key={section} className="mb-4">
                      <h2 className="text-sm font-bold text-orange-500 mb-4 uppercase tracking-[0.2em]">{section}</h2>
                      <div className="bg-card/50 border border-main rounded-2xl overflow-hidden backdrop-blur-sm">
                        <table className="w-full text-sm">
                          <tbody className="divide-y divide-main">
                            {items.map(([key, desc]) => (
                              <tr key={key} className="group hover:bg-main/5 transition-colors">
                                <td className="px-6 py-4 w-48">
                                  <kbd className="px-2.5 py-1 bg-main/10 border border-main rounded-md text-[11px] font-mono text-muted shadow-sm group-hover:border-orange-500/30 group-hover:text-orange-500 transition-all">
                                    {key}
                                  </kbd>
                                </td>
                                <td className="px-6 py-4 text-muted group-hover:text-main transition-colors">{desc}</td>
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

            {activeMenu === 'privacy' && (
              <>
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-main">Privacy</h1>
                  <p className="text-lg text-muted max-w-2xl">Manage your browsing data and how websites see you.</p>
                </header>

                <div className="space-y-10">
                  <section>
                    <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Browsing Activity</h2>
                    <div className="grid grid-cols-1 gap-4">
                      <Switch
                        label="Web browsing history"
                        description="Save the websites you visit to your history."
                        checked={historyEnabled}
                        onChange={(val) => {
                          setHistoryEnabled(val);
                          saveSettings({ historyEnabled: val });
                        }}
                      />
                      <Switch
                        label="Download History"
                        description="Keep a record of all files you have downloaded."
                        checked={downloadsEnabled}
                        onChange={(val) => {
                          setDownloadsEnabled(val);
                          saveSettings({ downloadsEnabled: val });
                        }}
                      />
                    </div>
                  </section>

                </div>
              </>
            )}

            {activeMenu === 'security' && (
              <>
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-main">Security</h1>
                  <p className="text-lg text-muted max-w-2xl">Control your security level and protection mechanisms.</p>
                </header>

                <div className="space-y-10">
                  <section>
                    <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Safe Navigation</h2>
                    <div className="grid grid-cols-1 gap-4">
                      {[
                        { title: 'HTTPS-Only Mode', desc: "Upgrade all navigations to HTTPS and warn you before loading sites that don't support it." },

                        { title: 'Enhanced Safe Browsing', desc: "Proactively protects you against dangerous websites, downloads, and extensions." }
                      ].map((item, idx) => (
                        <div key={idx} className="flex items-center justify-between p-8 bg-card/50 border border-main rounded-3xl relative overflow-hidden group">
                          <div className="absolute inset-0 bg-gradient-to-br from-orange-500/5 to-transparent opacity-0 group-hover:opacity-100 transition-opacity duration-500" />
                          <div className="flex-1 pr-8 relative z-10">
                            <h3 className="text-lg font-bold text-main mb-2">{item.title}</h3>
                            <p className="text-muted text-sm leading-relaxed">{item.desc}</p>
                          </div>
                          <div className="shrink-0 w-12 h-6 rounded-full bg-green-500/10 flex items-center justify-center border border-green-500/40 shadow-[0_0_15px_-3px_rgba(16,185,129,0.3)] relative z-10">
                            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#10b981" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                              <polyline points="20 6 9 17 4 12" />
                            </svg>
                          </div>
                        </div>
                      ))}
                    </div>
                  </section>
                </div>
              </>
            )}

            {activeMenu === 'reset' && (
              <>
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-4 text-main">Reset Settings</h1>
                  <p className="text-lg text-muted max-w-2xl">Restore OTF Browser to its original defaults and clear browsing data.</p>
                </header>

                <div className="space-y-12">
                  <section>
                    <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Default Reset Items</h2>
                    <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                      <Checkbox label="Startup behavior & pages" checked={resetItems.startup} onChange={() => toggleResetItem('startup')} disabled />
                      <Checkbox label="Default search engine" checked={resetItems.searchEngine} onChange={() => toggleResetItem('searchEngine')} disabled />
                      <Checkbox label="Cookies and other site data" checked={resetItems.cookies} onChange={() => toggleResetItem('cookies')} disabled />
                      <Checkbox label="Cached images and files" checked={resetItems.cache} onChange={() => toggleResetItem('cache')} disabled />
                      <Checkbox label="SSL Certificate Exceptions" checked={resetItems.ssl} onChange={() => toggleResetItem('ssl')} disabled />
                      <Checkbox label="Service Workers" checked={resetItems.serviceWorkers} onChange={() => toggleResetItem('serviceWorkers')} disabled />
                      <Checkbox label="Site permissions (camera, mic, notifications)" checked={resetItems.permissions} onChange={() => toggleResetItem('permissions')} disabled />
                      <Checkbox label="Web Storage (Local storage, IndexedDB)" checked={resetItems.storage} onChange={() => toggleResetItem('storage')} disabled />
                    </div>
                  </section>

                  <section>
                    <h2 className="text-sm font-bold text-muted mb-6 uppercase tracking-[0.2em]">Optional Items</h2>
                    <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                      <Checkbox label="Browsing history" checked={resetItems.history} onChange={() => toggleResetItem('history')} />
                      <Checkbox label="Bookmarks" checked={resetItems.bookmarks} onChange={() => toggleResetItem('bookmarks')} />
                      <Checkbox label="Download history" checked={resetItems.downloads} onChange={() => toggleResetItem('downloads')} />
                      <Checkbox label="Saved passwords" checked={resetItems.passwords} onChange={() => toggleResetItem('passwords')} />
                    </div>
                  </section>

                  <div className="pt-6 border-t border-main">
                    <button
                      onClick={() => {
                        setResetStatus('');
                        setRestartBusy(false);
                        setShowResetConfirm(true);
                      }}
                      disabled={resetBusy}
                      className="px-10 py-4 bg-orange-500 hover:bg-orange-600 text-white rounded-2xl text-base font-black transition-all shadow-xl shadow-orange-500/20 active:scale-95 disabled:opacity-60 disabled:cursor-not-allowed disabled:hover:bg-orange-500"
                    >
                      Reset Settings
                    </button>
                  </div>
                </div>
              </>
            )}

            {activeMenu === 'about' && (
              <div className="max-w-3xl animate-in fade-in slide-in-from-bottom-6 duration-700">
                <header className="mb-12">
                  <h1 className="text-4xl font-extrabold tracking-tight mb-2 text-main">OTF Browser</h1>
                  <p className="text-sm font-medium text-main/80 mb-6 tracking-wide">
                    Part of the <span className="text-orange-500 font-bold">Open Tech Foundation</span> ecosystem
                  </p>
                  <p className="text-lg text-muted">A fast, privacy-focused browser with hardened security, built on top of the Chromium Embedded Framework.</p>
                </header>

                <div className="space-y-6">
                  <section className="bg-card/50 border border-main rounded-3xl p-8 backdrop-blur-sm">
                    <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Resources</h2>
                    <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
                      <a
                        href="https://browser.opentechf.org/"
                        target="_blank"
                        rel="noopener noreferrer"
                        className="flex items-center gap-4 p-4 bg-main/5 rounded-2xl border border-main hover:border-orange-500/30 hover:bg-main/10 transition-all group"
                      >
                        <div className="w-10 h-10 rounded-xl bg-card flex items-center justify-center text-muted group-hover:text-orange-500 transition-colors">
                          <Icons.Globe />
                        </div>
                        <div>
                          <div className="text-sm font-bold text-main">Official Website</div>
                          <div className="text-[10px] text-muted">browser.opentechf.org</div>
                        </div>
                      </a>
                      <a
                        href="https://github.com/Open-Tech-Foundation/Web-Browser"
                        target="_blank"
                        rel="noopener noreferrer"
                        className="flex items-center gap-4 p-4 bg-main/5 rounded-2xl border border-main hover:border-orange-500/30 hover:bg-main/10 transition-all group"
                      >
                        <div className="w-10 h-10 rounded-xl bg-card flex items-center justify-center text-muted group-hover:text-orange-500 transition-colors">
                          <Icons.GitHub />
                        </div>
                        <div>
                          <div className="text-sm font-bold text-main">Source Code</div>
                          <div className="text-[10px] text-muted">github.com/Open-Tech-Foundation/Web-Browser</div>
                        </div>
                      </a>
                    </div>
                  </section>

                  <section className="bg-card/50 border border-main rounded-3xl p-8 backdrop-blur-sm">
                    <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Version Information</h2>
                    <div className="grid grid-cols-1 gap-4">
                      <div className="py-4 border-b border-main">
                        <div className="text-muted text-[10px] font-bold uppercase tracking-[0.1em] mb-1.5">Browser Version</div>
                        <div className="text-main font-mono text-sm">{versionInfo.browser || '—'}</div>
                      </div>
                      <div className="py-4 border-b border-main">
                        <div className="text-muted text-[10px] font-bold uppercase tracking-[0.1em] mb-1.5">Chromium Version</div>
                        <div className="text-main font-mono text-sm">{versionInfo.chromium || '—'}</div>
                      </div>
                      <div className="py-4">
                        <div className="text-muted text-[10px] font-bold uppercase tracking-[0.1em] mb-1.5">CEF Version</div>
                        <div className="text-main font-mono text-sm break-all leading-relaxed">{versionInfo.cef || '—'}</div>
                      </div>
                    </div>
                  </section>

                  <section>
                    <h2 className="text-sm font-bold text-orange-500 mb-6 uppercase tracking-[0.2em]">Browser Protection</h2>
                    <div className="p-8 bg-card/50 border border-main rounded-3xl relative overflow-hidden group">
                      <div className="absolute inset-0 bg-gradient-to-br from-orange-500/10 via-transparent to-transparent opacity-80" />
                      <div className="relative z-10 flex flex-col md:flex-row md:items-center gap-6">
                        <div className="flex-grow">
                          <h3 className="text-lg font-bold text-main mb-2">Browser Protection Diagnostics</h3>
                          <p className="text-muted text-sm leading-relaxed max-w-2xl">
                            Launch the official protection portal to verify active browser privacy defenses, security layers, and anti-fingerprinting safeguards.
                          </p>
                          <p className="text-xs text-muted mt-3 font-mono">https://browser.opentechf.org/protection</p>
                        </div>
                        <button
                          onClick={() => openInternalPage('https://browser.opentechf.org/protection')}
                          className="px-6 py-3 bg-orange-500 hover:bg-orange-600 text-white rounded-xl text-sm font-black transition-all shadow-lg shadow-orange-500/20 active:scale-95 cursor-pointer shrink-0"
                        >
                          Open test page
                        </button>
                      </div>
                    </div>
                  </section>
                </div>
              </div>
            )}
          </div>
        </div>
      </main>

      {/* Confirm Dialog Overlay */}
      {showResetConfirm && (
        <div className="fixed inset-0 z-[100] flex items-center justify-center p-6 animate-in fade-in duration-300">
          <div className="absolute inset-0 bg-slate-950/60 backdrop-blur-sm" onClick={() => setShowResetConfirm(false)} />
          <div
            className="relative w-full max-w-md border border-main rounded-[2.5rem] p-10 shadow-2xl animate-in zoom-in-95 duration-300"
            style={{ backgroundColor: 'var(--bg-card)' }}
          >
            <div className="relative z-10">
              <div className="w-16 h-16 rounded-3xl bg-orange-500/15 border border-orange-500/30 flex items-center justify-center mb-8 mx-auto">
                <Icons.Reset />
              </div>
              <h2 className="text-2xl font-black text-center text-main mb-4 tracking-tight">
                {resetStatus ? 'Reset Complete' : 'Reset Browser Settings?'}
              </h2>
              {!resetStatus ? (
                <>
                  <p className="text-muted text-center text-sm leading-relaxed mb-10">
                    This will restore your settings to their defaults and clear selected data. This action cannot be undone.
                  </p>
                  <div className="flex flex-col gap-3">
                    <button
                      onClick={handleReset}
                      disabled={resetBusy}
                      className="w-full py-4 bg-orange-500 hover:bg-orange-600 text-white rounded-2xl font-bold transition-all shadow-lg shadow-orange-500/20 active:scale-95 disabled:opacity-60 disabled:cursor-not-allowed disabled:hover:bg-orange-500"
                    >
                      {resetBusy ? 'Resetting...' : 'Yes, Reset Everything'}
                    </button>
                    <button
                      onClick={() => setShowResetConfirm(false)}
                      disabled={resetBusy}
                      className="w-full py-4 bg-main/5 hover:bg-main/10 text-muted hover:text-main rounded-2xl font-bold transition-all active:scale-95 disabled:opacity-60 disabled:cursor-not-allowed"
                    >
                      Cancel
                    </button>
                  </div>
                </>
              ) : (
                <>
                  <p className={`text-center text-sm leading-relaxed mb-8 ${resetStatus.startsWith('Reset failed') || resetStatus.startsWith('Restart failed') ? 'text-red-200' : 'text-muted'}`}>
                    {resetStatus}
                  </p>
                  <div className="flex flex-col gap-3">
                    <button
                      onClick={handleRestart}
                      disabled={restartBusy || resetStatus.startsWith('Reset failed')}
                      className="w-full py-4 bg-orange-500 hover:bg-orange-600 text-white rounded-2xl font-bold transition-all shadow-lg shadow-orange-500/20 active:scale-95 disabled:opacity-60 disabled:cursor-not-allowed disabled:hover:bg-orange-500"
                    >
                      {restartBusy ? 'Restarting...' : 'Restart Browser'}
                    </button>
                    <button
                      onClick={() => setShowResetConfirm(false)}
                      disabled={restartBusy}
                      className="w-full py-4 bg-main/5 hover:bg-main/10 text-muted hover:text-main rounded-2xl font-bold transition-all active:scale-95 disabled:opacity-60 disabled:cursor-not-allowed"
                    >
                      Close
                    </button>
                  </div>
                </>
              )}
            </div>
          </div>
        </div>
      )}

      {/* Clear Data Confirm Dialog */}
      {showClearConfirm && (
        <div className="fixed inset-0 z-[100] flex items-center justify-center p-6 animate-in fade-in duration-300">
          <div className="absolute inset-0 bg-slate-950/60 backdrop-blur-sm" onClick={() => setShowClearConfirm(false)} />
          <div
            className="relative w-full max-w-md border border-main rounded-[2.5rem] p-10 shadow-2xl animate-in zoom-in-95 duration-300"
            style={{ backgroundColor: 'var(--bg-card)' }}
          >
            <div className="relative z-10">
              <div className="w-16 h-16 rounded-3xl bg-red-500/15 border border-red-500/30 flex items-center justify-center mb-8 mx-auto">
                <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ef4444" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                  <polyline points="3 6 5 6 21 6" /><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
                </svg>
              </div>
              <h2 className="text-2xl font-black text-center text-main mb-4 tracking-tight">
                {clearStatus ? 'Cleared' : 'Clear Browsing Data?'}
              </h2>
              {!clearStatus ? (
                <>
                  <p className="text-muted text-center text-sm leading-relaxed mb-4">
                    The following items will be deleted. This action cannot be undone.
                  </p>
                  <div className="space-y-1 mb-8">
                    {clearItems.history && <p className="text-xs text-center text-muted">Browsing history</p>}
                    {clearItems.downloads && <p className="text-xs text-center text-muted">Download history</p>}
                    {clearItems.cookies && <p className="text-xs text-center text-muted">Cookies and other site data</p>}
                    {clearItems.cache && <p className="text-xs text-center text-muted">Cached images and files</p>}
                    {clearItems.siteData && <p className="text-xs text-center text-muted">Site storage</p>}
                  </div>
                  <div className="flex flex-col gap-3">
                    <button
                      onClick={() => {
                        setClearBusy(true);
                        const categories = Object.entries(clearItems)
                          .filter(([, v]) => v)
                          .map(([k]) => k);
                        window.cefQuery({
                          request: `clear-browsing-data:${JSON.stringify({ categories, timeRange: clearTimeRange })}`,
                          onSuccess: () => {
                            setClearStatus('Browsing data cleared.');
                            setClearBusy(false);
                            setSiteUsage([]);
                            setStorageTotals(null);
                          },
                          onFailure: (code, msg) => {
                            setClearStatus(`Failed: ${msg || code}`);
                            setClearBusy(false);
                          }
                        });
                      }}
                      disabled={clearBusy}
                      className="w-full py-4 bg-red-500 hover:bg-red-600 text-white rounded-2xl font-bold transition-all shadow-lg shadow-red-500/20 active:scale-95 disabled:opacity-60 disabled:cursor-not-allowed cursor-pointer"
                    >
                      {clearBusy ? 'Clearing...' : 'Clear Data'}
                    </button>
                    <button
                      onClick={() => setShowClearConfirm(false)}
                      disabled={clearBusy}
                      className="w-full py-4 bg-main/5 hover:bg-main/10 text-muted hover:text-main rounded-2xl font-bold transition-all active:scale-95 disabled:opacity-60 disabled:cursor-not-allowed"
                    >
                      Cancel
                    </button>
                  </div>
                </>
              ) : (
                <>
                  <p className="text-center text-sm leading-relaxed mb-8 text-muted">{clearStatus}</p>
                  <button
                    onClick={() => { setShowClearConfirm(false); setClearStatus(''); }}
                    className="w-full py-4 bg-main/5 hover:bg-main/10 text-muted hover:text-main rounded-2xl font-bold transition-all active:scale-95"
                  >
                    Close
                  </button>
                </>
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default Settings;
