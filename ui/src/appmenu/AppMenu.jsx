import React from 'react';
import { isBridgeAvailable, nativeRequest } from '../shared/nativeRequest';

const AppMenu = () => {
  const [guestSession, setGuestSession] = React.useState(false);
  const hideAppMenu = () => nativeRequest({ method: 'ui.appMenu.hide' }).catch(() => {});
  const handleAction = (request) => {
    const rpcMethods = {
      'toggle-console': 'ui.console.toggle',
      'show-findbar': 'ui.findbar.show',
      'start-snip': 'ui.snip.start',
      newPrivateTab: 'navigation.newPrivateTab',
    };
    if (rpcMethods[request]) {
      nativeRequest({ method: rpcMethods[request] })
        .then(hideAppMenu)
        .catch(() => {});
    }
  };

  React.useEffect(() => {
    nativeRequest({ method: 'session.isGuest' })
      .then((value) => setGuestSession(value === true))
      .catch(() => {});
  }, []);

  const handleGuestSession = () => {
    nativeRequest({ method: 'session.createGuest' })
      .then(() => {
        setGuestSession(true);
        hideAppMenu();
      })
      .catch(() => {});
  };

  const handleNavigate = (url) => {
    if (isBridgeAvailable()) {
      nativeRequest({
        method: 'navigation.newTab',
        params: { url },
      })
        .then(() => {
          hideAppMenu();
        })
        .catch(() => {});
    }
  };

  React.useEffect(() => {
    const onBlur = () => {
      if (isBridgeAvailable()) {
        hideAppMenu();
      }
    };
    const onKeyDown = (e) => {
      if (e.key === 'Escape' && isBridgeAvailable()) {
        hideAppMenu();
      }
    };
    window.addEventListener('blur', onBlur);
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('blur', onBlur);
      window.removeEventListener('keydown', onKeyDown);
    };
  }, []);

  return (
    <div className="w-full h-full p-2 bg-transparent box-border">
      <div className="w-full h-full bg-white dark:bg-[#0a0a0c] border border-slate-200 dark:border-white/10 rounded-2xl shadow-2xl p-5 flex flex-col overflow-hidden relative">

        <SectionHeader label="Data" />
        <div className="grid grid-cols-3 gap-4 relative z-10 mb-4">
          <AppMenuItem
            name="Downloads"
            icon={<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12 3v11"/><path d="m7 9 5 5 5-5"/><path d="M5 21h14"/></svg>}
            onClick={() => handleNavigate('browser://downloads')}
          />
          <AppMenuItem
            name="Bookmarks"
            icon={<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M4 19.5v-15A2.5 2.5 0 0 1 6.5 2H20v20H6.5a2.5 2.5 0 0 1 0-5H20"/></svg>}
            onClick={() => handleNavigate('browser://bookmarks')}
          />
          <AppMenuItem
            name="History"
            icon={<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12 8v5l3 3"/><circle cx="12" cy="12" r="9"/></svg>}
            onClick={() => handleNavigate('browser://history')}
          />
        </div>

        <SectionHeader label="Tools" />
        <div className="grid grid-cols-3 gap-4 relative z-10 mb-4">
          <AppMenuItem
            name="Console"
            icon={<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg>}
            onClick={() => handleAction('toggle-console')}
          />
          <AppMenuItem
            name="Find"
            icon={<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>}
            onClick={() => handleAction('show-findbar')}
          />
          <AppMenuItem
            name="Page Capture"
            icon={<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"/><circle cx="12" cy="13" r="4"/></svg>}
            onClick={() => handleAction('start-snip')}
          />
        </div>

        <SectionHeader label="Privacy" />
        <div className="grid grid-cols-3 gap-4 relative z-10">
          <AppMenuItem
            name="Private Tab"
            icon={<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M2 13h20"/><path d="M5 13l1.5-5.5A2 2 0 0 1 8.4 6h7.2a2 2 0 0 1 1.9 1.5L19 13"/><circle cx="7" cy="16" r="2.5"/><circle cx="17" cy="16" r="2.5"/><path d="M9.5 16h5"/></svg>}
            onClick={() => handleAction('newPrivateTab')}
          />
          <AppMenuItem
            name={guestSession ? 'Guest Active' : 'Guest Session'}
            accent="violet"
            icon={<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12 3 4 7v5c0 5 3.4 8.7 8 9 4.6-.3 8-4 8-9V7l-8-4Z"/><path d="M9 12h6"/></svg>}
            onClick={handleGuestSession}
          />
        </div>

      </div>
    </div>
  );
};

const SectionHeader = ({ label }) => (
  <div className="text-[9px] font-black text-slate-400 dark:text-slate-500 uppercase tracking-[0.15em] mb-2 ml-0.5">{label}</div>
);

const AppMenuItem = ({ name, icon, onClick, accent = 'orange' }) => (
  <button 
    onClick={onClick}
    className="flex flex-col items-center gap-3 p-2 rounded-2xl transition-all group active:scale-90"
  >
    <div className={`w-14 h-14 flex items-center justify-center bg-slate-100 dark:bg-white/5 rounded-2xl border border-transparent transition-all shadow-sm relative overflow-hidden ${
      accent === 'violet'
        ? 'text-violet-700 dark:text-violet-200 group-hover:border-violet-500/50 group-hover:bg-violet-500/10 group-hover:text-violet-500'
        : 'text-slate-700 dark:text-slate-200 group-hover:border-orange-500/50 group-hover:bg-orange-500/10 group-hover:text-orange-500'
    }`}>
      {icon}
    </div>
    <span className={`text-[10px] font-black text-slate-600 dark:text-slate-300 transition-colors uppercase tracking-widest ${
      accent === 'violet' ? 'group-hover:text-violet-500' : 'group-hover:text-orange-500'
    }`}>{name}</span>
  </button>
);

export default AppMenu;
