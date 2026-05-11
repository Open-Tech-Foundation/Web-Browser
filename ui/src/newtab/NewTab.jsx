import React from 'react';

const BROWSER_SCHEME = {
  NEWTAB: 'browser://newtab',
  SETTINGS: 'browser://settings',
};

const NewTab = () => {
  return (
    <div className="flex flex-col items-center justify-center h-screen bg-[#0f172a] text-slate-100 selection:bg-orange-500/30">
      <div className="relative group">
        <div className="absolute -inset-1 bg-gradient-to-r from-orange-600 to-amber-500 rounded-full blur opacity-25 group-hover:opacity-50 transition duration-1000 group-hover:duration-200"></div>
        <div className="relative flex items-center justify-center w-24 h-24 bg-[#1e293b] rounded-full border border-white/10 text-orange-500 shadow-2xl">
          <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
            <circle cx="12" cy="12" r="10"></circle>
            <path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"></path>
            <path d="M2 12h20"></path>
          </svg>
        </div>
      </div>
      
      <h1 className="mt-8 text-4xl font-bold tracking-tight text-white/90">
        OTF <span className="text-orange-500">Browser</span>
      </h1>
      
      <p className="mt-4 text-slate-400 text-lg font-medium animate-pulse">
        Search or type a URL to get started
      </p>

      <div className="mt-12 flex gap-4">
        {['GitHub', 'Documentation', 'Settings'].map((item) => (
          <div 
            key={item}
            className="px-4 py-2 bg-white/5 border border-white/10 rounded-lg text-sm font-medium text-slate-300 hover:bg-white/10 hover:border-white/20 transition-all cursor-pointer"
            onClick={() => {
              if (item === 'Settings') window.cefQuery({ request: `new-tab:${BROWSER_SCHEME.SETTINGS}` });
            }}
          >
            {item}
          </div>
        ))}
      </div>
    </div>
  );
};

export default NewTab;
