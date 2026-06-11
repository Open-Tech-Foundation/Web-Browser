import React, { useState } from 'react';
import Popup, { usePopupRestore } from '../components/Popup';
import { nativeRequest } from '../shared/nativeRequest';

const DownloadRequest = () => {
  const [url, setUrl] = useState('');
  const [origin, setOrigin] = useState('');
  const [name, setName] = useState('');

  usePopupRestore('downloadrequest', (payload) => {
    if (payload.url) setUrl(payload.url);
    if (payload.origin) setOrigin(payload.origin);
    if (payload.name) setName(payload.name);
  });

  const handleAllow = () => {
    window.cefQuery?.({ request: `allow-download:${origin}` });
  };

  const handleAlwaysAllow = () => {
    window.cefQuery?.({ request: `always-allow-download:${origin}` });
  };

  const handleBlock = () => {
    nativeRequest({
      method: 'ui.popup.hide',
      params: { name: 'downloadrequest' },
    }).catch(() => {});
  };

  if (!origin) {
    return (
      <Popup name="downloadrequest" title="Download requested">
        <div className="flex-1 flex items-center justify-center text-slate-400 text-xs">
          No pending download
        </div>
      </Popup>
    );
  }

  return (
    <Popup name="downloadrequest" title="Download requested">
      <div className="flex-1 flex flex-col gap-3">
        <div className="flex items-center gap-2.5 px-3 py-2.5 bg-sky-50/80 dark:bg-sky-900/30 rounded-lg border border-sky-200/50 dark:border-sky-700/30">
          <svg className="w-4 h-4 shrink-0 text-sky-500" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
            <path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4M7 10l5 5 5-5M12 15V3"/>
          </svg>
          <div className="min-w-0 flex-1">
            <div className="text-[11px] font-medium text-sky-800 dark:text-sky-200 truncate" title={name}>
              {name || url.replace(/^.+\//, '')}
            </div>
            <div className="text-[10px] text-sky-600 dark:text-sky-400 truncate">
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

export default DownloadRequest;
