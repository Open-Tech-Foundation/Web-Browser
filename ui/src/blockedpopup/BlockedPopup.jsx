import React, { useState } from 'react';
import Popup, { usePopupRestore } from '../components/Popup';
import { nativeRequest } from '../shared/nativeRequest';

const BlockedPopup = () => {
  const [id, setId] = useState(0);
  const [url, setUrl] = useState('');
  const [origin, setOrigin] = useState('');

  usePopupRestore('blockedpopup', (payload) => {
    if (payload.id) setId(payload.id);
    if (payload.url) setUrl(payload.url);
    if (payload.origin) setOrigin(payload.origin);
  });

  const handleAllow = () => {
    window.cefQuery?.({ request: `allow-popup:${id}` });
  };

  const handleAlwaysAllow = () => {
    window.cefQuery?.({ request: `always-allow-popup:${origin}:${id}` });
  };

  const handleBlock = () => {
    nativeRequest({
      method: 'ui.popup.hide',
      params: { name: 'blockedpopup' },
    }).catch(() => {});
  };

  if (!origin) {
    return (
      <Popup name="blockedpopup" title="Pop-up blocked">
        <div className="flex-1 flex items-center justify-center text-slate-400 text-xs">
          No blocked pop-up
        </div>
      </Popup>
    );
  }

  return (
    <Popup name="blockedpopup" title="Pop-up blocked">
      <div className="flex-1 flex flex-col gap-3">
        <div className="flex items-center gap-2.5 px-3 py-2.5 bg-amber-50/80 dark:bg-amber-900/30 rounded-lg border border-amber-200/50 dark:border-amber-700/30">
          <svg className="w-4 h-4 shrink-0 text-amber-500" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
            <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
            <circle cx="12" cy="12" r="3"/>
          </svg>
          <div className="min-w-0 flex-1">
            <div className="text-[11px] font-medium text-amber-800 dark:text-amber-200 truncate" title={url}>
              {url}
            </div>
            <div className="text-[10px] text-amber-600 dark:text-amber-400 truncate">
              {origin}
            </div>
          </div>
        </div>
        <div className="flex gap-2">
          <button
            onClick={handleBlock}
            className="py-2.5 px-4 rounded-xl bg-red-500 hover:bg-red-600 text-white text-[11.5px] font-semibold transition-colors cursor-pointer active:scale-[0.98]"
          >
            Block
          </button>
          <button
            onClick={handleAllow}
            className="flex-1 py-2.5 rounded-xl bg-emerald-500 hover:bg-emerald-600 text-white text-[11.5px] font-semibold transition-colors cursor-pointer active:scale-[0.98]"
          >
            Allow once
          </button>
          <button
            onClick={handleAlwaysAllow}
            className="flex-1 py-2.5 rounded-xl bg-emerald-600 hover:bg-emerald-700 text-white text-[11.5px] font-semibold transition-colors cursor-pointer active:scale-[0.98]"
          >
            Always allow
          </button>
        </div>
      </div>
    </Popup>
  );
};

export default BlockedPopup;
