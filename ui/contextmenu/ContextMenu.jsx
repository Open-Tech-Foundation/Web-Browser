import React, { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { isBridgeAvailable, nativeRequest, subscribe } from '../src/shared/nativeRequest';

// otf's page context menu: a full-window transparent overlay whose backdrop
// closes on click, with a menu built from the engine's hit-test
// (contextMenu.subscribe / .current). It mirrors the menu the browser has always
// shown — no view-source / dev-tools items (blocked by policy), and search goes
// through the backend resolver (the configured engine), never a hardcoded URL.
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

  useLayoutEffect(() => {
    const p = ctx?.params;
    if (!p) return;
    const h = menuRef.current?.offsetHeight || 0;
    const left = Math.max(4, Math.min(p.x ?? 0, window.innerWidth - MENU_WIDTH - 4));
    const top = Math.max(4, Math.min(p.y ?? 0, window.innerHeight - h - 4));
    setPos({ left, top });
  }, [ctx]);

  if (!ctx || !ctx.params) return null;
  const p = ctx.params;

  const run = (fn) => { try { fn(); } finally { hide(); } };
  const exec = (action) =>
    nativeRequest({ method: 'contextMenu.exec', params: { tabId: ctx.tabId, action, x: p.x, y: p.y } }).catch(() => {});
  const rpc = (method, params) => nativeRequest({ method, params }).catch(() => {});
  const newTab = (url) => url && rpc('navigation.newTab', { url });
  const clip = (text) => { try { navigator.clipboard.writeText(text); } catch (_) { /* ignore */ } };
  // Search the selection via the backend resolver, which applies the user's
  // configured search engine (no URL is built in the UI).
  const searchSelection = (text) =>
    nativeRequest({ method: 'navigation.resolveInput', params: { input: text } })
      .then((url) => newTab(url))
      .catch(() => {});

  const isImage = p.mediaType === 'image' || p.hasImage;
  const isMailto = (p.linkUrl || '').startsWith('mailto:');
  const hasLink = !!p.linkUrl && !isMailto;
  const selection = (p.selectionText || '').trim();
  const truncate = (s, n = 40) => (s.length > n ? `${s.slice(0, n)}…` : s);

  const groups = [];

  if (isMailto) {
    groups.push([
      { label: 'Copy Email ID', onClick: () => clip(p.linkUrl.replace(/^mailto:/, '')) },
    ]);
  } else if (hasLink) {
    groups.push([
      { label: 'Open in new tab', onClick: () => newTab(p.linkUrl) },
      { label: 'Copy link address', onClick: () => clip(p.linkUrl) },
    ]);
  }

  if (isImage) {
    groups.push([
      { label: 'Copy image', onClick: () => exec('copyImage') },
      { label: 'Save image', onClick: () => exec('saveImage') },
    ]);
  }

  if (selection && !p.isEditable) {
    groups.push([
      { label: `Search “${truncate(selection)}”`, onClick: () => searchSelection(selection) },
      { label: 'Copy', onClick: () => clip(selection) },
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
  }

  // Plain page (nothing else applies): a reload affordance, as before.
  if (!hasLink && !isMailto && !isImage && !selection && !p.isEditable) {
    groups.push([
      { label: 'Reload', onClick: () => rpc('tabs.reload', { tabId: ctx.tabId }) },
    ]);
  }

  if (groups.length === 0) return null;

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
                className={`w-full text-left px-3 py-1.5 transition-colors ${
                  item.disabled
                    ? 'opacity-40 cursor-default'
                    : 'hover:bg-orange-50 dark:hover:bg-orange-500/10 hover:text-brand-orange cursor-pointer'
                }`}
              >
                <span className="truncate">{item.label}</span>
              </button>
            ))}
          </React.Fragment>
        ))}
      </div>
    </div>
  );
}
