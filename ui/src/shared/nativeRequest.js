// Back-compat facade over the transport-agnostic bridge (see ./bridge.js).
//
// Historically the UI called `nativeRequest({ method, params })` directly against
// `window.cefQuery`. That logic now lives in the bridge; this module keeps the
// existing call sites working while the backend transport is swapped underneath.
// New code may import from ./bridge directly.

import { call, subscribe, isBridgeAvailable, setTransport } from './bridge';

// Re-exported so call sites can stay transport-agnostic without importing two modules.
export { subscribe, isBridgeAvailable, setTransport };

export const nativeRequest = (request) => {
  if (!request || typeof request.method !== 'string') {
    return Promise.reject(new Error('nativeRequest requires { method }'));
  }
  return call(request.method, request.params || {});
};

export const getNativeSettings = () => call('settings.get');

// Client-side request versioning: the bridge has no native cancellation, so
// callers stamp in-flight work and drop responses that arrive after a newer one.
export const createNativeRequestScope = () => {
  let version = 0;
  return {
    next() {
      version += 1;
      return version;
    },
    isCurrent(token) {
      return token === version;
    },
    cancel() {
      version += 1;
    },
  };
};
