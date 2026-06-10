export const nativeRequest = (request, options = {}) => new Promise((resolve, reject) => {
  if (!window.cefQuery) {
    reject(new Error('Native bridge unavailable'));
    return;
  }

  window.cefQuery({
    request,
    onSuccess: (response) => {
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
