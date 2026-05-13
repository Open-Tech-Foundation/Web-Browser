import React, { useState, useEffect } from 'react';

const Security = () => {
  const [httpsOnly, setHttpsOnly] = useState(false);
  const [blockInsecure, setBlockInsecure] = useState(true);
  const [safeBrowsing, setSafeBrowsing] = useState(true);

  useEffect(() => {
    // Sync with settings if needed
    if (window.cefQuery) {
      window.cefQuery({
        request: 'get-settings',
        onSuccess: (json) => {
          try {
            const s = JSON.parse(json);
            if (s.httpsOnly !== undefined) setHttpsOnly(s.httpsOnly);
            if (s.blockInsecure !== undefined) setBlockInsecure(s.blockInsecure);
          } catch (e) {}
        }
      });
    }
  }, []);

  const updateSetting = (key, value) => {
    if (window.cefQuery) {
      window.cefQuery({
        request: `update-setting:${key}:${value}`,
        onSuccess: () => {
          // Success feedback if needed
        }
      });
    }
  };

  const toggleHttpsOnly = () => {
    const next = !httpsOnly;
    setHttpsOnly(next);
    updateSetting('httpsOnly', next);
  };

  const toggleBlockInsecure = () => {
    const next = !blockInsecure;
    setBlockInsecure(next);
    updateSetting('blockInsecure', next);
  };

  return (
    <div className="min-h-screen bg-[#020617] text-slate-200 font-sans selection:bg-orange-500/30">
      <div className="flex h-screen overflow-hidden">
        {/* Sidebar */}
        <aside className="w-72 bg-[#020617] border-r border-white/5 flex flex-col p-8 shrink-0">
          <div className="flex items-center gap-3 mb-12">
            <div className="w-10 h-10 bg-orange-500 rounded-xl flex items-center justify-center text-white shadow-lg shadow-orange-500/20">
              <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
              </svg>
            </div>
            <h1 className="text-xl font-bold tracking-tight">Security</h1>
          </div>
          
          <nav className="space-y-1">
            <button className="w-full flex items-center gap-3 px-4 py-3 rounded-xl bg-white/5 text-orange-500 font-bold text-sm transition-all border border-orange-500/20">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/>
              </svg>
              Protection
            </button>
            <button className="w-full flex items-center gap-3 px-4 py-3 rounded-xl hover:bg-white/5 text-slate-400 font-medium text-sm transition-all">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
              </svg>
              Certificates
            </button>
          </nav>
        </aside>

        {/* Main Content */}
        <main className="flex-1 overflow-y-auto p-12 lg:p-20">
          <div className="max-w-3xl mx-auto animate-in fade-in slide-in-from-bottom-8 duration-700">
            <header className="mb-12">
              <h2 className="text-3xl font-extrabold text-white mb-4">Security Center</h2>
              <p className="text-slate-400 leading-relaxed text-lg">
                Manage your browser's security settings and protection mechanisms. 
                OTF Browser prioritizes your safety by enforcing strict navigation rules.
              </p>
            </header>

            <div className="space-y-6">
              {/* HTTPS-Only Mode */}
              <section className="bg-white/5 border border-white/5 rounded-3xl p-8 hover:bg-white/[0.07] transition-all">
                <div className="flex items-start justify-between gap-8">
                  <div className="flex-1">
                    <h3 className="text-lg font-bold text-white mb-2 flex items-center gap-2">
                      HTTPS-Only Mode
                      <span className="text-[10px] uppercase tracking-widest bg-orange-500/20 text-orange-400 px-2 py-0.5 rounded-full font-black">Recommended</span>
                    </h3>
                    <p className="text-slate-400 text-sm leading-relaxed mb-4">
                      Upgrade all navigations to HTTPS. If a site doesn't support a secure connection, 
                      OTF Browser will show a warning before connecting.
                    </p>
                  </div>
                  <Toggle active={httpsOnly} onClick={toggleHttpsOnly} />
                </div>
              </section>

              {/* Insecure Content Blocking */}
              <section className="bg-white/5 border border-white/5 rounded-3xl p-8 hover:bg-white/[0.07] transition-all">
                <div className="flex items-start justify-between gap-8">
                  <div className="flex-1">
                    <h3 className="text-lg font-bold text-white mb-2">Block Insecure Content</h3>
                    <p className="text-slate-400 text-sm leading-relaxed mb-4">
                      Prevent pages from loading insecure elements (scripts, images) over HTTP when the main page is secure.
                      This prevents 'Mixed Content' vulnerabilities.
                    </p>
                  </div>
                  <Toggle active={blockInsecure} onClick={toggleBlockInsecure} />
                </div>
              </section>

              {/* Safe Browsing */}
              <section className="bg-white/5 border border-white/5 rounded-3xl p-8 opacity-60">
                <div className="flex items-start justify-between gap-8">
                  <div className="flex-1">
                    <h3 className="text-lg font-bold text-white mb-2 flex items-center gap-2">
                      Enhanced Safe Browsing
                      <span className="text-[10px] uppercase tracking-widest bg-slate-500/20 text-slate-500 px-2 py-0.5 rounded-full font-black">Managed</span>
                    </h3>
                    <p className="text-slate-400 text-sm leading-relaxed">
                      Proactively protects you against dangerous websites, downloads, and extensions.
                      This feature is enabled by default for all OTF users.
                    </p>
                  </div>
                  <div className="shrink-0 w-12 h-6 rounded-full bg-green-500/20 flex items-center justify-center">
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#10b981" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                      <polyline points="20 6 9 17 4 12"/>
                    </svg>
                  </div>
                </div>
              </section>

              {/* Info Card */}
              <div className="mt-12 p-6 rounded-2xl bg-orange-500/10 border border-orange-500/20 flex gap-4">
                <div className="text-orange-500 shrink-0 mt-1">
                  <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                    <circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/>
                  </svg>
                </div>
                <p className="text-sm text-orange-400/90 leading-relaxed font-medium">
                  Your security settings are synced across your local browsing profile. 
                  Some restrictions may be enforced by your system administrator to ensure maximum protection.
                </p>
              </div>
            </div>
          </div>
        </main>
      </div>
    </div>
  );
};

const Toggle = ({ active, onClick }) => (
  <button 
    onClick={onClick}
    className={`shrink-0 w-12 h-6 rounded-full transition-all duration-300 relative border-2 ${
      active ? 'bg-orange-500 border-orange-500' : 'bg-slate-800 border-slate-700'
    }`}
  >
    <div className={`absolute top-1 w-3 h-3 rounded-full bg-white transition-all duration-300 shadow-sm ${
      active ? 'left-7' : 'left-1'
    }`} />
  </button>
);

export default Security;
