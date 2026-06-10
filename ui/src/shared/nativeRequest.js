let nextRequestId = 1;

const isRpcRequest = (request) =>
  request && typeof request === 'object' && typeof request.method === 'string';

export const nativeRequest = (request, options = {}) => new Promise((resolve, reject) => {
  if (!window.cefQuery) {
    reject(new Error('Native bridge unavailable'));
    return;
  }

  const rpc = isRpcRequest(request);
  const rpcId = rpc ? `ui-${nextRequestId++}` : '';
  const wireRequest = rpc
    ? JSON.stringify({
        id: rpcId,
        method: request.method,
        params: request.params || {},
      })
    : request;

  window.cefQuery({
    request: wireRequest,
    onSuccess: (response) => {
      if (rpc) {
        let envelope;
        try {
          envelope = JSON.parse(response);
        } catch (_) {
          reject(new Error(`Invalid native RPC response for ${request.method}`));
          return;
        }
        if (!envelope || envelope.id !== rpcId || typeof envelope.ok !== 'boolean') {
          reject(new Error(`Malformed native RPC response for ${request.method}`));
          return;
        }
        if (!envelope.ok) {
          reject(new Error(envelope.error?.message || `Native RPC failed: ${request.method}`));
          return;
        }
        resolve(envelope.result);
        return;
      }
      if (!options.parseJson) {
        resolve(response);
        return;
      }
      try {
        resolve(JSON.parse(response));
      } catch (err) {
        reject(new Error(`Invalid native JSON response for ${request}`));
      }
    },
    onFailure: (code, message) => {
      reject(new Error(message || `Native request failed (${code})`));
    },
  });
});

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
