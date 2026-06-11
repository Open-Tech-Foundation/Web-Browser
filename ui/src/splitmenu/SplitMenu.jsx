import React from 'react';
import Popup from '../components/Popup';
import { nativeRequest } from '../shared/nativeRequest';
import '../styles/App.css';

const send = (request) => {
  if (window.cefQuery) window.cefQuery({ request });
};

const close = () => nativeRequest({
  method: 'ui.popup.hide',
  params: { name: 'splitmenu' },
}).catch(() => {});

const MenuButton = ({ label, children, onClick }) => (
  <button
    type="button"
    onClick={() => {
      onClick();
      close();
    }}
    className="flex w-full items-center gap-2 rounded-lg px-3 py-2.5 text-left text-[12px] font-semibold text-slate-700 transition-colors hover:bg-slate-100 dark:text-slate-100 dark:hover:bg-white/10"
  >
    <svg className="h-4 w-4 shrink-0 text-brand-orange" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.3" strokeLinecap="round" strokeLinejoin="round">
      {children}
    </svg>
    <span className="truncate">{label}</span>
  </button>
);

const SplitMenu = () => {
  return (
    <Popup name="splitmenu" title="Split View" className="p-3 shadow-[0_18px_55px_rgba(15,23,42,0.28)]" closeOnBlur>
      <div className="space-y-1">
        <MenuButton label="Exit split view" onClick={() => send('close-split')}>
          <path d="M5 12h14" />
        </MenuButton>
        <MenuButton label="Swap panes" onClick={() => send('swap-split')}>
          <path d="m7 7-3 3 3 3" />
          <path d="M4 10h10" />
          <path d="m17 17 3-3-3-3" />
          <path d="M20 14H10" />
        </MenuButton>
        <div className="my-1 h-px bg-slate-200 dark:bg-slate-700" />
        <MenuButton label="Close left pane" onClick={() => send('close-split-pane:left')}>
          <path d="M9 4v16" />
          <path d="M4 6h3" />
          <path d="M4 12h3" />
          <path d="M4 18h3" />
          <path d="m17 9 4 4" />
          <path d="m21 9-4 4" />
        </MenuButton>
        <MenuButton label="Close right pane" onClick={() => send('close-split-pane:right')}>
          <path d="M15 4v16" />
          <path d="M17 6h3" />
          <path d="M17 12h3" />
          <path d="M17 18h3" />
          <path d="m3 9 4 4" />
          <path d="m7 9-4 4" />
        </MenuButton>
      </div>
    </Popup>
  );
};

export default SplitMenu;
