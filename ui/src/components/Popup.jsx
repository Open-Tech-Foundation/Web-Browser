import React, { useEffect } from 'react';

// Shared frame for any overlay backed by otf::PopupOverlay on the C++
// side. Wires up the contract that every popup needs:
//   • Esc → hide-popup:<name>
//   • Close button → hide-popup:<name>
//   • Standardized container + header so popups look consistent
// Body content is whatever the caller passes as children.
const Popup = ({ name, title, children, className = '', closeOnBlur = false }) => {
  // Esc closes. (Window blur as a hide trigger was tried but fires
  // during the focus handoff right after Show, which causes the popup
  // to close before it paints. Click-outside-to-hide is a separate
  // feature that needs to be implemented on the C++ side via a focus
  // handler, not from the popup's renderer.)
  useEffect(() => {
    const hide = () => window.cefQuery?.({ request: `hide-popup:${name}` });
    const onKeyDown = (e) => { if (e.key === 'Escape') hide(); };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [name]);

  useEffect(() => {
    if (!closeOnBlur) return undefined;
    const hide = () => window.cefQuery?.({ request: `hide-popup:${name}` });
    const onBlur = () => {
      window.setTimeout(() => {
        if (!document.hasFocus()) hide();
      }, 120);
    };
    window.addEventListener('blur', onBlur);
    return () => window.removeEventListener('blur', onBlur);
  }, [closeOnBlur, name]);

  const close = () => window.cefQuery?.({ request: `hide-popup:${name}` });

  return (
    <div className="w-full h-full p-1.5 bg-transparent box-border">
      <div className={`popup-container w-full h-full bg-white dark:bg-[#0a0a0c] text-slate-900 dark:text-slate-100 p-6 flex flex-col text-sm rounded-2xl border border-slate-200 dark:border-white/10 shadow-2xl overflow-hidden ${className}`}>
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-[13px] font-semibold text-slate-800 dark:text-slate-100">{title}</h2>
          <button
            onClick={close}
            className="w-6 h-6 flex items-center justify-center rounded-md text-slate-400 hover:text-slate-600 dark:hover:text-slate-200 hover:bg-slate-100 dark:hover:bg-slate-700/60 transition-colors duration-150 cursor-pointer"
            aria-label="Close"
          >
            <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" strokeWidth="2.5" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        <div className="flex-1 flex flex-col overflow-hidden">
          {children}
        </div>
      </div>
    </div>
  );
};

// Subscribes to the `popup-restore:<name>` channel and calls onRestore with
// the parsed JSON payload every time C++ pushes a restore event (which it
// does on every Show()). Use this to reset transient state — selections,
// busy spinners, success badges — so a reopened popup starts fresh.
export const usePopupRestore = (name, onRestore) => {
  useEffect(() => {
    if (!window.cefQuery) return;
    window.cefQuery({
      request: `popup-restore:${name}`,
      persistent: true,
      onSuccess: (response) => {
        try {
          const msg = JSON.parse(response);
          if (msg && msg.key === 'popup-restore' && msg.name === name) {
            let payload = {};
            try { payload = typeof msg.payload === 'string' ? JSON.parse(msg.payload) : (msg.payload || {}); } catch (_) {}
            onRestore(payload);
          }
        } catch (_) {}
      },
      onFailure: () => {},
    });
    // No cleanup — the subscription lives for the popup's lifetime.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [name]);
};

export default Popup;
