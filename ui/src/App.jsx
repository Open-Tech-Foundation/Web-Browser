import React, { useRef, useEffect, useReducer } from 'react';
import AddressBar from './components/AddressBar';
import TabStrip from './components/TabStrip';
import './styles/App.css';

const BROWSER_SCHEME = {
  SETTINGS: 'browser://settings',
};

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
      const nextTabs = state.tabs.filter(t => t.id !== action.payload);
      let nextActiveId = state.activeTabId;
      if (state.activeTabId === action.payload && nextTabs.length > 0) {
        nextActiveId = nextTabs[nextTabs.length - 1].id;
      }
      return { ...state, tabs: nextTabs, activeTabId: nextActiveId };
    default:
      return state;
  }
};

const App = () => {
  const [state, dispatch] = useReducer(tabReducer, { tabs: [], activeTabId: null });
  const [searchEngine, setSearchEngine] = React.useState('');
  const initialized = useRef(false);
  const stateRef = useRef(state);
  stateRef.current = state;
  const addressBarRef = useRef(null);

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
              const tabData = JSON.parse(event.value);
              dispatch({
                type: 'ADD_TAB',
                payload: { ...tabData, loading: false }
              });
            } else if (event.key === 'load-end') {
              const tab = stateRef.current.tabs.find(t => t.id === event.id);
              if (tab && !tab.url) addressBarRef.current?.focus();
            } else if (event.key === 'settings-changed') {
              try {
                const settings = JSON.parse(event.value);
                setSearchEngine(settings.searchEngine || '');
              } catch (e) {}
            } else {
              dispatch({ 
                type: 'UPDATE_TAB', 
                payload: { 
                  id: event.id, 
                  key: event.key, 
                  value: ['loading', 'canGoBack', 'canGoForward'].includes(event.key)
                    ? event.value === 'true' 
                    : event.value 
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
            const existingTabs = JSON.parse(tabsJson);
            if (existingTabs.length > 0) {
              const formattedTabs = existingTabs.map(t => ({
                ...t,
                url: (t.url && (t.url.startsWith('browser://newtab') || t.url.includes('/newtab.html'))) ? '' : (t.url || ''),
                loading: false
              }));
              dispatch({ type: 'SET_TABS', payload: formattedTabs });
              dispatch({ type: 'SET_ACTIVE', payload: formattedTabs[0].id });
            } else {
              handleNewTab();
            }
            setTimeout(() => addressBarRef.current?.focus(), 100);
          } catch (e) {
            console.error("Failed to parse initial tabs:", e);
          }
        },
        onFailure: (code, msg) => console.error("Failed to get tabs:", msg)
      });
    }
  }, []);

  const buildSearchUrl = (engine, query) => {
    const engineMap = {
      google: 'https://www.google.com/search?q=',
      bing: 'https://www.bing.com/search?q=',
      yahoo: 'https://search.yahoo.com/search?p=',
      duckduckgo: 'https://duckduckgo.com/?q=',
      baidu: 'https://www.baidu.com/s?wd=',
      yandex: 'https://yandex.com/search/?text=',
      ecosia: 'https://www.ecosia.org/search?q=',
      naver: 'https://search.naver.com/search.naver?query=',
      startpage: 'https://www.startpage.com/search?q='
    };
    const baseUrl = engineMap[engine];
    if (!baseUrl) return BROWSER_SCHEME.SETTINGS;
    return baseUrl + encodeURIComponent(query);
  };

  const handleNavigate = (input) => {
    if (state.activeTabId !== null) {
      let finalUrl = input;
      // Simple URL detection: starts with protocol or contains a dot and no spaces
      const isUrl = /^[a-zA-Z][a-zA-Z\d+./-]*:\/\//.test(input) || 
                    (input.includes('.') && !input.includes(' '));
      
      if (!isUrl && !input.startsWith('browser://')) {
        finalUrl = buildSearchUrl(searchEngine, input);
      }
      
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
      request: url ? `new-tab:${url}` : "new-tab:",
      onSuccess: (id) => {
        const newId = parseInt(id);
        const newTab = { id: newId, title: 'New Tab', url: url || '', loading: false };
        dispatch({ type: 'ADD_TAB', payload: newTab });
        dispatch({ type: 'SET_ACTIVE', payload: newId });
        window.cefQuery({ request: `switch-tab:${newId}` });
        addressBarRef.current?.focus();
      }
    });
  };

  const handleCloseTab = (id) => {
    window.cefQuery({ request: `close-tab:${id}` });
    dispatch({ type: 'REMOVE_TAB', payload: id });
    // Side effect: tell C++ to switch if we closed the active one
    const remaining = state.tabs.filter(t => t.id !== id);
    if (state.activeTabId === id && remaining.length > 0) {
      const nextId = remaining[remaining.length - 1].id;
      window.cefQuery({ request: `switch-tab:${nextId}` });
    }
  };

  const handleSwitchTab = (id) => {
    window.cefQuery({ request: `switch-tab:${id}` });
    dispatch({ type: 'SET_ACTIVE', payload: id });
  };

  const handleNavAction = (action) => {
    if (state.activeTabId !== null) {
      window.cefQuery({ request: `${action}:${state.activeTabId}` });
    }
  };

  const currentActiveTab = state.tabs.find(t => t.id === state.activeTabId);

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
        <AddressBar ref={addressBarRef} url={currentActiveTab?.url || ''} onNavigate={handleNavigate} />
        <div className="flex ml-1">
          <NavButton 
            onClick={() => handleNewTab(BROWSER_SCHEME.SETTINGS)}
            icon={<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3"/></svg>}
          />
        </div>
      </div>
    </div>
  );
};

const NavButton = ({ icon, onClick, disabled }) => (
  <button 
    onClick={onClick}
    disabled={disabled}
    className={`
      w-7 h-7 flex items-center justify-center rounded transition-colors active:scale-90
      ${disabled 
        ? 'opacity-30 cursor-default' 
        : 'hover:bg-slate-200 dark:hover:bg-white/10 text-slate-700 dark:text-slate-200'}
    `}
  >
    {icon}
  </button>
);

export default App;
