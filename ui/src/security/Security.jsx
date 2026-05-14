import React, { useEffect, useState } from 'react';

const Card = ({ title, description, enabled }) => (
  <div className="p-8 rounded-3xl bg-white/5 border border-white/10 flex items-center justify-between gap-6">
    <div className="min-w-0">
      <h3 className="text-lg font-bold text-white mb-2">{title}</h3>
      <p className="text-sm text-slate-400 leading-relaxed">{description}</p>
    </div>
    <div className={`shrink-0 w-12 h-6 rounded-full border flex items-center px-1 transition-colors ${
      enabled ? 'bg-green-500/15 border-green-500/40' : 'bg-slate-800 border-slate-700'
    }`}>
      <div className={`w-4 h-4 rounded-full transition-transform ${enabled ? 'translate-x-5 bg-green-400' : 'translate-x-0 bg-slate-500'}`} />
    </div>
  </div>
);

const Security = () => {
  const [showAdvanced, setShowAdvanced] = useState(false);

  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    setShowAdvanced(params.get('advanced') === '1');
  }, []);

  const openSecuritySettings = () => {
    window.cefQuery?.({ request: 'navigate-current:browser://settings?tab=security' });
  };

  return (
    <div className="min-h-screen bg-[#020617] text-slate-100 p-8 md:p-12">
      <div className="max-w-4xl mx-auto">
        <header className="mb-10">
          <p className="text-xs uppercase tracking-[0.28em] text-orange-400/80 font-bold mb-3">Browser Security</p>
          <h1 className="text-4xl font-extrabold tracking-tight mb-4">Security overview</h1>
          <p className="text-slate-400 max-w-2xl">
            Control connection protection, mixed content blocking, and other security settings for this browser profile.
          </p>
        </header>

        <div className="space-y-4">
          <Card
            title="HTTPS-Only Mode"
            description="Upgrade navigations to HTTPS whenever possible and warn before insecure loads."
            enabled
          />
          <Card
            title="Block Insecure Content"
            description="Prevent pages from loading insecure subresources over HTTP when the main page is secure."
            enabled
          />
          <Card
            title="Enhanced Safe Browsing"
            description="Protect against dangerous websites, downloads, and extensions."
            enabled={showAdvanced}
          />
        </div>

        <div className="mt-8 flex flex-wrap gap-3">
          <button
            onClick={openSecuritySettings}
            className="px-4 py-2 rounded-xl bg-orange-500 text-white font-semibold hover:bg-orange-600 transition-colors"
          >
            Open security settings
          </button>
          <button
            onClick={() => window.history.back()}
            className="px-4 py-2 rounded-xl bg-white/5 text-slate-200 font-semibold hover:bg-white/10 transition-colors"
          >
            Back
          </button>
        </div>
      </div>
    </div>
  );
};

export default Security;
