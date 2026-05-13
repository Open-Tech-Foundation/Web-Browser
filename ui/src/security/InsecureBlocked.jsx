import React, { useEffect, useState } from 'react';

const InsecureBlocked = () => {
  const [url, setUrl] = useState('');

  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    setUrl(params.get('url') || '');
  }, []);

  const handleGoBack = () => {
    if (window.cefQuery) {
      window.cefQuery({ request: 'back-current' });
    } else {
      window.history.back();
    }
  };

  const handleProceed = () => {
    // This would require a bypass mechanism in the backend
    // For now, we just show it's blocked
    alert('Security bypass is restricted in this version of the browser.');
  };

  return (
    <div className="min-h-screen bg-[#020617] text-slate-100 flex items-center justify-center p-6 selection:bg-orange-500/30 font-sans relative overflow-hidden">
      {/* Background Ambient Effects */}
      <div className="absolute top-1/4 left-1/4 w-[500px] h-[500px] bg-orange-600/10 blur-[120px] rounded-full pointer-events-none animate-pulse"></div>
      <div className="absolute bottom-1/4 right-1/4 w-[500px] h-[500px] bg-red-600/5 blur-[120px] rounded-full pointer-events-none"></div>

      <div className="max-w-xl w-full relative z-10 animate-in fade-in zoom-in duration-700">
        <div className="flex flex-col items-center text-center">
          {/* Warning Shield Icon */}
          <div className="w-24 h-24 mb-10 relative group">
             <div className="absolute inset-0 bg-orange-500 blur-2xl opacity-20 group-hover:opacity-40 transition-opacity duration-1000"></div>
             <div className="relative w-full h-full bg-gradient-to-br from-orange-500 to-red-600 rounded-[2rem] flex items-center justify-center text-white shadow-2xl shadow-orange-500/20 rotate-3 group-hover:rotate-0 transition-transform duration-500">
                <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                  <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/>
                  <line x1="12" y1="9" x2="12" y2="13"/>
                  <line x1="12" y1="17" x2="12.01" y2="17"/>
                </svg>
             </div>
          </div>

          <h1 className="text-4xl font-black text-white mb-6 tracking-tight">
            Connection <span className="text-orange-500">Insecure</span>
          </h1>
          
          <p className="text-slate-400 text-lg leading-relaxed mb-8">
            You tried to visit <code className="bg-white/5 px-2 py-1 rounded text-orange-400 font-mono text-sm break-all">{url}</code>, which does not support a secure connection.
          </p>

          <div className="p-6 rounded-2xl bg-white/5 border border-white/5 mb-10 text-left w-full">
            <h2 className="text-sm font-bold text-slate-300 mb-2 uppercase tracking-widest">Security Risk</h2>
            <p className="text-xs text-slate-500 leading-relaxed">
              OTF Browser has blocked this page because it uses an unencrypted connection (HTTP). 
              Any information you enter—passwords, credit cards, or messages—could be stolen by attackers. 
              We recommend only visiting sites that support HTTPS.
            </p>
          </div>


          <div className="mt-12 flex items-center gap-2 text-slate-600 font-bold text-[10px] uppercase tracking-[0.3em]">
             <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
             </svg>
             OTF Security Guard Active
          </div>
        </div>
      </div>
    </div>
  );
};

export default InsecureBlocked;
