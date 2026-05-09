import React, { useRef, useEffect, useReducer } from 'react';
import AddressBar from './components/AddressBar';
import TabStrip from './components/TabStrip';
import './styles/App.css';

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
        tabs: [...state.tabs, action.payload],
        activeTabId: action.payload.id 
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
  const initialized = useRef(false);

  useEffect(() => {
    if (initialized.current) return;
    initialized.current = true;

    if (window.cefQuery) {
      window.cefQuery({
        request: "subscribe-events",
        persistent: true,
        onSuccess: (eventStr) => {
          try {
            const event = JSON.parse(eventStr);
            dispatch({ 
              type: 'UPDATE_TAB', 
              payload: { 
                id: event.id, 
                key: event.key, 
                value: event.key === 'loading' ? event.value === 'true' : event.value 
              } 
            });
          } catch (e) {}
        }
      });

      // Initial tab
      window.cefQuery({ 
        request: "new-tab:https://www.google.com",
        onSuccess: (id) => {
          const newId = parseInt(id);
          const newTab = { id: newId, title: 'New Tab', url: 'https://www.google.com', loading: false };
          dispatch({ type: 'ADD_TAB', payload: newTab });
          window.cefQuery({ request: `switch-tab:${newId}` });
        }
      });
    }
  }, []);

  const handleNavigate = (url) => {
    if (state.activeTabId !== null) {
      window.cefQuery({ request: `navigate:${state.activeTabId}:${url}` });
    }
  };

  const handleNewTab = () => {
    window.cefQuery({ 
      request: "new-tab:https://www.google.com",
      onSuccess: (id) => {
        const newId = parseInt(id);
        const newTab = { id: newId, title: 'New Tab', url: 'https://www.google.com', loading: false };
        dispatch({ type: 'ADD_TAB', payload: newTab });
        window.cefQuery({ request: `switch-tab:${newId}` });
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
          <NavButton onClick={() => handleNavAction('back')} icon={<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="m15 18-6-6 6-6"/></svg>} />
          <NavButton onClick={() => handleNavAction('forward')} icon={<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="m9 18 6-6-6-6"/></svg>} />
          <NavButton onClick={() => handleNavAction('reload')} icon={<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.85.83 6.72 2.38L21 8"/><path d="M21 3v5h-5"/></svg>} />
        </div>
        <AddressBar url={currentActiveTab?.url || ''} onNavigate={handleNavigate} />
      </div>
    </div>
  );
};

const NavButton = ({ icon, onClick }) => (
  <button 
    onClick={onClick}
    className="w-7 h-7 flex items-center justify-center rounded hover:bg-slate-200 dark:hover:bg-white/10 text-slate-700 dark:text-slate-200 transition-colors active:scale-90"
  >
    {icon}
  </button>
);

export default App;
