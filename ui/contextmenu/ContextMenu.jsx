import React, { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { isBridgeAvailable, nativeRequest, subscribe } from '../src/shared/nativeRequest';

// otf draws its own page context menu: a full-window transparent overlay whose
// backdrop closes on click, with a positioned menu built from the hit-test the
// engine reports (contextMenu.subscribe / .current). Item actions run over the
// bridge (page edits/images via contextMenu.exec, navigation/new-tab via the
// existing RPCs) or locally (clipboard for links/selection).
const MENU_WIDTH = 240;

const hide = () =>
  nativeRequest({ method: 'ui.popup.hide', params: { name: 'contextmenu' } }).catch(() => {});

export default function ContextMenu() {
  const [ctx, setCtx] = useState(null); // { tabId, params }
  const menuRef = useRef(null);
  const [pos, setPos] = useState({ left: 0, top: 0 });

  useEffect(() => {
    if (!isBridgeAvailable()) return;
    nativeRequest({ method: 'contextMenu.current' })
      .then((cur) => { if (cur && cur.params) setCtx(cur); })
      .catch(() => {});
    subscribe('contextMenu.subscribe', {}, (msg) => {
      if (msg && msg.key === 'context-menu' && msg.params) {
        setCtx({ tabId: msg.tabId, params: msg.params });
      }
    });
  }, []);

  useEffect(() => {
    const onKey = (e) => { if (e.key === 'Escape') hide(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  // Clamp the menu inside the viewport once we know its height.
  useLayoutEffect(() => {
    const p = ctx?.params;
    if (!p) return;
    const h = menuRef.current?.offsetHeight || 0;
    const left = Math.max(4, Math.min(p.x ?? 0, window.innerWidth - MENU_WIDTH - 4));
    const top = Math.max(4, Math.min(p.y ?? 0, window.innerHeight - h - 4));
    setPos({ left, top });
  }, [ctx]);

  const run = useCallback((fn) => { try { fn(); } finally { hide(); } }, []);

  if (!ctx || !ctx.params) return null;
  const p = ctx.params;
  const exec = (action) =>
    nativeRequest({ method: 'contextMenu.exec', params: { tabId: ctx.tabId, action, x: p.x, y: p.y } }).catch(() => {});
  const rpc = (method, params) => nativeRequest({ method, params }).catch(() => {});
  const clip = (text) => { try { navigator.clipboard.writeText(text); } catch (_) { /* ignore */ } };

  const isImage = p.mediaType === 'image' || p.hasImage;
  const hasLink = !!p.linkUrl;
  const hasSelection = !!(p.selectionText && p.selectionText.trim());
  const short = (s, n = 24) => (s && s.length > n ? `${s.slice(0, n)}…` : s);

  const groups = [];

  if (hasLink) {
    groups.push([
      { label: 'Open link in new tab', onClick: () => rpc('navigation.newTab', { url: p.linkUrl }) },
      { label: 'Copy link address', onClick: () => clip(p.linkUrl) },
    ]);
  }

  if (isImage) {
    groups.push([
      { label: 'Open image in new tab', onClick: () => rpc('navigation.newTab', { url: p.srcUrl }), disabled: !p.srcUrl },
      { label: 'Copy image', onClick: () => exec('copyImage') },
      { label: 'Save image as…', onClick: () => exec('saveImage') },
    ]);
  }

  if (p.isEditable) {
    groups.push([
      { label: 'Undo', onClick: () => exec('undo'), disabled: !p.canUndo },
      { label: 'Redo', onClick: () => exec('redo'), disabled: !p.canRedo },
      { label: 'Cut', onClick: () => exec('cut'), disabled: !p.canCut },
      { label: 'Copy', onClick: () => exec('copy'), disabled: !p.canCopy },
      { label: 'Paste', onClick: () => exec('paste'), disabled: !p.canPaste },
      { label: 'Select all', onClick: () => exec('selectAll'), disabled: !p.canSelectAll },
    ]);
  } else if (hasSelection) {
    groups.push([
      { label: 'Copy', onClick: () => clip(p.selectionText) },
      { label: `Search web for “${short(p.selectionText)}”`,
        onClick: () => rpc('navigation.newTab', { url: `https://www.google.com/search?q=${encodeURIComponent(p.selectionText)}` }) },
    ]);
  }

  // Page navigation — always available.
  groups.push([
    { label: 'Back', onClick: () => rpc('tabs.back', { tabId: ctx.tabId }) },
    { label: 'Forward', onClick: () => rpc('tabs.forward', { tabId: ctx.tabId }) },
    { label: 'Reload', onClick: () => rpc('tabs.reload', { tabId: ctx.tabId }) },
  ]);

  // otf's own additions.
  groups.push([
    { label: 'Copy page address', onClick: () => clip(p.pageUrl), otf: true },
    { label: 'View page source', onClick: () => rpc('navigation.newTab', { url: `view-source:${p.pageUrl}` }), otf: true, disabled: !p.pageUrl },
  ]);

  return (
    <div
      className="fixed inset-0 bg-transparent"
      onMouseDown={(e) => { if (e.target === e.currentTarget) hide(); }}
      onContextMenu={(e) => { e.preventDefault(); hide(); }}
    >
      <div
        ref={menuRef}
        style={{ left: pos.left, top: pos.top, width: MENU_WIDTH }}
        className="absolute py-1 rounded-xl border border-slate-200 dark:border-white/10 bg-white/95 dark:bg-[#0a0a0c]/95 backdrop-blur-md shadow-2xl text-[13px] text-slate-800 dark:text-slate-100 select-none overflow-hidden"
      >
        {groups.map((group, gi) => (
          <React.Fragment key={gi}>
            {gi > 0 && <div className="my-1 h-px bg-slate-200/70 dark:bg-white/10" />}
            {group.map((item, ii) => (
              <button
                key={ii}
                disabled={item.disabled}
                onClick={() => run(item.onClick)}
                className={`w-full text-left px-3 py-1.5 flex items-center gap-2 transition-colors ${
                  item.disabled
                    ? 'opacity-40 cursor-default'
                    : 'hover:bg-orange-50 dark:hover:bg-orange-500/10 hover:text-brand-orange cursor-pointer'
                }`}
              >
                {item.otf && <span className="w-1.5 h-1.5 rounded-full bg-brand-orange shrink-0" />}
                <span className="truncate">{item.label}</span>
              </button>
            ))}
          </React.Fragment>
        ))}
      </div>
    </div>
  );
}
