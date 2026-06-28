import React, { useState, useEffect } from 'react';
import SearchHero from './SearchHero';
import QuickLinks from './QuickLinks';
import { isBridgeAvailable, getNativeSettings, nativeRequest } from '../shared/nativeRequest';

const PrivateBadge = () => (
  <div className="flex items-center gap-2 px-3 py-1.5 rounded-full bg-violet-100 dark:bg-violet-500/20 border border-violet-300 dark:border-violet-500/40 shadow-sm animate-in fade-in duration-500">
    <svg className="w-4 h-4 text-violet-600 dark:text-violet-300" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
      <path d="M2 13h20" /><path d="M5 13l1.5-5.5A2 2 0 0 1 8.4 6h7.2a2 2 0 0 1 1.9 1.5L19 13" />
      <circle cx="7" cy="16" r="2.5" /><circle cx="17" cy="16" r="2.5" /><path d="M9.5 16h5" />
    </svg>
    <span className="text-[11px] font-bold text-violet-700 dark:text-violet-200 uppercase tracking-[0.15em]">Private</span>
  </div>
);

const NewTab = () => {
  const [tabId, setTabId] = useState(null);
  const [isPrivate, setIsPrivate] = useState(false);
  const [isGuest, setIsGuest] = useState(false);
  const [time, setTime] = useState(new Date());
  const [appearanceMode, setAppearanceMode] = useState('auto');

  const applyTheme = (mode) => {
    const root = document.documentElement;
    if (mode === 'light') {
      root.classList.remove('dark');
    } else if (mode === 'dark') {
      root.classList.add('dark');
    } else {
      if (window.matchMedia('(prefers-color-scheme: dark)').matches) {
        root.classList.add('dark');
      } else {
        root.classList.remove('dark');
      }
    }
  };

  useEffect(() => {
    applyTheme(appearanceMode);
  }, [appearanceMode]);

  useEffect(() => {
    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const handleChange = () => {
      if (appearanceMode === 'auto') applyTheme('auto');
    };
    mediaQuery.addEventListener('change', handleChange);
    return () => mediaQuery.removeEventListener('change', handleChange);
  }, [appearanceMode]);

  useEffect(() => {
    if (isBridgeAvailable()) {
      getNativeSettings()
        .then((settings) => setAppearanceMode(settings.appearanceMode || 'auto'))
        .catch(() => {});
    }
  }, []);

  useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 60000);
    return () => clearInterval(timer);
  }, []);

  useEffect(() => {
    if (isBridgeAvailable()) {
      nativeRequest({ method: 'tabs.currentContext' })
        .then((context) => {
          setTabId(context.tabId);
          setIsPrivate(context.isPrivate === true);
        })
        .catch(() => {});
      nativeRequest({ method: 'session.isGuest' })
        .then((value) => setIsGuest(value === true))
        .catch(() => {});
    }
  }, []);

  const getGreeting = () => {
    const hour = time.getHours();
    if (hour < 12) return 'Good morning';
    if (hour < 18) return 'Good afternoon';
    return 'Good evening';
  };

  return (
    <div className={`flex flex-col items-center h-screen overflow-y-auto text-main selection:bg-orange-500/30 overflow-x-hidden ${isPrivate ? 'bg-violet-950/90' : 'bg-main'}`}>
      {/* Ambient Background Glow */}
      <div className={`absolute top-[-10%] left-[-10%] w-[40%] h-[40%] blur-[120px] rounded-full pointer-events-none ${isPrivate ? 'bg-violet-500/10' : 'bg-orange-500/5'}`}></div>
      <div className={`absolute bottom-[-10%] right-[-10%] w-[40%] h-[40%] blur-[120px] rounded-full pointer-events-none ${isPrivate ? 'bg-violet-500/5' : 'bg-amber-500/5'}`}></div>

      {/* Top Right Date/Time & Private Badge */}
      <div className="absolute top-8 right-10 flex items-start gap-4 animate-in fade-in slide-in-from-top-4 duration-1000">
        {isPrivate && <PrivateBadge />}
        <div className="text-right">
        <p className={`text-2xl font-bold tracking-tight leading-none mb-1 ${isPrivate ? 'text-white' : 'text-main'}`}>
          {time.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' })}
        </p>
        <p className={`text-[10px] font-bold uppercase tracking-[0.2em] ${isPrivate ? 'text-violet-200' : 'text-muted'}`}>
          {time.toLocaleDateString(undefined, { weekday: 'short', month: 'short', day: 'numeric' })}
        </p>
      </div>
      </div>

      {/* Spacer to push content down or keep layout balanced */}
      <div className="h-8"></div>

      <div className="flex-1 flex flex-col items-center w-full max-w-4xl px-6 relative z-10 justify-center pb-8">
        <div className="animate-in fade-in slide-in-from-bottom-8 duration-1000 flex flex-col items-center">
          <div className="flex items-center gap-4 mb-3">
            <div className="relative group">
              <div className={`absolute -inset-1 rounded-xl blur opacity-20 group-hover:opacity-40 transition duration-1000 ${isPrivate ? 'bg-gradient-to-r from-violet-600 to-violet-500' : 'bg-gradient-to-r from-orange-600 to-amber-500'}`}></div>
              <img
                src="/assets/icons/otf-browser-128.png"
                alt="OTF Browser Logo"
                className="relative w-12 h-12 object-contain"
              />
            </div>
            <h1 className={`text-4xl font-extrabold tracking-tight ${isPrivate ? 'text-white' : 'text-main'}`}>
              OTF <span className="text-orange-500">Browser</span>
            </h1>
          </div>
          
          <div className="mb-4">
            <p className={`text-sm font-bold uppercase tracking-[0.3em] ${isPrivate ? 'text-violet-200' : 'text-muted'}`}>
              {getGreeting()}, Explorer
            </p>
          </div>
        </div>

        <SearchHero tabId={tabId} isPrivate={isPrivate} isGuest={isGuest} />
        <QuickLinks isPrivate={isPrivate} />
      </div>
      
      <footer className="mt-auto pt-4 pb-8 text-[10px] font-bold uppercase tracking-[0.3em] animate-in fade-in duration-1000 delay-500 text-center z-10 shrink-0">
        <span className={`${isPrivate ? 'text-white' : 'text-muted/30'}`}>
          {'Engineered for Privacy & Security'}
        </span>
      </footer>
    </div>
  );
};

export default NewTab;
