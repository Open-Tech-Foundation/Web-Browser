import React, { useRef, useEffect, useState, useCallback } from 'react';

const S = {
  bar: {
    display: 'flex',
    alignItems: 'center',
    height: 36,
    padding: '0 10px',
    gap: 6,
    background: 'var(--bg, #ffffff)',
    border: '1px solid var(--accent, #FF7A00)',
    borderRadius: '0 0 8px 8px',
    boxShadow: '0 4px 12px rgba(255,122,0,0.18)',
    fontFamily: "'Inter', system-ui, sans-serif",
  },
  input: {
    flex: 1,
    minWidth: 120,
    border: 'none',
    outline: 'none',
    fontSize: 13,
    background: 'transparent',
    color: 'var(--fg, #0f172a)',
    padding: '2px 4px',
  },
  count: {
    fontSize: 11,
    color: 'var(--muted, #64748b)',
    minWidth: 50,
    textAlign: 'center',
    userSelect: 'none',
    whiteSpace: 'nowrap',
  },
  btn: {
    background: 'none',
    border: 'none',
    cursor: 'pointer',
    color: 'var(--muted, #94a3b8)',
    padding: '3px 5px',
    borderRadius: 4,
    fontSize: 12,
    lineHeight: 1,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    transition: 'background 0.15s, color 0.15s',
  },
  btnActive: {
    background: 'rgba(255,122,0,0.12)',
    color: 'var(--accent, #FF7A00)',
  },
  sep: {
    width: 1,
    height: 16,
    background: 'var(--sep, #e2e8f0)',
    margin: '0 3px',
    flexShrink: 0,
  },
  toggle: {
    fontSize: 10,
    fontWeight: 600,
    letterSpacing: '0.3px',
    textTransform: 'uppercase',
    padding: '3px 6px',
  },
};

const FindBar = () => {
  const inputRef = useRef(null);
  const [text, setText] = useState('');
  const [count, setCount] = useState(0);
  const [active, setActive] = useState(0);
  const [matchCase, setMatchCase] = useState(false);
  const [isFinal, setIsFinal] = useState(true);
  const [activeTabId, setActiveTabId] = useState(-1);
  const debounceRef = useRef(null);

  const cancelPendingFind = useCallback(() => {
    if (debounceRef.current) {
      clearTimeout(debounceRef.current);
      debounceRef.current = null;
    }
  }, []);

  // Subscribe to CEF events (persistent)
  useEffect(() => {
    if (!window.cefQuery) return;
    const sub = window.cefQuery({
      request: 'findbar-subscribe',
      persistent: true,
      onSuccess: (json) => {
        try {
          const ev = JSON.parse(json);
          if (ev.key === 'find-result') {
            setCount(ev.count ?? 0);
            setActive(ev.active ?? 0);
            setIsFinal(ev.final ?? true);
          } else if (ev.key === 'find-restore') {
            cancelPendingFind();
            setActiveTabId(ev.tabId ?? -1);
            setText(ev.text ?? '');
            setMatchCase(ev.matchCase ?? false);
            setCount(0);
            setActive(0);
            setIsFinal(true);
            // Focus + select, but only if user is not already typing in this input
            requestAnimationFrame(() => {
              const el = inputRef.current;
              if (el && document.activeElement !== el) {
                el.focus();
                el.select();
              }
            });
          } else if (ev.key === 'findbar-closed') {
            cancelPendingFind();
            setActiveTabId(ev.tabId ?? -1);
            setCount(0);
            setActive(0);
            setIsFinal(true);
          }
        } catch (e) {}
      },
    });
    return () => {
      cancelPendingFind();
      if (sub && typeof sub.cancel === 'function') sub.cancel();
    };
  }, [cancelPendingFind]);

  // Focus + select once on mount only
  useEffect(() => {
    const el = inputRef.current;
    if (el) { el.focus(); el.select(); }
  }, []);

  // Execute a find (called by buttons, Enter, toggles)
  const execFind = useCallback((fwd, caseSensitive, findNext) => {
    if (activeTabId < 0) return;
    const t = (inputRef.current?.value ?? text).trim();
    if (!t) {
      cancelPendingFind();
      setCount(0);
      setActive(0);
      setIsFinal(true);
      window.cefQuery({ request: 'findbar-stop:' });
      return;
    }
    if (!findNext) setIsFinal(false);
    window.cefQuery({
      request: `findbar-find:${JSON.stringify({
        tabId: activeTabId,
        text: t,
        forward: !!fwd,
        matchCase: !!caseSensitive,
        findNext: !!findNext,
      })}`,
    });
  }, [activeTabId, cancelPendingFind, text]);

  // Typing: debounced, always starts a NEW search (findNext = false)
  const onTyping = useCallback((val) => {
    setText(val);
    cancelPendingFind();
    if (!val.trim()) {
      setCount(0);
      setActive(0);
      setIsFinal(true);
      window.cefQuery({ request: 'findbar-stop:' });
      return;
    }
    setIsFinal(false);
    const tabIdAtSchedule = activeTabId;
    debounceRef.current = setTimeout(() => {
      const el = inputRef.current;
      if (!el) return;
      const t = el.value.trim();
      debounceRef.current = null;
      if (t && tabIdAtSchedule >= 0) {
        window.cefQuery({
          request: `findbar-find:${JSON.stringify({
            tabId: tabIdAtSchedule,
            text: t,
            forward: true,
            matchCase: !!matchCase,
            findNext: false,
          })}`,
        });
      }
    }, 200);
  }, [activeTabId, cancelPendingFind, matchCase]);

  const onKeyDown = useCallback((e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      execFind(!e.shiftKey, matchCase, true); // findNext=true
      return;
    }
    if (e.key === 'Escape') {
      e.preventDefault();
      window.cefQuery({ request: 'findbar-close:' });
      return;
    }
  }, [execFind, matchCase]);

  // Toggle case: fresh search with new case
  const onToggleCase = useCallback(() => {
    const next = !matchCase;
    setMatchCase(next);
    cancelPendingFind();
    const t = (inputRef.current?.value ?? text).trim();
    if (t && activeTabId >= 0) {
      setIsFinal(false);
      window.cefQuery({
        request: `findbar-find:${JSON.stringify({
          tabId: activeTabId,
          text: t,
          forward: true,
          matchCase: !!next,
          findNext: false,
        })}`,
      });
    }
  }, [activeTabId, cancelPendingFind, matchCase, text]);

  const onPrev = useCallback(() => execFind(false, matchCase, true), [execFind, matchCase]);
  const onNext = useCallback(() => execFind(true, matchCase, true), [execFind, matchCase]);

  const countText = count > 0 ? `${active}/${count}` : isFinal ? '0/0' : '';
  const btnStyle = (on) => ({ ...S.btn, ...S.toggle, ...(on ? S.btnActive : {}) });

  return (
    <div style={S.bar}>
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" style={{ flexShrink: 0, color: 'var(--muted,#94a3b8)' }}>
        <circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/>
      </svg>
      <input
        ref={inputRef}
        type="text"
        value={text}
        onChange={(e) => onTyping(e.target.value)}
        onKeyDown={onKeyDown}
        placeholder="Find in page..."
        style={S.input}
      />
      <span style={S.count}>{countText}</span>
      <span style={S.sep} />
      <button onClick={onPrev} style={S.btn} title="Previous (Shift+Enter)">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="m18 15-6-6-6 6"/></svg>
      </button>
      <button onClick={onNext} style={S.btn} title="Next (Enter)">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="m6 9 6 6 6-6"/></svg>
      </button>
      <span style={S.sep} />
      <button onClick={onToggleCase} style={btnStyle(matchCase)} title="Match case">Aa</button>
      <span style={S.sep} />
      <button onClick={() => window.cefQuery({ request: 'findbar-close:' })} style={S.btn} title="Close (Escape)">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>
      </button>
    </div>
  );
};

export default FindBar;
