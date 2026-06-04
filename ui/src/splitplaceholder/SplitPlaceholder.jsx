import React from 'react';
import '../styles/App.css';

const SplitPlaceholder = () => (
  <div className="flex h-screen items-center justify-center bg-main px-8 text-main">
    <div className="max-w-sm text-center animate-in fade-in slide-in-from-bottom-4 duration-500">
      <div className="mx-auto mb-5 flex h-14 w-14 items-center justify-center rounded-2xl border border-orange-500/30 bg-orange-500/10 text-orange-500">
        <svg className="h-7 w-7" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.3" strokeLinecap="round" strokeLinejoin="round">
          <rect x="3" y="4" width="18" height="16" rx="2" />
          <path d="M12 4v16" />
          <path d="M7 9h2" />
          <path d="M15 9h2" />
        </svg>
      </div>
      <h1 className="mb-2 text-xl font-extrabold tracking-tight">Add a tab to split view</h1>
      <p className="text-sm leading-6 text-muted">
        Pick an existing tab from the tab strip or its context menu to place it in this pane.
      </p>
    </div>
  </div>
);

export default SplitPlaceholder;
