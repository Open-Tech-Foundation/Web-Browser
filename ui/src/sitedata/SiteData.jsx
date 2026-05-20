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

const SiteData = () => {
  const [origin, setOrigin] = useState('');
  const [cookies, setCookies] = useState([]);
  const [storage, setStorage] = useState(null);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('');
  const [activeTab, setActiveTab] = useState('cookies');
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
    const [cookiesRes, storageRes] = await Promise.all([
      callQuery(`get-cookies-for-site:${origin}`),
      callQuery(`get-storage-for-site:${origin}`),
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
          <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
            <button
              onClick={() => setActiveTab('cookies')}
              className={`text-left rounded-lg p-4 border transition-all duration-200 cursor-pointer focus:outline-none focus:ring-2 focus:ring-orange-500/20 ${
                activeTab === 'cookies'
                  ? 'border-orange-500 dark:border-orange-500 bg-orange-500/5 dark:bg-orange-500/5 shadow-sm'
                  : 'bg-white dark:bg-slate-900 border-slate-200 dark:border-slate-800 hover:border-slate-300 dark:hover:border-slate-700'
              }`}
            >
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
              <div className={`text-xs mb-1 font-semibold ${activeTab === 'permissions' ? 'text-orange-500 dark:text-orange-400' : 'text-slate-500 dark:text-slate-400'}`}>Permissions</div>
              <div className="font-mono text-2xl font-bold">—</div>
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
                      <th className="text-left px-3 py-2 font-semibold">Domain</th>
                      <th className="text-left px-3 py-2 font-semibold">Path</th>
                      <th className="text-left px-3 py-2 font-semibold">Flags</th>
                    </tr>
                  </thead>
                  <tbody>
                    {cookies.map((c, i) => (
                      <tr key={i} className="border-t border-slate-100 dark:border-slate-800">
                        <td className="px-3 py-2 font-mono break-all">{c.name}</td>
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
            <h2 className="text-sm font-bold uppercase tracking-[0.2em] text-slate-500 dark:text-slate-400 mb-3">Permissions</h2>
            <div className="rounded-lg bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 p-6">
              <div className="text-sm text-slate-400 mb-3">Per-site permission details are not yet exposed.</div>
              <div className="text-xs text-slate-500 leading-relaxed">
                Standard permissions (such as camera, microphone, notifications, and location) are set to their default settings or managed globally. Granular site-specific controls will be available in a future update.
              </div>
            </div>
          </section>
        )}
      </div>
    </div>
  );
};

export default SiteData;
