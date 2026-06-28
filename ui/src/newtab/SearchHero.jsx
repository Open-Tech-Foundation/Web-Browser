import React, { useState, useEffect, useRef, useCallback } from 'react';
import { resolveUrl } from '../shared/search';
import { isBridgeAvailable, getNativeSettings, nativeRequest } from '../shared/nativeRequest';

const stateByTab = {};

const GenericIcon = ({ isPrivate }) => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className={`w-full h-full ${isPrivate ? 'text-violet-300' : 'text-muted'}`}>
    <circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>
  </svg>
);

const EngineLogo = ({ id, name, isPrivate }) => {
  const [error, setError] = useState(false);
  return error ? <GenericIcon isPrivate={isPrivate} /> : (
    <img 
      src={`/assets/logos/${id}.svg`} 
      alt={name} 
      className="w-full h-full object-contain opacity-60 group-focus-within:opacity-100 transition-opacity duration-300" 
      onError={() => setError(true)} 
    />
  );
};

const SearchHero = ({ tabId, isPrivate, isGuest }) => {
  const cached = tabId != null ? stateByTab[tabId] : null;
  const [query, setQuery] = useState(cached ? cached.query : '');
  const [engine, setEngine] = useState(() => {
    if (cached && cached.engine) return cached.engine;
    return localStorage.getItem('otf_last_engine') || '';
  });
  const [customEngines, setCustomEngines] = useState([]);
  const [suggestions, setSuggestions] = useState([]);
  const [selectedIdx, setSelectedIdx] = useState(-1);
  const inputRef = useRef(null);
  const debounceRef = useRef(null);

  useEffect(() => {
    const handleSettingsChanged = (event) => {
      const nextEngine = event.detail?.searchEngine || '';
      setEngine(nextEngine);
      setCustomEngines(event.detail?.customEngines || []);
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
    if (isBridgeAvailable()) {
      getNativeSettings()
        .then((s) => {
          setCustomEngines(s.customSearchEngines || []);
          if (s.searchEngine) {
            setEngine(s.searchEngine);
            localStorage.setItem('otf_last_engine', s.searchEngine);
          } else {
            localStorage.removeItem('otf_last_engine');
            setEngine('');
          }
        })
        .catch(() => {});
    }
  }, []);

  const fetchSuggestions = useCallback((value) => {
    if (isGuest || !value || !value.trim() || !isBridgeAvailable()) {
      setSuggestions([]);
      return;
    }
    nativeRequest({
      method: 'search.suggestions',
      params: { prefix: value, limit: 10 },
    })
      .then((list) => setSuggestions(Array.isArray(list) ? list : []))
      .catch(() => setSuggestions([]));
  }, [isGuest]);

  const triggerSuggestions = useCallback((value) => {
    if (debounceRef.current) clearTimeout(debounceRef.current);
    if (!value || !value.trim()) {
      setSuggestions([]);
      return;
    }
    debounceRef.current = setTimeout(() => fetchSuggestions(value), 200);
  }, [fetchSuggestions]);

  const doNavigate = useCallback((input) => {
    setSuggestions([]);
    setSelectedIdx(-1);
    if (isBridgeAvailable() && !isGuest) {
      nativeRequest({
        method: 'search.history.add',
        params: { query: input },
      }).catch(() => {});
    }
    const navigateTo = (url) => {
      nativeRequest({ method: 'navigation.current', params: { url } }).catch(() => {});
    };
    nativeRequest({
      method: 'navigation.resolveInput',
      params: { input },
    })
      .then(navigateTo)
      .catch(() => navigateTo(resolveUrl(input, engine, customEngines)));
  }, [engine, customEngines, isGuest]);

  const handleKeyDown = (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      if (selectedIdx >= 0 && suggestions[selectedIdx]) {
        doNavigate(suggestions[selectedIdx]);
      } else if (query.trim()) {
        let input = query.trim();
        if (e.ctrlKey) {
          input = e.shiftKey ? `https://${input}.org` : `https://${input}.com`;
        }
        doNavigate(input);
      }
      inputRef.current?.blur();
      return;
    }
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      setSelectedIdx((prev) => Math.min(prev + 1, suggestions.length - 1));
      return;
    }
    if (e.key === 'ArrowUp') {
      e.preventDefault();
      setSelectedIdx((prev) => Math.max(prev - 1, -1));
      return;
    }
    if (e.key === 'Escape') {
      setSuggestions([]);
      setSelectedIdx(-1);
      return;
    }
  };

  const engineDisplayName = engine
    ? (customEngines.find(e => e.id === engine)?.name || engine.charAt(0).toUpperCase() + engine.slice(1))
    : '';

  const showDropdown = suggestions.length > 0;

  return (
    <div className="w-full max-w-2xl mx-auto mt-12 animate-in fade-in slide-in-from-bottom-8 duration-1000 delay-150">
      <div className="relative group">
        <div className="absolute -inset-1 bg-gradient-to-r from-orange-600/20 to-amber-500/20 rounded-2xl blur opacity-0 group-focus-within:opacity-100 transition duration-700"></div>
        <div className={`relative flex flex-col rounded-2xl shadow-2xl transition-all duration-500 ${isPrivate ? 'bg-violet-950/60 border border-violet-500/20' : 'bg-card/80 border border-main'}`}>
          <div className={`flex items-center rounded-2xl transition-all duration-500 group-focus-within:border-orange-500/40 ${isPrivate ? 'group-focus-within:bg-violet-950/80' : 'group-focus-within:bg-card'} ${showDropdown ? 'rounded-b-none' : ''}`}>
            <div className="w-6 h-6 ml-5 shrink-0 flex items-center justify-center">
              {engine ? <EngineLogo id={engine} name={engineDisplayName} isPrivate={isPrivate} /> : <GenericIcon isPrivate={isPrivate} />}
            </div>
            <input
              ref={inputRef}
              type="text"
              value={query}
              onChange={(e) => {
                setQuery(e.target.value);
                setSelectedIdx(-1);
                triggerSuggestions(e.target.value);
              }}
              onKeyDown={handleKeyDown}
              onFocus={() => triggerSuggestions(query)}
              onBlur={() => setTimeout(() => { setSuggestions([]); setSelectedIdx(-1); }, 180)}
              placeholder={engine ? `Search with ${engineDisplayName} or enter address...` : "Search or enter address..."}
              className={`w-full bg-transparent border-none outline-none text-lg
                         py-5 px-5 font-medium ${isPrivate ? 'text-white placeholder-violet-300' : 'text-main placeholder-muted'}`}
              autoFocus
            />
            <div className="mr-5 flex items-center gap-2">
               <kbd className={`hidden sm:inline-flex items-center justify-center px-2 py-1 border rounded-lg text-[10px] font-mono ${isPrivate ? 'bg-white/5 border-violet-500/30 text-violet-300' : 'bg-main/5 border-main text-muted'}`}>
                 Enter
               </kbd>
            </div>
          </div>
          {showDropdown && (
            <div className={`border-t ${isPrivate ? 'border-violet-500/20' : 'border-main/20'} overflow-hidden rounded-b-2xl`}>
              {suggestions.map((s, i) => (
                <div
                  key={i}
                  onMouseDown={(e) => { e.preventDefault(); doNavigate(s); }}
                  className={`flex items-center gap-3 px-5 py-3 text-sm cursor-pointer transition-colors ${
                    i === selectedIdx
                      ? 'bg-orange-500/10 text-orange-600 dark:text-orange-400'
                      : isPrivate
                        ? 'text-violet-200 hover:bg-white/5'
                        : 'text-main hover:bg-black/5'
                  }`}
                >
                  <svg className="w-3.5 h-3.5 shrink-0 text-muted" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                    <circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/>
                  </svg>
                  <span className="truncate">{s}</span>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

export default SearchHero;
