// Transport-agnostic bridge between the React UI and the native browser backend.
//
// The UI talks ONLY to this module. The physical transport — today CEF's
// `window.cefQuery`, tomorrow the Rust/content shim — is hidden behind the
// Transport interface and swapped at runtime via `setTransport()`. No code above
// this layer references the CEF API or knows which backend is live.
//
// Wire protocol (transport-independent — every backend speaks the same envelope):
//   request:  { id, method, params }
//   response: { id, ok: true, result } | { id, ok: false, error: { code, message } }
//   event:    { key, ... }            pushed over a subscription, no envelope
//
// A Transport only moves opaque wire STRINGS; the envelope lives here so the
// protocol is owned in one place regardless of backend.
//
//   Transport.isAvailable(): boolean
//   Transport.request(wire): Promise<string>
//   Transport.subscribe(wire, onMessage, onError): () => void   // returns unsubscribe

let nextRequestId = 1;
const makeId = (prefix = 'ui') => `${prefix}-${nextRequestId++}`;

// --- CEF transport (the only CEF-aware code in the UI) -----------------------
const cefTransport = {
  isAvailable: () => typeof window !== 'undefined' && !!window.cefQuery,
  request(wire) {
    return new Promise((resolve, reject) => {
      window.cefQuery({
        request: wire,
        onSuccess: resolve,
        onFailure: (code, message) =>
          reject(new Error(message || `native request failed (${code})`)),
      });
    });
  },
  subscribe(wire, onMessage, onError) {
    const queryId = window.cefQuery({
      request: wire,
      persistent: true,
      onSuccess: onMessage,
      onFailure: (code, message) =>
        onError && onError(new Error(message || `subscription failed (${code})`)),
    });
    return () => {
      try {
        window.cefQueryCancel?.(queryId);
      } catch (_) {
        /* transport gone — nothing to cancel */
      }
    };
  },
};

let transport = cefTransport;

// Swap the live transport (used at the Rust cutover). Passing nothing restores CEF.
export const setTransport = (next) => {
  transport = next || cefTransport;
};
export const getTransport = () => transport;
export const isBridgeAvailable = () => transport.isAvailable();

// --- RPC call (request / response) -------------------------------------------
// Resolves with the `result` payload, rejects with an Error on transport or
// protocol failure. This is the single channel for every JS → native call.
export const call = (method, params = {}) =>
  new Promise((resolve, reject) => {
    if (!transport.isAvailable()) {
      reject(new Error('Native bridge unavailable'));
      return;
    }
    const id = makeId();
    const wire = JSON.stringify({ id, method, params: params || {} });
    transport.request(wire).then((response) => {
      let envelope;
      try {
        envelope = JSON.parse(response);
      } catch (_) {
        reject(new Error(`Invalid native RPC response for ${method}`));
        return;
      }
      if (!envelope || envelope.id !== id || typeof envelope.ok !== 'boolean') {
        reject(new Error(`Malformed native RPC response for ${method}`));
        return;
      }
      if (!envelope.ok) {
        reject(new Error(envelope.error?.message || `Native RPC failed: ${method}`));
        return;
      }
      resolve(envelope.result);
    }, reject);
  });

// --- Subscription (event stream) ---------------------------------------------
// Opens a persistent stream for `method`. `onEvent` receives each parsed event
// object ({ key, ... }). Returns an unsubscribe function.
export const subscribe = (method, params, onEvent, onError) => {
  if (!transport.isAvailable()) {
    if (onError) onError(new Error('Native bridge unavailable'));
    return () => {};
  }
  const id = makeId(`sub-${method}`);
  const wire = JSON.stringify({ id, method, params: params || {} });
  return transport.subscribe(
    wire,
    (message) => {
      let event;
      try {
        event = JSON.parse(message);
      } catch (_) {
        if (onError) onError(new Error(`Invalid native event for ${method}`));
        return;
      }
      onEvent(event);
    },
    onError,
  );
};

export const bridge = {
  call,
  subscribe,
  setTransport,
  getTransport,
  isAvailable: isBridgeAvailable,
};
export default bridge;
