import React, { useEffect, useState } from 'react';
import Popup, { usePopupRestore } from '../components/Popup';
import { formatBytes } from '../shared/format';

const callQuery = (request) => new Promise((resolve) => {
  if (!window.cefQuery) {
    resolve({ ok: false, error: 'cefQuery unavailable' });
    return;
  }
  window.cefQuery({
    request,
    onSuccess: (response) => resolve({ ok: true, response }),
    onFailure: (code, msg) => resolve({ ok: false, error: msg || `code ${code}` }),
  });
});

const Spinner = () => (
  <svg className="animate-spin" width="14" height="14" viewBox="0 0 24 24" fill="none">
    <circle cx="12" cy="12" r="10" stroke="currentColor" strokeOpacity="0.25" strokeWidth="4" />
    <path d="M22 12a10 10 0 0 1-10 10" stroke="currentColor" strokeWidth="4" strokeLinecap="round" />
  </svg>
);

const Check = () => (
  <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
    <polyline points="20 6 9 17 4 12" />
  </svg>
);

const defaultSelection = () => ({ cookies: true, storage: true, permissions: true });

const ClearSiteData = () => {
  const [origin, setOrigin] = useState('');
  // 'idle' | 'running' | 'done'
  const [phase, setPhase] = useState('idle');
  const [cookieCount, setCookieCount] = useState(null);
  const [storageBytes, setStorageBytes] = useState(null);
  const [selection, setSelection] = useState(defaultSelection);

  const refreshCounts = async (forOrigin) => {
    const target = forOrigin || origin;
    if (!target) return;
    // Run both queries in parallel — they're independent.
    const [cookies, storage] = await Promise.all([
      callQuery(`get-cookies-for-site:${target}`),
      callQuery(`get-storage-for-site:${target}`),
    ]);
    if (cookies.ok) {
      try { setCookieCount(JSON.parse(cookies.response).length); } catch (_) {}
    }
    if (storage.ok) {
      try {
        const parsed = JSON.parse(storage.response);
        setStorageBytes(typeof parsed.usage === 'number' ? parsed.usage : null);
      } catch (_) { setStorageBytes(null); }
    } else {
      setStorageBytes(null);
    }
  };

  // Every popup-restore push from C++ — fired on each Show() — resets
  // transient state so a reopened popup never displays the previous
  // "Cleared" badge.
  usePopupRestore('cleardata', (payload) => {
    if (payload.origin) {
      setOrigin(payload.origin);
      setPhase('idle');
      setSelection(defaultSelection());
      setCookieCount(null);
      setStorageBytes(null);
      refreshCounts(payload.origin);
    }
  });

  const toggle = (key) => setSelection((s) => ({ ...s, [key]: !s[key] }));

  const clearSelected = async () => {
    if (!origin) return;
    const kinds = Object.entries(selection).filter(([, v]) => v).map(([k]) => k);
    if (kinds.length === 0) return;
    setPhase('running');
    await Promise.all(
      kinds.map((k) => callQuery(`clear-${k}-for-site:${origin}`))
    );
    setPhase('done');
    refreshCounts(origin);
  };

  const openDetails = () => {
    if (!origin) return;
    window.cefQuery?.({ request: `open-site-data-page:${origin}` });
  };

  const anyChecked = selection.cookies || selection.storage || selection.permissions;
  const busy = phase === 'running';
  const done = phase === 'done';

  return (
    <Popup name="cleardata" title="Clear site data">
      <div className="text-xs text-slate-500 dark:text-slate-400 break-all">
        {origin || 'No site selected'}
      </div>

      <div className="grid grid-cols-3 gap-2 text-[11px]">
        <div className="px-2 py-1.5">
          <div className="text-slate-500 dark:text-slate-400">Cookies</div>
          <div className="font-mono text-sm text-slate-900 dark:text-slate-100">
            {cookieCount === null ? '…' : cookieCount}
          </div>
        </div>
        <div className="px-2 py-1.5">
          <div className="text-slate-500 dark:text-slate-400">Storage</div>
          <div className="font-mono text-sm text-slate-900 dark:text-slate-100">
            {storageBytes === null ? '…' : formatBytes(storageBytes)}
          </div>
        </div>
        <div className="px-2 py-1.5" title="Detailed counts coming next iteration.">
          <div className="text-slate-500 dark:text-slate-400">Perms</div>
          <div className="font-mono text-sm text-slate-400">—</div>
        </div>
      </div>

      <div className="flex flex-col gap-2">
        <label className="flex items-center gap-2 px-3 py-2 rounded-md cursor-pointer">
          <input
            type="checkbox"
            checked={selection.cookies}
            onChange={() => toggle('cookies')}
            disabled={busy}
          />
          <span>Cookies</span>
        </label>
        <label className="flex items-center gap-2 px-3 py-2 rounded-md cursor-pointer">
          <input
            type="checkbox"
            checked={selection.storage}
            onChange={() => toggle('storage')}
            disabled={busy}
          />
          <span>Storage &amp; cache</span>
        </label>
        <label className="flex items-center gap-2 px-3 py-2 rounded-md cursor-pointer">
          <input
            type="checkbox"
            checked={selection.permissions}
            onChange={() => toggle('permissions')}
            disabled={busy}
          />
          <span>Permissions</span>
        </label>
        <button
          disabled={!origin || busy || !anyChecked}
          onClick={clearSelected}
          className={`px-3 py-2 rounded-md text-white disabled:opacity-50 font-semibold flex items-center justify-center gap-2 ${
            done ? 'bg-green-500 hover:bg-green-600' : 'bg-orange-500 hover:bg-orange-600'
          }`}
        >
          {busy ? (<><Spinner /> Clearing…</>)
            : done ? (<><Check /> Cleared</>)
            : 'Clear'}
        </button>
        <button
          disabled={!origin}
          onClick={openDetails}
          className="px-3 py-2 rounded-md text-xs text-orange-600 hover:underline disabled:opacity-50 text-left"
        >
          Open site data →
        </button>
      </div>
    </Popup>
  );
};

export default ClearSiteData;
