import React from 'react';

const TabStrip = ({ tabs, onSwitch, onClose, onNew }) => {
  return (
    <div className="flex items-end px-1 gap-0.5 h-[24px] bg-slate-200 dark:bg-slate-900/80 overflow-x-auto no-scrollbar">
      {tabs.map(tab => (
        <div 
          key={tab.id}
          onClick={() => onSwitch(tab.id)}
          className={`
            group relative flex items-center h-[24px] px-3 min-w-[140px] max-w-[220px] rounded-t-lg text-[11px] cursor-pointer transition-all duration-150
            ${tab.active 
              ? 'bg-bar-light dark:bg-bar-dark text-slate-900 dark:text-slate-100 shadow-[0_-1px_3px_rgba(0,0,0,0.1)]' 
              : 'text-slate-500 hover:bg-white/50 dark:hover:bg-white/5'}
          `}
        >
          {tab.active && (
            <div className="absolute top-0 left-0 right-0 h-0.5 bg-brand-orange rounded-t-lg" />
          )}
          <div className="flex items-center flex-1 min-w-0 mr-2">
            {tab.favicon ? (
              <img src={tab.favicon} className="w-3.5 h-3.5 mr-2 shrink-0 object-contain" alt="" />
            ) : (
              <svg className="w-3.5 h-3.5 mr-2 shrink-0 text-slate-400" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/></svg>
            )}
            <span className="truncate font-medium">{tab.title || tab.url || 'New Tab'}</span>
          </div>
          <button 
            onClick={(e) => { e.stopPropagation(); onClose(tab.id); }}
            className="ml-2 w-4 h-4 flex items-center justify-center rounded-full opacity-0 group-hover:opacity-100 hover:bg-slate-300 dark:hover:bg-white/20 transition-all"
          >
            <svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>
          </button>
        </div>
      ))}
      <button 
        onClick={onNew}
        className="h-[24px] w-8 flex items-center justify-center text-slate-600 dark:text-slate-400 hover:text-brand-orange hover:bg-white/50 dark:hover:bg-white/5 rounded-t-lg transition-all mb-0.5"
      >
        <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M5 12h14"/><path d="M12 5v14"/></svg>
      </button>
    </div>
  );
};

export default TabStrip;
