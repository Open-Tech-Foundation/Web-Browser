import React, { useState, useEffect } from 'react';
import SearchHero from './SearchHero';
import QuickLinks from './QuickLinks';

const NewTab = () => {
  const [tabId, setTabId] = useState(null);
  const [time, setTime] = useState(new Date());

  useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 60000);
    return () => clearInterval(timer);
  }, []);

  useEffect(() => {
    if (window.cefQuery) {
      window.cefQuery({
        request: 'get-my-tab-id',
        onSuccess: (id) => setTabId(parseInt(id))
      });
    }
  }, []);

  const getGreeting = () => {
    const hour = time.getHours();
    if (hour < 12) return 'Good morning';
    if (hour < 18) return 'Good afternoon';
    return 'Good evening';
  };

  return (
    <div className="flex flex-col items-center justify-center min-h-screen bg-main text-main selection:bg-orange-500/30 overflow-x-hidden relative">
      {/* Ambient Background Glow */}
      <div className="absolute top-[-10%] left-[-10%] w-[40%] h-[40%] bg-orange-500/5 blur-[120px] rounded-full pointer-events-none"></div>
      <div className="absolute bottom-[-10%] right-[-10%] w-[40%] h-[40%] bg-amber-500/5 blur-[120px] rounded-full pointer-events-none"></div>

      {/* Top Right Date/Time */}
      <div className="absolute top-8 right-10 text-right animate-in fade-in slide-in-from-top-4 duration-1000">
        <p className="text-2xl font-bold text-main tracking-tight leading-none mb-1">
          {time.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' })}
        </p>
        <p className="text-[10px] font-bold text-muted uppercase tracking-[0.2em]">
          {time.toLocaleDateString(undefined, { weekday: 'short', month: 'short', day: 'numeric' })}
        </p>
      </div>

      <div className="flex flex-col items-center w-full max-w-4xl px-6 relative z-10">
        <div className="animate-in fade-in slide-in-from-bottom-8 duration-1000 flex flex-col items-center">
          <div className="flex items-center gap-4 mb-3">
            <div className="relative group">
              <div className="absolute -inset-1 bg-gradient-to-r from-orange-600 to-amber-500 rounded-xl blur opacity-20 group-hover:opacity-40 transition duration-1000"></div>
              <div className="relative flex items-center justify-center w-12 h-12 bg-card/80 backdrop-blur-xl rounded-xl border border-main text-orange-500 shadow-2xl">
                <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                  <circle cx="12" cy="12" r="10"></circle>
                  <path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"></path>
                  <path d="M2 12h20"></path>
                </svg>
              </div>
            </div>
            <h1 className="text-4xl font-extrabold tracking-tight text-main">
              OTF <span className="text-orange-500">Browser</span>
            </h1>
          </div>
          
          <p className="text-sm font-bold text-muted uppercase tracking-[0.3em] mb-4">
            {getGreeting()}, Explorer
          </p>
        </div>

        <SearchHero tabId={tabId} />
        <QuickLinks />
      </div>
      
      <div className="absolute bottom-10 text-muted/30 text-[10px] font-bold uppercase tracking-[0.3em] animate-in fade-in duration-1000 delay-500">
        Engineered for Privacy & Security
      </div>
    </div>
  );
};

export default NewTab;
