import React, { useEffect } from 'react';

// Shared frame for any overlay backed by otf::PopupOverlay on the C++
// side. Wires up the contract that every popup needs:
//   • Esc → hide-popup:<name>
//   • Close button → hide-popup:<name>
//   • Standardized container + header so popups look consistent
// Body content is whatever the caller passes as children.
const Popup = ({ name, title, children, className = '' }) => {
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

  const close = () => window.cefQuery?.({ request: `hide-popup:${name}` });

  return (
    <div className={`h-screen w-screen bg-white dark:bg-slate-900 text-slate-900 dark:text-slate-100 p-4 flex flex-col gap-3 text-sm ${className}`}>
      <div className="flex items-center justify-between">
        <h2 className="font-semibold text-base">{title}</h2>
        <button
          onClick={close}
          className="text-slate-400 hover:text-slate-700 dark:hover:text-slate-200 text-lg leading-none"
          aria-label="Close"
        >×</button>
      </div>
      {children}
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
