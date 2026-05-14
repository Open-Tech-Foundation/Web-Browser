import React, { useState, useEffect, useRef } from 'react';
import { resolveUrl } from '../shared/search';

const stateByTab = {};

const GenericIcon = () => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="w-full h-full text-muted">
    <circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>
  </svg>
);

const EngineLogo = ({ id, name }) => {
  const [error, setError] = useState(false);
  return error ? <GenericIcon /> : (
    <img 
      src={`/assets/logos/${id}.svg`} 
      alt={name} 
      className="w-full h-full object-contain opacity-60 group-focus-within:opacity-100 transition-opacity duration-300" 
      onError={() => setError(true)} 
    />
  );
};

const SearchHero = ({ tabId }) => {
  const cached = tabId != null ? stateByTab[tabId] : null;
  const [query, setQuery] = useState(cached ? cached.query : '');
  const [engine, setEngine] = useState(() => {
    if (cached && cached.engine) return cached.engine;
    return localStorage.getItem('otf_last_engine') || '';
  });
  const inputRef = useRef(null);

  useEffect(() => {
    const handleSettingsChanged = (event) => {
      const nextEngine = event.detail?.searchEngine || '';
      setEngine(nextEngine);
      try {
        if (nextEngine) {
          localStorage.setItem('otf_last_engine', nextEngine);
        } else {
          localStorage.removeItem('otf_last_engine');
        }
      } catch (e) {}
    };

    window.addEventListener('otf-settings-changed', handleSettingsChanged);
    return () => window.removeEventListener('otf-settings-changed', handleSettingsChanged);
  }, []);

  useEffect(() => {
    if (tabId != null) {
      stateByTab[tabId] = { query, engine };
    }
  }, [query, engine, tabId]);

  useEffect(() => {
    if (tabId != null) {
      const s = stateByTab[tabId];
      if (s) {
        setQuery(s.query || '');
        if (s.engine) setEngine(s.engine);
      }
    }
  }, [tabId]);

  useEffect(() => {
    if (window.cefQuery) {
      window.cefQuery({
        request: 'get-settings',
        onSuccess: (response) => {
          try {
            const s = JSON.parse(response);
            if (s.searchEngine) {
               setEngine(s.searchEngine);
               localStorage.setItem('otf_last_engine', s.searchEngine);
            } else {
               localStorage.removeItem('otf_last_engine');
               setEngine('');
            }
          } catch (e) {}
        }
      });
    }
  }, []);

  const handleKeyDown = (e) => {
    if (e.key === 'Enter' && query.trim()) {
      const url = resolveUrl(query.trim(), engine);
      window.cefQuery({ request: `navigate-current:${url}` });
    }
  };

  return (
    <div className="w-full max-w-2xl mx-auto mt-12 animate-in fade-in slide-in-from-bottom-8 duration-1000 delay-150">
      <div className="relative group">
        <div className="absolute -inset-1 bg-gradient-to-r from-orange-600/20 to-amber-500/20 rounded-2xl blur opacity-0 group-focus-within:opacity-100 transition duration-700"></div>
        <div className="relative flex items-center bg-card/80 backdrop-blur-xl border border-main rounded-2xl
                        group-focus-within:border-orange-500/40 group-focus-within:bg-card shadow-2xl transition-all duration-500">
          <div className="w-6 h-6 ml-5 shrink-0 flex items-center justify-center">
            <EngineLogo id={engine} name={engine} />
          </div>
          <input
            ref={inputRef}
            type="text"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder={`Search with ${engine.charAt(0).toUpperCase() + engine.slice(1)} or enter address...`}
            className="w-full bg-transparent border-none outline-none text-main text-lg
                       placeholder-muted py-5 px-5 font-medium"
            autoFocus
          />
          <div className="mr-5 flex items-center gap-2">
             <kbd className="hidden sm:inline-flex items-center justify-center px-2 py-1 bg-main/5 border border-main rounded-lg text-[10px] font-mono text-muted">
               Enter
             </kbd>
          </div>
        </div>
      </div>
    </div>
  );
};

export default SearchHero;
