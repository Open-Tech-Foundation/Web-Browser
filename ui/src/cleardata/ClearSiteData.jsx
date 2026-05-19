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

/* ── Data category row ─────────────────────────────────────────────── */
const CategoryRow = ({ label, detail, icon, color, checked, onToggle, disabled }) => {
  // Color map for the icon pill and checkbox
  const colors = {
    amber:   { bg: 'bg-amber-500/10',   text: 'text-amber-500',   check: 'bg-amber-500 border-amber-500',   shadow: 'shadow-amber-500/15' },
    blue:    { bg: 'bg-blue-500/10',     text: 'text-blue-500',    check: 'bg-blue-500 border-blue-500',     shadow: 'shadow-blue-500/15' },
    emerald: { bg: 'bg-emerald-500/10',  text: 'text-emerald-500', check: 'bg-emerald-500 border-emerald-500', shadow: 'shadow-emerald-500/15' },
  };
  const c = colors[color] || colors.amber;

  return (
    <button
      type="button"
      onClick={onToggle}
      disabled={disabled}
      className={`w-full flex items-center gap-3 px-3 py-3 rounded-xl border transition-all duration-150 cursor-pointer text-left group
        ${checked
          ? `border-slate-200/80 dark:border-slate-700/50 bg-slate-50/70 dark:bg-slate-800/35`
          : `border-transparent hover:bg-slate-50/60 dark:hover:bg-slate-800/25`
        }`}
    >
      {/* Icon */}
      <div className={`w-9 h-9 rounded-xl ${c.bg} ${c.text} flex items-center justify-center shrink-0`}>
        {icon}
      </div>
      {/* Label + detail */}
      <div className="flex-1 min-w-0">
        <div className="text-[12.5px] font-medium text-slate-800 dark:text-slate-200">{label}</div>
        <div className="text-[10.5px] text-slate-400 dark:text-slate-500 mt-0.5 truncate">{detail}</div>
      </div>
      {/* Checkbox */}
      <div
        className={`w-[18px] h-[18px] rounded-md border-[1.5px] flex items-center justify-center transition-all duration-150 shrink-0
          ${checked
            ? `${c.check} text-white shadow-sm ${c.shadow}`
            : `border-slate-300 dark:border-slate-600 bg-transparent`
          }`}
      >
        {checked && (
          <svg className="w-2.5 h-2.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="4.5" strokeLinecap="round" strokeLinejoin="round">
            <polyline points="20 6 9 17 4 12" />
          </svg>
        )}
      </div>
    </button>
  );
};

/* ── Main component ─────────────────────────────────────────────── */
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
      <div className="flex-1 flex flex-col gap-3">
        {/* Domain indicator */}
        <div className="flex items-center gap-2.5 px-3 py-2.5 bg-slate-50/80 dark:bg-slate-800/40 rounded-lg overflow-hidden">
          <svg className="w-3.5 h-3.5 text-slate-400 dark:text-slate-500 shrink-0" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
            <circle cx="12" cy="12" r="10"/>
            <path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/>
            <path d="M2 12h20"/>
          </svg>
          <span className="text-[11px] font-mono text-slate-600 dark:text-slate-400 truncate select-all" title={origin}>
            {origin || 'No site selected'}
          </span>
        </div>

        {/* Category rows */}
        <div className="flex flex-col gap-1">
          <CategoryRow
            label="Cookies"
            detail={cookieCount === null ? 'Calculating…' : `${cookieCount} cookies stored`}
            color="amber"
            checked={selection.cookies}
            onToggle={() => !busy && toggle('cookies')}
            disabled={busy}
            icon={
              <svg className="w-[18px] h-[18px]" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                <path d="M12 2a10 10 0 1 0 10 10 4 4 0 0 1-5-5 4 4 0 0 1-5-5Z"/>
                <circle cx="8" cy="14" r="1" fill="currentColor"/>
                <circle cx="12" cy="16" r="1" fill="currentColor"/>
                <circle cx="16" cy="11" r="1" fill="currentColor"/>
              </svg>
            }
          />
          <CategoryRow
            label="Storage & Cache"
            detail={storageBytes === null ? 'Calculating…' : `${formatBytes(storageBytes)} used`}
            color="blue"
            checked={selection.storage}
            onToggle={() => !busy && toggle('storage')}
            disabled={busy}
            icon={
              <svg className="w-[18px] h-[18px]" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                <ellipse cx="12" cy="5" rx="9" ry="3"/>
                <path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/>
                <path d="M3 12c0 1.66 4 3 9 3s9-1.34 9-3"/>
              </svg>
            }
          />
          <CategoryRow
            label="Permissions"
            detail="Reset site access settings"
            color="emerald"
            checked={selection.permissions}
            onToggle={() => !busy && toggle('permissions')}
            disabled={busy}
            icon={
              <svg className="w-[18px] h-[18px]" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
              </svg>
            }
          />
        </div>

        {/* Actions */}
        <div className="flex flex-col gap-2 mt-auto">
          <button
            disabled={!origin || busy || !anyChecked}
            onClick={clearSelected}
            className={`w-full py-3 rounded-xl text-white disabled:opacity-40 font-semibold text-[12.5px] tracking-wide flex items-center justify-center gap-2 transition-all duration-200 cursor-pointer active:scale-[0.98] ${
              done
                ? 'bg-emerald-500 hover:bg-emerald-600'
                : 'bg-brand-orange hover:bg-brand-orange/90'
            }`}
          >
            {busy ? (<><Spinner /> Clearing…</>)
              : done ? (<><Check /> Cleared</>)
              : 'Clear Selected Data'}
          </button>
          <button
            disabled={!origin}
            onClick={openDetails}
            className="w-full py-1.5 text-[11.5px] font-medium text-brand-orange hover:underline disabled:opacity-40 text-center transition-colors duration-150 cursor-pointer flex items-center justify-center gap-1 group"
          >
            <span>View detailed site data</span>
            <span className="transform group-hover:translate-x-0.5 transition-transform duration-150">→</span>
          </button>
        </div>
      </div>
    </Popup>
  );
};

export default ClearSiteData;
