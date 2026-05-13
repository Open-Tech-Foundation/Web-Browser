import React from 'react';

const AppMenu = () => {
  const handleAction = (request) => {
    if (window.cefQuery) {
      window.cefQuery({
        request,
        onSuccess: () => {
          // Hide menu after action
          window.cefQuery({ request: 'hide-appmenu' });
        }
      });
    }
  };

  const handleNavigate = (url) => {
    if (window.cefQuery) {
      window.cefQuery({
        request: `new-tab:${url}`,
        onSuccess: () => {
          window.cefQuery({ request: 'hide-appmenu' });
        }
      });
    }
  };

  React.useEffect(() => {
    const onBlur = () => {
      if (window.cefQuery) {
        window.cefQuery({ request: 'hide-appmenu' });
      }
    };
    const onKeyDown = (e) => {
      if (e.key === 'Escape' && window.cefQuery) {
        window.cefQuery({ request: 'hide-appmenu' });
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
    <div className="w-full h-full bg-white dark:bg-[#0a0a0c] border border-slate-200 dark:border-white/10 rounded-2xl shadow-2xl p-5 flex flex-col overflow-hidden relative">
      <div className="grid grid-cols-4 gap-4 relative z-10">
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
        <AppMenuItem 
          name="Security" 
          icon={<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>}
          onClick={() => handleNavigate('browser://security')}
        />
      </div>
    </div>
  );
};

const AppMenuItem = ({ name, icon, onClick }) => (
  <button 
    onClick={onClick}
    className="flex flex-col items-center gap-3 p-2 rounded-2xl transition-all group active:scale-90"
  >
    <div className="w-14 h-14 flex items-center justify-center bg-slate-100 dark:bg-white/5 rounded-2xl border border-transparent group-hover:border-orange-500/50 group-hover:bg-orange-500/10 text-slate-700 dark:text-slate-200 group-hover:text-orange-500 transition-all shadow-sm relative overflow-hidden">
      {icon}
    </div>
    <span className="text-[10px] font-black text-slate-600 dark:text-slate-300 group-hover:text-orange-500 transition-colors uppercase tracking-widest">{name}</span>
  </button>
);

export default AppMenu;
