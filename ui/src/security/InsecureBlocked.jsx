import React, { useMemo } from 'react';

const InsecureBlocked = () => {
  const blockedUrl = useMemo(() => {
    const params = new URLSearchParams(window.location.search);
    return params.get('url') || '';
  }, []);

  return (
    <div className="min-h-screen bg-[#020617] text-slate-100 flex items-center justify-center p-6">
      <div className="w-full max-w-2xl rounded-[2rem] border border-orange-500/20 bg-gradient-to-br from-[#0f172a] to-[#020617] shadow-2xl shadow-orange-500/10 p-8 md:p-10">
        <div className="flex items-start gap-4 mb-6">
          <div className="w-14 h-14 rounded-2xl bg-orange-500/15 border border-orange-500/30 flex items-center justify-center shrink-0">
            <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="#fb923c" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
              <path d="M12 9v4" />
              <path d="M12 17h.01" />
              <path d="M10.29 3.86 1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0Z" />
            </svg>
          </div>
          <div>
            <p className="text-xs uppercase tracking-[0.3em] text-orange-400/80 font-bold mb-2">Connection blocked</p>
            <h1 className="text-3xl md:text-4xl font-extrabold tracking-tight mb-3">This site is not secure</h1>
            <p className="text-slate-300 leading-relaxed">
              The browser blocked an insecure HTTP navigation to protect your data.
            </p>
          </div>
        </div>

        {blockedUrl && (
          <div className="mb-6 rounded-2xl border border-white/10 bg-white/5 p-4">
            <div className="text-[11px] uppercase tracking-[0.22em] text-slate-500 font-bold mb-2">Blocked URL</div>
            <div className="font-mono text-sm text-slate-200 break-all">{blockedUrl}</div>
          </div>
        )}

        <div className="space-y-3 text-sm text-slate-400 leading-relaxed">
          <p>
            Insecure pages can expose passwords, messages, and credit card information.
          </p>
        </div>
      </div>
    </div>
  );
};

export default InsecureBlocked;
