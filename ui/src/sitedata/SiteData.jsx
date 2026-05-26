import React, { useEffect, useState } from 'react';
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

const PERMISSION_LABELS = {
  popup: 'Pop-ups',
  downloads: 'Downloads',
  images: 'Images',
  javascript: 'JavaScript',
};

const PERMISSION_SETTING_ORDER = {
  popup: ['ask', 'allow', 'block'],
  downloads: ['ask', 'allow', 'block'],
  images: ['allow', 'block'],
  javascript: ['allow', 'block'],
};

const SiteData = () => {
  const [origin, setOrigin] = useState('');
  const [cookies, setCookies] = useState([]);
  const [storage, setStorage] = useState(null);
  const [permissions, setPermissions] = useState({});
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('');
  const [activeTab, setActiveTab] = useState('cookies');
  const [settingBusy, setSettingBusy] = useState({});
  const [jsJustChanged, setJsJustChanged] = useState(false);
  const [crossOrigins, setCrossOrigins] = useState([]);
  const [selection, setSelection] = useState({
    cookies: true,
    storage: true,
    permissions: true,
  });

  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    setOrigin(params.get('origin') || '');
  }, []);

  const refresh = async () => {
    if (!origin) return;
    const [cookiesRes, storageRes, permsRes, crossRes] = await Promise.all([
      callQuery(`get-cookies-for-site:${origin}`),
      callQuery(`get-storage-for-site:${origin}`),
      callQuery(`get-permissions-for-site:${origin}`),
      callQuery(`get-cross-origin-resources:${origin}`),
    ]);
    if (cookiesRes.ok) {
      try { setCookies(JSON.parse(cookiesRes.response)); } catch (_) {}
    }
    if (storageRes.ok) {
      try { setStorage(JSON.parse(storageRes.response)); }
      catch (_) { setStorage(null); }
    } else {
      setStorage(null);
    }
    if (permsRes.ok) {
      try { setPermissions(JSON.parse(permsRes.response)); } catch (_) {}
    }
    if (crossRes.ok) {
      try { setCrossOrigins(JSON.parse(crossRes.response)); } catch (_) {}
    }
  };

  const setPermission = async (perm, setting) => {
    setSettingBusy((s) => ({ ...s, [perm]: true }));
    const res = await callQuery(`set-permission-for-site:${origin}:${perm}:${setting}`);
    setSettingBusy((s) => ({ ...s, [perm]: false }));
    if (res.ok) {
      setPermissions((p) => ({ ...p, [perm]: setting }));
      if (perm === 'javascript') setJsJustChanged(true);
    }
  };

  useEffect(() => {
    if (origin) refresh();
  }, [origin]);

  const toggle = (key) => setSelection((s) => ({ ...s, [key]: !s[key] }));

  const clearSelected = async () => {
    if (!origin) return;
    const kinds = Object.entries(selection).filter(([, v]) => v).map(([k]) => k);
    if (kinds.length === 0) return;
    setBusy(true);
    setStatus('');
    const results = await Promise.all(
      kinds.map((k) => callQuery(`clear-${k}-for-site:${origin}`))
    );
    setBusy(false);
    const failures = results.filter((r) => !r.ok);
    setStatus(failures.length === 0
      ? `Cleared ${kinds.join(', ')}`
      : `Failed: ${failures.map((r) => r.error).join('; ')}`);
    refresh();
  };

  const anyChecked = selection.cookies || selection.storage || selection.permissions;

  return (
    <div className="h-screen overflow-y-auto bg-slate-50 dark:bg-slate-950 text-slate-900 dark:text-slate-100 p-8">
      <div className="max-w-3xl mx-auto">
        <header className="mb-8">
          <p className="text-xs uppercase tracking-[0.28em] text-orange-500 font-bold mb-2">Site data</p>
          <h1 className="text-3xl font-extrabold tracking-tight mb-1">{origin || '—'}</h1>
          <p className="text-sm text-slate-500 dark:text-slate-400">
            Inspect and clear data this origin has stored.
          </p>
        </header>

        <section className="mb-8">
          <div className="grid grid-cols-1 sm:grid-cols-4 gap-3">
            <button
              onClick={() => setActiveTab('cookies')}
              className={`text-left rounded-lg p-4 border transition-all duration-200 cursor-pointer focus:outline-none focus:ring-2 focus:ring-orange-500/20 ${
                activeTab === 'cookies'
                  ? 'border-orange-500 dark:border-orange-500 bg-orange-500/5 dark:bg-orange-500/5 shadow-sm'
                  : 'bg-white dark:bg-slate-900 border-slate-200 dark:border-slate-800 hover:border-slate-300 dark:hover:border-slate-700'
              }`}
            >
              <div className={`w-9 h-9 rounded-xl flex items-center justify-center mb-2 ${activeTab === 'cookies' ? 'bg-amber-500/10 text-amber-500' : 'bg-slate-100 dark:bg-slate-800 text-slate-400 dark:text-slate-500'}`}>
                <svg className="w-[18px] h-[18px]" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                  <path d="M12 2a10 10 0 1 0 10 10 4 4 0 0 1-5-5 4 4 0 0 1-5-5Z"/>
                  <circle cx="8" cy="14" r="1" fill="currentColor"/>
                  <circle cx="12" cy="16" r="1" fill="currentColor"/>
                  <circle cx="16" cy="11" r="1" fill="currentColor"/>
                </svg>
              </div>
              <div className={`text-xs mb-1 font-semibold ${activeTab === 'cookies' ? 'text-orange-500 dark:text-orange-400' : 'text-slate-500 dark:text-slate-400'}`}>Cookies</div>
              <div className="font-mono text-2xl font-bold">{cookies.length}</div>
            </button>

            <button
              onClick={() => setActiveTab('storage')}
              className={`text-left rounded-lg p-4 border transition-all duration-200 cursor-pointer focus:outline-none focus:ring-2 focus:ring-orange-500/20 ${
                activeTab === 'storage'
                  ? 'border-orange-500 dark:border-orange-500 bg-orange-500/5 dark:bg-orange-500/5 shadow-sm'
                  : 'bg-white dark:bg-slate-900 border-slate-200 dark:border-slate-800 hover:border-slate-300 dark:hover:border-slate-700'
              }`}
            >
              <div className={`w-9 h-9 rounded-xl flex items-center justify-center mb-2 ${activeTab === 'storage' ? 'bg-blue-500/10 text-blue-500' : 'bg-slate-100 dark:bg-slate-800 text-slate-400 dark:text-slate-500'}`}>
                <svg className="w-[18px] h-[18px]" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                  <ellipse cx="12" cy="5" rx="9" ry="3"/>
                  <path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/>
                  <path d="M3 12c0 1.66 4 3 9 3s9-1.34 9-3"/>
                </svg>
              </div>
              <div className={`text-xs mb-1 font-semibold ${activeTab === 'storage' ? 'text-orange-500 dark:text-orange-400' : 'text-slate-500 dark:text-slate-400'}`}>Storage</div>
              <div className="font-mono text-2xl font-bold">
                {storage && typeof storage.usage === 'number' ? formatBytes(storage.usage) : '—'}
              </div>
            </button>

            <button
              onClick={() => setActiveTab('permissions')}
              className={`text-left rounded-lg p-4 border transition-all duration-200 cursor-pointer focus:outline-none focus:ring-2 focus:ring-orange-500/20 ${
                activeTab === 'permissions'
                  ? 'border-orange-500 dark:border-orange-500 bg-orange-500/5 dark:bg-orange-500/5 shadow-sm'
                  : 'bg-white dark:bg-slate-900 border-slate-200 dark:border-slate-800 hover:border-slate-300 dark:hover:border-slate-700'
              }`}
            >
              <div className={`w-9 h-9 rounded-xl flex items-center justify-center mb-2 ${activeTab === 'permissions' ? 'bg-emerald-500/10 text-emerald-500' : 'bg-slate-100 dark:bg-slate-800 text-slate-400 dark:text-slate-500'}`}>
                <svg className="w-[18px] h-[18px]" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                  <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
                </svg>
              </div>
              <div className={`text-xs mb-1 font-semibold ${activeTab === 'permissions' ? 'text-orange-500 dark:text-orange-400' : 'text-slate-500 dark:text-slate-400'}`}>Permissions</div>
              <div className="font-mono text-2xl font-bold">
                {Object.keys(PERMISSION_LABELS).filter(k => {
                  const def = (PERMISSION_SETTING_ORDER[k] || ['ask'])[0];
                  return (permissions[k] || def) !== def;
                }).length}
              </div>
            </button>

            <button
              onClick={() => setActiveTab('external')}
              className={`text-left rounded-lg p-4 border transition-all duration-200 cursor-pointer focus:outline-none focus:ring-2 focus:ring-orange-500/20 ${
                activeTab === 'external'
                  ? 'border-orange-500 dark:border-orange-500 bg-orange-500/5 dark:bg-orange-500/5 shadow-sm'
                  : 'bg-white dark:bg-slate-900 border-slate-200 dark:border-slate-800 hover:border-slate-300 dark:hover:border-slate-700'
              }`}
            >
              <div className={`w-9 h-9 rounded-xl flex items-center justify-center mb-2 ${activeTab === 'external' ? 'bg-purple-500/10 text-purple-500' : 'bg-slate-100 dark:bg-slate-800 text-slate-400 dark:text-slate-500'}`}>
                <svg className="w-[18px] h-[18px]" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
                  <circle cx="12" cy="12" r="10"/>
                  <path d="M2 12h20"/>
                  <path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/>
                </svg>
              </div>
              <div className={`text-xs mb-1 font-semibold ${activeTab === 'external' ? 'text-orange-500 dark:text-orange-400' : 'text-slate-500 dark:text-slate-400'}`}>External</div>
              <div className="font-mono text-2xl font-bold">{crossOrigins.length}</div>
            </button>
          </div>
        </section>

        <section className="mb-8">
          <h2 className="text-sm font-bold uppercase tracking-[0.2em] text-slate-500 dark:text-slate-400 mb-3">Clear</h2>
          <div className="flex flex-wrap items-center gap-3 text-sm">
            <label className="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" checked={selection.cookies} onChange={() => toggle('cookies')} disabled={busy} />
              <span>Cookies</span>
            </label>
            <label className="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" checked={selection.storage} onChange={() => toggle('storage')} disabled={busy} />
              <span>Storage &amp; cache</span>
            </label>
            <label className="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" checked={selection.permissions} onChange={() => toggle('permissions')} disabled={busy} />
              <span>Permissions</span>
            </label>
            <button
              disabled={!origin || busy || !anyChecked}
              onClick={clearSelected}
              className="ml-2 px-4 py-2 rounded-md bg-orange-500 hover:bg-orange-600 text-white disabled:opacity-50 text-sm font-semibold"
            >
              {busy ? 'Clearing…' : 'Clear'}
            </button>
          </div>
          {status && (
            <div className="text-xs text-slate-600 dark:text-slate-300 mt-3">{status}</div>
          )}
        </section>

        {activeTab === 'storage' && (
          <section className="mb-8">
            <h2 className="text-sm font-bold uppercase tracking-[0.2em] text-slate-500 dark:text-slate-400 mb-3">Storage breakdown</h2>
            {!storage || !Array.isArray(storage.usageBreakdown) || storage.usageBreakdown.length === 0 ? (
              <div className="text-sm text-slate-400">No site-scoped storage in use.</div>
            ) : (
              <div className="rounded-lg bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 overflow-hidden">
                <table className="w-full text-xs">
                  <thead className="bg-slate-100 dark:bg-slate-800/60">
                    <tr>
                      <th className="text-left px-3 py-2 font-semibold">Type</th>
                      <th className="text-right px-3 py-2 font-semibold">Usage</th>
                    </tr>
                  </thead>
                  <tbody>
                    {storage.usageBreakdown
                      .filter((row) => row && typeof row.usage === 'number' && row.usage > 0)
                      .sort((a, b) => b.usage - a.usage)
                      .map((row, i) => (
                        <tr key={i} className="border-t border-slate-100 dark:border-slate-800">
                          <td className="px-3 py-2 font-mono">{row.storageType}</td>
                          <td className="px-3 py-2 font-mono text-right">{formatBytes(row.usage)}</td>
                        </tr>
                      ))}
                  </tbody>
                </table>
              </div>
            )}
            <p className="text-[10px] text-slate-400 mt-2">
              HTTP cache is not exposed per-origin by the runtime and is not included here.
            </p>
          </section>
        )}

        {activeTab === 'cookies' && (
          <section className="mb-8">
            <h2 className="text-sm font-bold uppercase tracking-[0.2em] text-slate-500 dark:text-slate-400 mb-3">Cookies</h2>
            {cookies.length === 0 ? (
              <div className="text-sm text-slate-400">No cookies set for this origin.</div>
            ) : (
              <div className="rounded-lg bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 overflow-hidden">
                <table className="w-full text-xs">
                  <thead className="bg-slate-100 dark:bg-slate-800/60">
                    <tr>
                      <th className="text-left px-3 py-2 font-semibold">Name</th>
                      <th className="text-left px-3 py-2 font-semibold">Value</th>
                      <th className="text-left px-3 py-2 font-semibold">Domain</th>
                      <th className="text-left px-3 py-2 font-semibold">Path</th>
                      <th className="text-left px-3 py-2 font-semibold">Flags</th>
                    </tr>
                  </thead>
                  <tbody>
                    {cookies.map((c, i) => (
                      <tr key={i} className="border-t border-slate-100 dark:border-slate-800">
                        <td className="px-3 py-2 font-mono break-all max-w-[160px]">{c.name}</td>
                        <td className="px-3 py-2 font-mono break-all max-w-[200px] text-slate-600 dark:text-slate-400">{c.value}</td>
                        <td className="px-3 py-2 font-mono break-all">{c.domain}</td>
                        <td className="px-3 py-2 font-mono break-all">{c.path}</td>
                        <td className="px-3 py-2 font-mono text-slate-500">
                          {[c.secure && 'Secure', c.httpOnly && 'HttpOnly'].filter(Boolean).join(', ') || '—'}
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
          </section>
        )}

        {activeTab === 'permissions' && (
          <section className="mb-8">
            {(() => {
              const overridden = Object.keys(PERMISSION_LABELS).filter(k => {
                const def = (PERMISSION_SETTING_ORDER[k] || ['ask'])[0];
                return (permissions[k] || def) !== def;
              });
              return (
                <h2 className="text-sm font-bold uppercase tracking-[0.2em] text-slate-500 dark:text-slate-400 mb-3">
                  Permissions
                  <span className="ml-2 text-[10px] font-normal normal-case tracking-normal text-slate-400">
                    {Object.keys(PERMISSION_LABELS).length} available
                    {overridden.length > 0 && ` · ${overridden.length} overridden`}
                  </span>
                </h2>
              );
            })()}
            <div className="rounded-lg bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 overflow-hidden">
              <table className="w-full text-xs">
                <thead className="bg-slate-100 dark:bg-slate-800/60">
                  <tr>
                    <th className="text-left px-3 py-2 font-semibold">Permission</th>
                    <th className="text-right px-3 py-2 font-semibold">Setting</th>
                  </tr>
                </thead>
                <tbody>
                  {Object.entries(PERMISSION_LABELS).map(([key, label], i) => {
                    const order = PERMISSION_SETTING_ORDER[key] || ['ask', 'allow', 'block'];
                    const defaultSetting = order[0];
                    const current = permissions[key] || defaultSetting;
                    const nextIdx = (order.indexOf(current) + 1) % order.length;
                    const next = order[nextIdx];
                    return (
                      <tr key={key} className="border-t border-slate-100 dark:border-slate-800">
                        <td className="px-3 py-2.5">{label}</td>
                        <td className="px-3 py-2.5 text-right">
                          <button
                            disabled={settingBusy[key]}
                            onClick={() => setPermission(key, next)}
                            className={`inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-semibold border transition-all duration-200 cursor-pointer focus:outline-none focus:ring-2 focus:ring-orange-500/20 disabled:opacity-50 ${
                              current === 'allow'
                                ? 'bg-emerald-50 dark:bg-emerald-900/20 text-emerald-700 dark:text-emerald-300 border-emerald-200 dark:border-emerald-800'
                                : current === 'block'
                                  ? 'bg-red-50 dark:bg-red-900/20 text-red-700 dark:text-red-300 border-red-200 dark:border-red-800'
                                  : 'bg-slate-50 dark:bg-slate-800 text-slate-500 dark:text-slate-400 border-slate-200 dark:border-slate-700'
                            }`}
                          >
                            {current === 'allow' ? 'Allow' : current === 'block' ? 'Block' : 'Ask'}
                          </button>
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
            <p className="text-[10px] text-slate-400 mt-2">
              Click a setting to cycle: Ask → Allow → Block → Ask.
            </p>
            {jsJustChanged && (
              <div className="mt-3 p-2 rounded-md bg-amber-50 dark:bg-amber-900/10 border border-amber-200 dark:border-amber-800/40 text-[10px] text-amber-700 dark:text-amber-300 leading-relaxed">
                JavaScript changes apply to newly opened tabs. Close and reopen the tab for the
                change to take effect.
              </div>
            )}
          </section>
        )}

        {activeTab === 'external' && (
          <section className="mb-8">
            <h2 className="text-sm font-bold uppercase tracking-[0.2em] text-slate-500 dark:text-slate-400 mb-3">
              Cross-origin resources
            </h2>
            {crossOrigins.length === 0 ? (
              <div className="text-sm text-slate-400">No external origins detected.</div>
            ) : (
              <div className="rounded-lg bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 overflow-hidden">
                <table className="w-full text-xs">
                  <thead className="bg-slate-100 dark:bg-slate-800/60">
                    <tr>
                      <th className="text-left px-3 py-2 font-semibold">Origin</th>
                      <th className="text-right px-3 py-2 font-semibold"></th>
                    </tr>
                  </thead>
                  <tbody>
                    {crossOrigins.map((res, i) => (
                      <tr key={i} className="border-t border-slate-100 dark:border-slate-800">
                        <td className="px-3 py-2.5 font-mono">{res}</td>
                        <td className="px-3 py-2.5 text-right">
                          <button
                            onClick={() => window.cefQuery?.({ request: `open-site-data-page:${res}` })}
                            className="px-2 py-1 rounded text-[10px] font-medium bg-slate-100 dark:bg-slate-800 text-slate-600 dark:text-slate-300 hover:bg-slate-200 dark:hover:bg-slate-700 border border-slate-200 dark:border-slate-700 cursor-pointer"
                          >
                            Configure
                          </button>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
            <p className="text-[10px] text-slate-400 mt-2">
              External origins that this site has loaded resources from. Click <strong>Configure</strong>
              to manage permissions for that origin.
            </p>
          </section>
        )}
      </div>
    </div>
  );
};

export default SiteData;
