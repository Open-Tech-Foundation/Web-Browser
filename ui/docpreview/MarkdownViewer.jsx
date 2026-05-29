import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { marked } from 'marked';

marked.setOptions({
  breaks: true,
  gfm: true,
});

function applyTheme(mode) {
  const root = document.documentElement;
  if (mode === 'light') {
    root.classList.remove('dark');
  } else if (mode === 'dark') {
    root.classList.add('dark');
  } else {
    const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    root.classList.toggle('dark', prefersDark);
  }
}

const MarkdownViewer = ({ content }) => {
  const [leftPct, setLeftPct] = useState(50);
  const rowRef = useRef(null);

  useEffect(() => {
    if (!window.cefQuery) return;
    window.cefQuery({
      request: 'get-settings',
      onSuccess: (response) => {
        try {
          const settings = JSON.parse(response);
          applyTheme(settings.appearanceMode || 'auto');
        } catch { /* ignore */ }
      },
    });
  }, []);

  const html = useMemo(() => {
    if (!content) return '';
    try {
      return marked.parse(content);
    } catch {
      return '<pre style="color:red;font-family:monospace">Failed to render markdown</pre>';
    }
  }, [content]);

  const handleMouseDown = useCallback((e) => {
    e.preventDefault();
    const row = rowRef.current;
    if (!row) return;
    const onMove = (ev) => {
      const rect = row.getBoundingClientRect();
      const pct = ((ev.clientX - rect.left) / rect.width) * 100;
      setLeftPct(Math.min(80, Math.max(20, pct)));
    };
    const onUp = () => {
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
      document.body.style.cursor = '';
      document.body.style.userSelect = '';
    };
    document.body.style.cursor = 'col-resize';
    document.body.style.userSelect = 'none';
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
  }, []);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', width: '100%' }}>
      <style>{`
        .md-preview h1 { font-size: 2em; font-weight: 600; margin: 0.67em 0; padding-bottom: 0.3em; border-bottom: 1px solid var(--border-main); }
        .md-preview h2 { font-size: 1.5em; font-weight: 600; margin: 0.83em 0; padding-bottom: 0.3em; border-bottom: 1px solid var(--border-main); }
        .md-preview h3 { font-size: 1.25em; font-weight: 600; margin: 1em 0; }
        .md-preview h4 { font-size: 1em; font-weight: 600; margin: 1.33em 0; }
        .md-preview h5, .md-preview h6 { font-size: 0.875em; font-weight: 600; margin: 1.67em 0; }
        .md-preview p { margin-top: 0; margin-bottom: 16px; }
        .md-preview a { color: var(--color-brand-orange); text-decoration: none; }
        .md-preview a:hover { text-decoration: underline; }
        .md-preview ul, .md-preview ol { padding-left: 2em; margin-bottom: 16px; margin-top: 0; }
        .md-preview li { word-wrap: break-word; }
        .md-preview li + li { margin-top: 0.25em; }
        .md-preview blockquote { margin: 0 0 16px; padding: 0 1em; color: var(--text-muted); border-left: 0.25em solid var(--border-main); }
        .md-preview pre { margin-bottom: 16px; padding: 16px; overflow: auto; font-size: 85%; line-height: 1.45; border-radius: 6px; background: var(--bg-main); border: 1px solid var(--border-main); }
        .md-preview pre code { padding: 0; margin: 0; background: transparent; border: 0; font-size: 100%; white-space: pre; }
        .md-preview code { padding: 0.2em 0.4em; margin: 0; font-size: 85%; border-radius: 6px; background: color-mix(in srgb, var(--text-muted) 20%, transparent); font-family: 'JetBrains Mono', 'Fira Code', 'SFMono-Regular', Consolas, monospace; }
        .md-preview table { display: block; width: 100%; overflow: auto; border-spacing: 0; border-collapse: collapse; margin-bottom: 16px; }
        .md-preview table th { font-weight: 600; padding: 6px 13px; border: 1px solid var(--border-main); background: var(--bg-main); }
        .md-preview table td { padding: 6px 13px; border: 1px solid var(--border-main); }
        .md-preview table tr { background: var(--bg-card); border-top: 1px solid var(--border-main); }
        .md-preview table tr:nth-child(2n) { background: var(--bg-main); }
        .md-preview img { max-width: 100%; box-sizing: border-box; border-radius: 6px; }
        .md-preview hr { height: 0.25em; padding: 0; margin: 24px 0; background: var(--border-main); border: 0; }
      `}</style>
      <div ref={rowRef} style={{ flex: 1, display: 'flex', minHeight: 0 }}>
        <div style={{ width: `${leftPct}%`, overflow: 'auto', padding: '32px', fontFamily: 'monospace', fontSize: '13px', lineHeight: 1.6, whiteSpace: 'pre-wrap', wordBreak: 'break-word', backgroundColor: 'var(--bg-main)', color: 'var(--text-main)', borderRight: '1px solid var(--border-main)' }}>
          {content || ''}
        </div>
        <div onMouseDown={handleMouseDown} style={{ width: '4px', cursor: 'col-resize', flexShrink: 0, backgroundColor: 'var(--border-main)', position: 'relative' }}>
          <div style={{ position: 'absolute', top: 0, bottom: 0, left: -4, right: -4 }} />
        </div>
        <div style={{ width: `${100 - leftPct}%`, overflow: 'auto', backgroundColor: 'var(--bg-card)' }}>
          <div className="md-preview" style={{ maxWidth: '882px', margin: '0 auto', padding: '32px', fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif', fontSize: '16px', lineHeight: 1.6, color: 'var(--text-main)' }} dangerouslySetInnerHTML={{ __html: html }} />
        </div>
      </div>
    </div>
  );
};

export default MarkdownViewer;
