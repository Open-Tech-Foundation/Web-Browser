import React, { useMemo, useEffect, useState } from 'react';

const InsecureBlocked = () => {
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
    if (window.cefQuery) {
      window.cefQuery({
        request: "get-settings",
        onSuccess: (response) => {
          try {
            const settings = JSON.parse(response);
            const mode = settings.appearanceMode || 'auto';
            setAppearanceMode(mode);
            applyTheme(mode);
          } catch (e) {}
        }
      });
    }

    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const handleChange = () => {
      if (appearanceMode === 'auto') applyTheme('auto');
    };
    mediaQuery.addEventListener('change', handleChange);
    return () => mediaQuery.removeEventListener('change', handleChange);
  }, [appearanceMode]);

  const blockedUrl = useMemo(() => {
    const params = new URLSearchParams(window.location.search);
    return params.get('url') || '';
  }, []);

  return (
    <div className="min-h-screen bg-main text-main flex items-center justify-center p-6">
      <div className="w-full max-w-2xl rounded-[2rem] border border-main bg-card shadow-2xl shadow-orange-500/10 p-8 md:p-10">
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
            <p className="text-muted leading-relaxed">
              The browser blocked an insecure HTTP navigation to protect your data.
            </p>
          </div>
        </div>

        {blockedUrl && (
          <div className="mb-6 rounded-2xl border border-main bg-main/5 p-4">
            <div className="text-[11px] uppercase tracking-[0.22em] text-muted font-bold mb-2">Blocked URL</div>
            <div className="font-mono text-sm text-main break-all">{blockedUrl}</div>
          </div>
        )}

        <div className="space-y-3 text-sm text-muted leading-relaxed">
          <p>
            Insecure pages can expose passwords, messages, and credit card information.
          </p>
        </div>
      </div>
    </div>
  );
};

export default InsecureBlocked;
