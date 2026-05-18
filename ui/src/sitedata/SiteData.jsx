import React, { useEffect, useState } from 'react';

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
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('');
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
    const { ok, response } = await callQuery(`get-cookies-for-site:${origin}`);
    if (ok) {
      try { setCookies(JSON.parse(response)); } catch (_) {}
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
    <div className="min-h-screen bg-slate-50 dark:bg-slate-950 text-slate-900 dark:text-slate-100 p-8">
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
            <div className="rounded-lg bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 p-4">
              <div className="text-xs text-slate-500 dark:text-slate-400 mb-1">Cookies</div>
              <div className="font-mono text-2xl">{cookies.length}</div>
            </div>
            <div className="rounded-lg bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 p-4" title="Storage usage breakdown not exposed yet — next iteration.">
              <div className="text-xs text-slate-500 dark:text-slate-400 mb-1">Storage</div>
              <div className="font-mono text-2xl text-slate-400">—</div>
            </div>
            <div className="rounded-lg bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 p-4" title="Per-site permission enumeration not exposed yet — next iteration.">
              <div className="text-xs text-slate-500 dark:text-slate-400 mb-1">Permissions</div>
              <div className="font-mono text-2xl text-slate-400">—</div>
            </div>
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

        <section>
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
      </div>
    </div>
  );
};

export default SiteData;
