import React, { useEffect, useMemo, useState } from 'react';
import { nativeRequest } from '../shared/nativeRequest';
import '../styles/App.css';

const getHostname = (url) => {
  try {
    return new URL(url).hostname;
  } catch (_) {
    return url || 'Internal page';
  }
};

const isPlaceholderTab = (tab) => {
  const haystack = `${tab?.url || ''} ${tab?.title || ''}`.toLowerCase();
  return haystack.includes('split-placeholder') ||
    haystack.includes('splitplaceholder.html') ||
    haystack.includes('add a tab to split view');
};

const SplitIcon = () => (
  <svg className="h-7 w-7" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.3" strokeLinecap="round" strokeLinejoin="round">
    <rect x="3" y="4" width="18" height="16" rx="2" />
    <path d="M12 4v16" />
    <path d="M7 9h2" />
    <path d="M15 9h2" />
  </svg>
);

const TabIcon = ({ tab }) => {
  const [imgErrored, setImgErrored] = useState(false);
  if (tab?.favicon && !imgErrored) {
    return <img src={tab.favicon} alt="" className="h-4 w-4 rounded-sm object-contain" onError={() => setImgErrored(true)} />;
  }
  return (
    <span className="flex h-7 w-7 shrink-0 items-center justify-center rounded-lg bg-slate-100 text-slate-400 dark:bg-white/5 dark:text-slate-500">
      <svg className="h-3.5 w-3.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
        <path d="M14 2v6h6" />
      </svg>
    </span>
  );
};

const SplitPlaceholder = () => {
  const [tabs, setTabs] = useState([]);
  const [splitState, setSplitState] = useState({ enabled: false, leftTabId: -1, rightTabId: -1 });
  const [busyTabId, setBusyTabId] = useState(null);
  const [error, setError] = useState('');

  const refresh = async () => {
    try {
      const [tabsList, split] = await Promise.all([
        nativeRequest({ method: 'tabs.list' }),
        nativeRequest({ method: 'tabs.splitState' }),
      ]);
      setTabs(Array.isArray(tabsList) ? tabsList : []);
      setSplitState(split || { enabled: false, leftTabId: -1, rightTabId: -1 });
      setError('');
    } catch (err) {
      setError(err?.message || 'Could not load tabs');
    }
  };

  useEffect(() => {
    refresh();
    const id = window.setInterval(refresh, 1500);
    const onFocus = () => refresh();
    window.addEventListener('focus', onFocus);
    return () => {
      window.clearInterval(id);
      window.removeEventListener('focus', onFocus);
    };
  }, []);

  const candidates = useMemo(() => {
    const leftId = Number(splitState?.leftTabId ?? -1);
    const rightId = Number(splitState?.rightTabId ?? -1);
    return tabs.filter((tab) =>
      tab &&
      !tab.pinned &&
      tab.id !== leftId &&
      tab.id !== rightId &&
      !isPlaceholderTab(tab)
    );
  }, [splitState, tabs]);

  const addToSplit = async (tabId) => {
    setBusyTabId(tabId);
    setError('');
    try {
      await nativeRequest({ method: 'split.addTab', params: { tabId } });
    } catch (err) {
      setError(err?.message || 'Could not add tab to split view');
      setBusyTabId(null);
      return;
    }
    setBusyTabId(null);
    refresh();
  };

  return (
    <div className="min-h-screen bg-[radial-gradient(circle_at_top_left,rgba(249,115,22,0.14),transparent_34%),linear-gradient(135deg,#f8fafc,#eef2f7)] px-6 py-7 text-slate-950 dark:bg-[radial-gradient(circle_at_top_left,rgba(249,115,22,0.16),transparent_34%),linear-gradient(135deg,#05070d,#111827)] dark:text-slate-100">
      <div className="mx-auto flex h-full max-w-2xl flex-col">
        <div className="mb-5 flex items-start gap-4">
          <div className="flex h-14 w-14 shrink-0 items-center justify-center rounded-2xl border border-orange-500/30 bg-orange-500/10 text-orange-500 shadow-[0_18px_45px_rgba(249,115,22,0.16)]">
            <SplitIcon />
          </div>
          <div className="min-w-0">
            <h1 className="text-2xl font-black tracking-tight">Choose a tab for this pane</h1>
            <p className="mt-1 max-w-xl text-sm leading-6 text-slate-600 dark:text-slate-400">
              Select an existing workspace tab below. The selected tab will replace this placeholder in split view.
            </p>
          </div>
        </div>

        {error && (
          <div className="mb-4 rounded-2xl border border-red-500/30 bg-red-500/10 px-4 py-3 text-sm font-semibold text-red-700 dark:text-red-300">
            {error}
          </div>
        )}

        <div className="flex-1 overflow-hidden rounded-3xl border border-white/70 bg-white/78 p-2 shadow-[0_24px_80px_rgba(15,23,42,0.16)] backdrop-blur dark:border-white/10 dark:bg-slate-950/68 dark:shadow-[0_24px_90px_rgba(0,0,0,0.4)]">
          {candidates.length === 0 ? (
            <div className="flex min-h-[280px] flex-col items-center justify-center px-8 text-center">
              <p className="text-base font-bold text-slate-800 dark:text-slate-200">No other tabs available</p>
              <p className="mt-2 max-w-sm text-sm leading-6 text-slate-500 dark:text-slate-400">
                Open another tab in this workspace, then return here to add it to split view.
              </p>
              <button
                type="button"
                onClick={refresh}
                className="mt-5 rounded-xl border border-slate-200 bg-white px-4 py-2 text-sm font-bold text-slate-700 shadow-sm transition hover:border-orange-400 hover:text-orange-600 dark:border-white/10 dark:bg-white/5 dark:text-slate-200"
              >
                Refresh tabs
              </button>
            </div>
          ) : (
            <div className="max-h-[min(62vh,520px)] overflow-y-auto pr-1">
              {candidates.map((tab) => (
                <button
                  key={tab.id}
                  type="button"
                  onClick={() => addToSplit(tab.id)}
                  disabled={busyTabId !== null}
                  className="group mb-2 flex w-full items-center gap-3 rounded-2xl border border-transparent bg-white/72 px-3 py-3 text-left transition hover:border-orange-500/35 hover:bg-orange-50/80 hover:shadow-[0_14px_30px_rgba(249,115,22,0.10)] disabled:cursor-wait disabled:opacity-70 dark:bg-white/[0.045] dark:hover:bg-orange-500/10"
                >
                  <TabIcon tab={tab} />
                  <span className="min-w-0 flex-1">
                    <span className="block truncate text-sm font-extrabold text-slate-900 dark:text-slate-100">
                      {tab.title || tab.url || 'Untitled tab'}
                    </span>
                    <span className="mt-0.5 block truncate text-xs font-medium text-slate-500 dark:text-slate-400">
                      {getHostname(tab.url)}
                    </span>
                  </span>
                  <span className="rounded-xl bg-slate-950 px-3 py-1.5 text-xs font-black text-white opacity-0 transition group-hover:opacity-100 dark:bg-orange-500">
                    {busyTabId === tab.id ? 'Adding...' : 'Add'}
                  </span>
                </button>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

export default SplitPlaceholder;
