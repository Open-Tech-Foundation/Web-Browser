import React, { useRef, useEffect, useReducer } from 'react';
import AddressBar from './components/AddressBar';
import TabStrip from './components/TabStrip';
import { resolveUrl } from './shared/search';
import './styles/App.css';

const BROWSER_SCHEME = {
  SETTINGS: 'browser://settings',
  HISTORY: 'browser://history',
  BOOKMARKS: 'browser://bookmarks',
};

const normalizeTab = (tab) => ({
  ...tab,
  url: (tab.url && (tab.url.startsWith('browser://newtab') || tab.url.includes('/newtab.html'))) ? '' : (tab.url || ''),
  loading: Boolean(tab.loading),
  canGoBack: Boolean(tab.canGoBack),
  canGoForward: Boolean(tab.canGoForward),
  zoomPercent: Number(tab.zoomPercent ?? 100),
  sslError: Boolean(tab.sslError),
  bookmarked: Boolean(tab.bookmarked),
});

const tabReducer = (state, action) => {
  switch (action.type) {
    case 'SET_TABS':
      return { ...state, tabs: action.payload };
    case 'UPDATE_TAB':
      return {
        ...state,
        tabs: state.tabs.map(tab => 
          tab.id === action.payload.id 
            ? { ...tab, [action.payload.key]: action.payload.value } 
            : tab
        )
      };
    case 'SET_ACTIVE':
      return { ...state, activeTabId: action.payload };
    case 'ADD_TAB':
      return { 
        ...state, 
        tabs: [...state.tabs, action.payload]
      };
    case 'REMOVE_TAB':
      return { ...state, tabs: state.tabs.filter(t => t.id !== action.payload) };
    default:
      return state;
  }
};

const App = () => {
  const [state, dispatch] = useReducer(tabReducer, { tabs: [], activeTabId: null });
  const [searchEngine, setSearchEngine] = React.useState('');
  const [downloadBadge, setDownloadBadge] = React.useState(0);
  const [hasDownloads, setHasDownloads] = React.useState(false);
  const initialized = useRef(false);
  const stateRef = useRef(state);
  stateRef.current = state;
  const addressBarRef = useRef(null);
  const [appearanceMode, setAppearanceMode] = React.useState('auto');

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
      if (appearanceMode === 'auto') {
        applyTheme('auto');
      }
    };
    mediaQuery.addEventListener('change', handleChange);
    return () => mediaQuery.removeEventListener('change', handleChange);
  }, [appearanceMode]);

  useEffect(() => {
    if (initialized.current) return;
    initialized.current = true;

    if (window.cefQuery) {
      // Load settings
      window.cefQuery({
        request: "get-settings",
        onSuccess: (response) => {
          try {
            const settings = JSON.parse(response);
            setSearchEngine(settings.searchEngine || '');
            setAppearanceMode(settings.appearanceMode || 'auto');
          } catch (e) {}
        }
      });

      // Subscribe to real-time events from the browser engine
      window.cefQuery({
        request: "subscribe-events",
        persistent: true,
        onSuccess: (eventStr) => {
          try {
            const event = JSON.parse(eventStr);
            if (event.key === 'new-tab') {
              const tabData = normalizeTab(event.tab || {});
              dispatch({
                type: 'ADD_TAB',
                payload: tabData
              });
              // Tab opened, focus will be handled by the page content (e.g. New Tab page)
            } else if (event.key === 'load-end') {
              const tab = stateRef.current.tabs.find(t => t.id === event.id);
              if (tab && !tab.url) addressBarRef.current?.focus();
            } else if (event.key === 'settings-changed') {
              const nextSearchEngine = event.settings?.searchEngine || '';
              setSearchEngine(nextSearchEngine);
              setAppearanceMode(event.settings?.appearanceMode || 'auto');
              window.dispatchEvent(
                new CustomEvent('otf-settings-changed', {
                  detail: {
                    searchEngine: nextSearchEngine,
                  },
                })
              );
            } else if (event.key === 'tab-closed') {
              dispatch({ type: 'REMOVE_TAB', payload: event.id });
            } else if (event.key === 'active-tab-changed') {
              dispatch({ type: 'SET_ACTIVE', payload: event.id });
              const tab = stateRef.current.tabs.find(t => t.id === event.id);
              if (tab && tab.url) {
                addressBarRef.current?.blur();
              }
            } else if (event.key === 'downloads-badge') {
              setDownloadBadge(Number(event.value) || 0);
              setHasDownloads(Number(event.total) > 0);
            } else if (event.key === 'bookmarks-changed') {
              dispatch({
                type: 'UPDATE_TAB',
                payload: { id: event.id, key: 'bookmarked', value: Boolean(event.bookmarked) }
              });
            } else if (event.key === 'bookmark-sync') {
              dispatch({
                type: 'UPDATE_TAB',
                payload: { id: event.id, key: 'bookmarked', value: Boolean(event.bookmarked) }
              });
            } else if (event.key === 'shortcut') {
              if (event.value === 'focus-bar') {
                addressBarRef.current?.focus();
                window.cefQuery({ request: 'focus-ui' });
              }
            } else {
              dispatch({
                type: 'UPDATE_TAB', 
                payload: { 
                  id: event.id, 
                  key: event.key, 
                  value: event.key === 'zoomPercent' ? Number(event.value) : event.value
                } 
              });
            }
          } catch (e) {
            console.error("Failed to parse browser event:", e);
          }
        },
        onFailure: (code, msg) => console.error("Event subscription failed:", msg)
      });

      // Sync initial state with the C++ backend
      window.cefQuery({ 
        request: "get-tabs",
        onSuccess: (tabsJson) => {
          try {
            const existingTabs = JSON.parse(tabsJson).map(normalizeTab);
            if (existingTabs.length > 0) {
              dispatch({ type: 'SET_TABS', payload: existingTabs });
              window.cefQuery({
                request: 'get-active-tab',
                onSuccess: (activeId) => {
                  const parsedId = parseInt(activeId, 10);
                  dispatch({ type: 'SET_ACTIVE', payload: parsedId });
                },
              });
            } else {
              handleNewTab();
            }
          } catch (e) {
            console.error("Failed to parse initial tabs:", e);
          }
        },
        onFailure: (code, msg) => console.error("Failed to get tabs:", msg)
      });

      window.cefQuery({
        request: 'get-downloads',
        onSuccess: (json) => {
          try {
            const items = JSON.parse(json);
            setHasDownloads(Array.isArray(items) && items.length > 0);
          } catch (e) {}
        },
      });
    }
  }, []);

  const handleNavigate = (input) => {
    if (state.activeTabId !== null) {
      const finalUrl = resolveUrl(input, searchEngine);
      window.cefQuery({ request: `navigate:${state.activeTabId}:${finalUrl}` });
      
      if (finalUrl === BROWSER_SCHEME.SETTINGS) {
        setTimeout(() => {
          window.cefQuery({
            request: "get-settings",
            onSuccess: (response) => {
              try {
                const settings = JSON.parse(response);
                setSearchEngine(settings.searchEngine || '');
              } catch (e) {}
            }
          });
        }, 1000);
      }
    }
  };

  const handleNewTab = (url = "") => {
    window.cefQuery({
      request: url ? `new-tab:${url}` : "new-tab:"
    });
  };

  const handleCloseTab = (id) => {
    window.cefQuery({ request: `close-tab:${id}` });
  };

  const handleSwitchTab = (id) => {
    window.cefQuery({ request: `switch-tab:${id}` });
  };

  const handleNavAction = (action) => {
    if (state.activeTabId !== null) {
      window.cefQuery({ request: `${action}:${state.activeTabId}` });
    }
  };

  const handleZoomAction = (action) => {
    if (state.activeTabId !== null) {
      window.cefQuery({ request: `${action}:${state.activeTabId}` });
    }
  };

  const handleToggleBookmark = () => {
    window.cefQuery({
      request: 'toggle-bookmark-current',
    });
  };

  const handleShowCertificate = () => {
    window.cefQuery({
      request: 'toggle-certificate'
    });
  };

  const currentActiveTab = state.tabs.find(t => t.id === state.activeTabId);
  const downloadButtonClass = downloadBadge > 0 ? 'animate-download-pulse text-brand-orange' : '';

  if (state.tabs.length === 0) {
    return (
      <div className="flex flex-col h-[60px] bg-slate-50 items-center justify-center border-b border-slate-200">
        <span className="text-[10px] font-medium text-slate-400 animate-pulse">Initializing Browser Engine...</span>
      </div>
    );
  }

  return (
    <div className="flex flex-col h-[60px] max-h-[60px] w-full bg-slate-100 dark:bg-slate-950 border-b border-slate-300 dark:border-slate-800 antialiased overflow-hidden select-none box-border m-0 p-0">
      <TabStrip
        tabs={state.tabs.map(t => ({ ...t, active: t.id === state.activeTabId }))}
        onSwitch={handleSwitchTab}
        onClose={handleCloseTab}
        onNew={handleNewTab}
      />
      <div className="flex items-center px-3 h-[36px] min-h-[36px] bg-bar-light dark:bg-bar-dark box-border border-t border-slate-200 dark:border-white/5 m-0 p-0">
          <div className="flex gap-0.5 mr-2">
            <NavButton
              disabled={!currentActiveTab?.canGoBack}
              onClick={() => handleNavAction('back')}
              icon={<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="m15 18-6-6 6-6"/></svg>}
            />
            <NavButton
              disabled={!currentActiveTab?.canGoForward}
              onClick={() => handleNavAction('forward')}
              icon={<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="m9 18 6-6-6-6"/></svg>}
            />
            <NavButton onClick={() => handleNavAction(currentActiveTab?.loading ? 'stop' : 'reload')} icon={
              currentActiveTab?.loading
                ? <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>
                : <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.85.83 6.72 2.38L21 8"/><path d="M21 3v5h-5"/></svg>
            } />
          </div>
          <AddressBar 
            ref={addressBarRef} 
            url={currentActiveTab?.url || ''} 
            tabId={state.activeTabId} 
            onNavigate={handleNavigate}
            isBookmarked={Boolean(currentActiveTab?.bookmarked)}
            onToggleBookmark={handleToggleBookmark}
            sslError={currentActiveTab?.sslError}
            onShowCertificate={handleShowCertificate}
          />
          <div className="flex items-center ml-1 gap-1">
            <NavButton
              onClick={() => window.cefQuery({ request: 'toggle-zoombar' })}
              icon={
                <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
                  <circle cx="11" cy="11" r="7"/><path d="m21 21-4.35-4.35"/><path d="M11 8v6"/><path d="M8 11h6"/>
                </svg>
              }
            />
            {hasDownloads && (
              <div className="relative">
                <NavButton
                  onClick={() => window.cefQuery({ request: 'toggle-downloadsbar' })}
                  className={downloadButtonClass}
                  icon={
                    <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
                      <path d="M12 3v11"/><path d="m7 9 5 5 5-5"/><path d="M5 21h14"/>
                    </svg>
                  }
                />
                {downloadBadge > 0 && (
                  <span className="absolute -right-0.5 -top-0.5 min-w-[14px] rounded-full bg-brand-orange px-1 text-center text-[10px] font-semibold leading-[14px] text-white">
                    {downloadBadge > 9 ? '9+' : downloadBadge}
                  </span>
                )}
              </div>
            )}
            <NavButton
              onClick={() => window.cefQuery({ request: 'toggle-appmenu' })}
              icon={<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="1"/><circle cx="12" cy="5" r="1"/><circle cx="12" cy="19" r="1"/></svg>}
            />
            <NavButton
              onClick={() => handleNewTab(BROWSER_SCHEME.SETTINGS)}
              icon={<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3"/></svg>}
            />
          </div>
        </div>
    </div>
  );
};

const AppMenuItem = ({ name, icon, onClick }) => (
  <button 
    onClick={onClick}
    className="flex flex-col items-center gap-2 p-3 rounded-xl hover:bg-slate-100 dark:hover:bg-white/5 transition-all group active:scale-95"
  >
    <div className="w-12 h-12 flex items-center justify-center bg-slate-100 dark:bg-white/5 rounded-xl border border-transparent group-hover:border-orange-500/30 group-hover:bg-orange-500/10 group-hover:text-orange-500 text-slate-600 dark:text-slate-400 transition-all">
      {icon}
    </div>
    <span className="text-[10px] font-bold text-slate-500 dark:text-slate-400 group-hover:text-slate-900 dark:group-hover:text-white transition-colors">{name}</span>
  </button>
);

const NavButton = ({ icon, onClick, disabled, className = '' }) => (
  <button 
    onClick={onClick}
    disabled={disabled}
    className={`
      w-7 h-7 flex items-center justify-center rounded transition-all active:scale-90
      ${disabled 
        ? 'opacity-30 cursor-default' 
        : 'hover:bg-slate-200 dark:hover:bg-white/10 text-slate-700 dark:text-slate-200'}
      ${className}
    `}
  >
    {icon}
  </button>
);

export default App;
