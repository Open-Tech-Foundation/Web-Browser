import React from 'react';

const WorkspaceSwitcher = ({ workspaces, activeId }) => {
  const active = workspaces.find((w) => w.id === activeId) || workspaces[0];
  const activeName = active?.name || 'Default';

  return (
    <button
      tabIndex={-1}
      onClick={() => window.cefQuery?.({ request: 'toggle-popup:workspace' })}
      title="Workspaces"
      className="h-[23px] mx-1 px-2 flex items-center gap-1 rounded-md text-[11px] font-semibold text-slate-700 dark:text-slate-200 bg-white dark:bg-[#1a1a20] hover:text-brand-orange transition-all shadow-sm shrink-0"
    >
      <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.4" strokeLinecap="round" strokeLinejoin="round">
        <rect x="3" y="3" width="7" height="7" rx="1" />
        <rect x="14" y="3" width="7" height="7" rx="1" />
        <rect x="3" y="14" width="7" height="7" rx="1" />
        <rect x="14" y="14" width="7" height="7" rx="1" />
      </svg>
      <span className="truncate max-w-[100px]">{activeName}</span>
      <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
        <path d="m6 9 6 6 6-6" />
      </svg>
    </button>
  );
};

export default WorkspaceSwitcher;
