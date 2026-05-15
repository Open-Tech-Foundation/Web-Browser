#include "otf_page_policy.h"

namespace otf {

bool ShouldInjectPagePolicy(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string BuildPagePolicyScript() {
  static const std::string kScript = R"JS(
(() => {
  if (globalThis.__otfPagePolicyInjected) return;
  Object.defineProperty(globalThis, '__otfPagePolicyInjected', {
    value: true,
    configurable: false,
    enumerable: false,
    writable: false
  });

  const makeSecurityError = (message) => {
    try {
      return new DOMException(message, 'SecurityError');
    } catch (_) {
      const error = new Error(message);
      error.name = 'SecurityError';
      return error;
    }
  };

  const defineMethod = (target, name, value) => {
    try {
      Object.defineProperty(target, name, {
        value,
        configurable: false,
        enumerable: false,
        writable: false
      });
    } catch (_) {}
  };

  // WebGPU: block compute pipelines while leaving graphics pipelines intact.
  const installWebGPUComputePolicy = () => {
    const GPUDeviceCtor = globalThis.GPUDevice;
    if (!GPUDeviceCtor || !GPUDeviceCtor.prototype) return false;
    if (GPUDeviceCtor.prototype.__otfWebGPUComputePolicy) return true;

    const message = 'WebGPU compute pipelines are disabled by browser policy.';
    defineMethod(GPUDeviceCtor.prototype, 'createComputePipeline', () => {
      throw makeSecurityError(message);
    });
    defineMethod(GPUDeviceCtor.prototype, 'createComputePipelineAsync', () =>
      Promise.reject(makeSecurityError(message))
    );
    Object.defineProperty(GPUDeviceCtor.prototype, '__otfWebGPUComputePolicy', {
      value: true,
      configurable: false,
      enumerable: false,
      writable: false
    });
    return true;
  };

  if (!installWebGPUComputePolicy()) {
    const timer = setInterval(() => {
      if (installWebGPUComputePolicy()) clearInterval(timer);
    }, 50);
    setTimeout(() => clearInterval(timer), 5000);
  }

  // Canvas: protect readback/export surfaces used for fingerprinting.
  const noiseSeed = (() => {
    try {
      const values = new Uint32Array(1);
      globalThis.crypto.getRandomValues(values);
      return values[0] || Date.now();
    } catch (_) {
      return Date.now();
    }
  })();
  const noise = (index) => (Math.imul(index ^ noiseSeed, 1103515245) >>> 16) & 1;
  const perturbImageData = (imageData) => {
    try {
      const data = imageData && imageData.data;
      if (!data) return imageData;
      for (let i = 0; i < data.length; i += 64) {
        data[i] = Math.max(0, Math.min(255, data[i] + (noise(i) ? 1 : -1)));
      }
    } catch (_) {}
    return imageData;
  };

  const installCanvasPolicy = () => {
    const Canvas2D = globalThis.CanvasRenderingContext2D;
    let originalGetImageData = null;
    let originalPutImageData = null;
    if (Canvas2D && Canvas2D.prototype && !Canvas2D.prototype.__otfCanvasPolicy) {
      originalGetImageData = Canvas2D.prototype.getImageData;
      originalPutImageData = Canvas2D.prototype.putImageData;
      if (typeof originalGetImageData === 'function') {
        defineMethod(Canvas2D.prototype, 'getImageData', function(...args) {
          return perturbImageData(originalGetImageData.apply(this, args));
        });
      }
      Object.defineProperty(Canvas2D.prototype, '__otfCanvasPolicy', {
        value: true,
        configurable: false
      });
    }

    const Canvas = globalThis.HTMLCanvasElement;
    if (Canvas && Canvas.prototype && !Canvas.prototype.__otfCanvasPolicy) {
      const originalToDataURL = Canvas.prototype.toDataURL;
      const originalToBlob = Canvas.prototype.toBlob;

      const createProtectedCanvas = (canvas) => {
        try {
          if (!canvas || !canvas.width || !canvas.height ||
              typeof document === 'undefined' ||
              typeof originalGetImageData !== 'function' ||
              typeof originalPutImageData !== 'function') {
            return null;
          }
          const copy = document.createElement('canvas');
          copy.width = canvas.width;
          copy.height = canvas.height;
          const copyContext = copy.getContext('2d');
          if (!copyContext) return null;
          copyContext.drawImage(canvas, 0, 0);
          const imageData = originalGetImageData.call(
              copyContext, 0, 0, copy.width, copy.height);
          perturbImageData(imageData);
          originalPutImageData.call(copyContext, imageData, 0, 0);
          return copy;
        } catch (_) {
          return null;
        }
      };

      if (typeof originalToDataURL === 'function') {
        defineMethod(Canvas.prototype, 'toDataURL', function(...args) {
          const protectedCanvas = createProtectedCanvas(this);
          if (protectedCanvas) {
            return originalToDataURL.apply(protectedCanvas, args);
          }
          return originalToDataURL.apply(this, args);
        });
      }
      if (typeof originalToBlob === 'function') {
        defineMethod(Canvas.prototype, 'toBlob', function(...args) {
          const protectedCanvas = createProtectedCanvas(this);
          if (protectedCanvas) {
            return originalToBlob.apply(protectedCanvas, args);
          }
          return originalToBlob.apply(this, args);
        });
      }
      Object.defineProperty(Canvas.prototype, '__otfCanvasPolicy', {
        value: true,
        configurable: false
      });
    }
  };
  installCanvasPolicy();

  // WebGL: normalize common high-entropy fingerprint values.
  const installWebGLPolicyFor = (ctor) => {
    if (!ctor || !ctor.prototype || ctor.prototype.__otfWebGLPolicy) return;
    const originalGetParameter = ctor.prototype.getParameter;
    const originalGetExtension = ctor.prototype.getExtension;
    const originalGetSupportedExtensions = ctor.prototype.getSupportedExtensions;
    const originalReadPixels = ctor.prototype.readPixels;

    if (typeof originalGetParameter === 'function') {
      defineMethod(ctor.prototype, 'getParameter', function(parameter) {
        if (parameter === 37445) return 'OTF Browser';
        if (parameter === 37446) return 'OTF WebGL';
        return originalGetParameter.call(this, parameter);
      });
    }
    if (typeof originalGetExtension === 'function') {
      defineMethod(ctor.prototype, 'getExtension', function(name) {
        if (String(name).toLowerCase() === 'webgl_debug_renderer_info') {
          return null;
        }
        return originalGetExtension.call(this, name);
      });
    }
    if (typeof originalGetSupportedExtensions === 'function') {
      defineMethod(ctor.prototype, 'getSupportedExtensions', function() {
        const extensions = originalGetSupportedExtensions.call(this) || [];
        return extensions.filter((name) =>
          String(name).toLowerCase() !== 'webgl_debug_renderer_info'
        );
      });
    }
    if (typeof originalReadPixels === 'function') {
      defineMethod(ctor.prototype, 'readPixels', function(...args) {
        const result = originalReadPixels.apply(this, args);
        try {
          const pixels = args[6];
          if (pixels && typeof pixels.length === 'number') {
            for (let i = 0; i < pixels.length; i += 64) {
              pixels[i] = Math.max(0, Math.min(255, pixels[i] + (noise(i) ? 1 : -1)));
            }
          }
        } catch (_) {}
        return result;
      });
    }
    Object.defineProperty(ctor.prototype, '__otfWebGLPolicy', {
      value: true,
      configurable: false
    });
  };
  installWebGLPolicyFor(globalThis.WebGLRenderingContext);
  installWebGLPolicyFor(globalThis.WebGL2RenderingContext);

  // Workers: wrap constructor-created workers with the same policy bootstrap.
  const installWorkerPolicy = () => {
    const policySource = '(' + (() => {
      if (globalThis.__otfPagePolicyInjected) return;
      Object.defineProperty(globalThis, '__otfPagePolicyInjected', {
        value: true,
        configurable: false
      });
      const makeSecurityError = (message) => {
        try { return new DOMException(message, 'SecurityError'); }
        catch (_) { const error = new Error(message); error.name = 'SecurityError'; return error; }
      };
      const defineMethod = (target, name, value) => {
        try {
          Object.defineProperty(target, name, {
            value,
            configurable: false,
            enumerable: false,
            writable: false
          });
        } catch (_) {}
      };
      const installWebGPUComputePolicy = () => {
        const GPUDeviceCtor = globalThis.GPUDevice;
        if (!GPUDeviceCtor || !GPUDeviceCtor.prototype) return false;
        if (GPUDeviceCtor.prototype.__otfWebGPUComputePolicy) return true;
        const message = 'WebGPU compute pipelines are disabled by browser policy.';
        defineMethod(GPUDeviceCtor.prototype, 'createComputePipeline', () => {
          throw makeSecurityError(message);
        });
        defineMethod(GPUDeviceCtor.prototype, 'createComputePipelineAsync', () =>
          Promise.reject(makeSecurityError(message))
        );
        Object.defineProperty(GPUDeviceCtor.prototype, '__otfWebGPUComputePolicy', {
          value: true,
          configurable: false
        });
        return true;
      };
      if (!installWebGPUComputePolicy()) {
        const timer = setInterval(() => {
          if (installWebGPUComputePolicy()) clearInterval(timer);
        }, 50);
        setTimeout(() => clearInterval(timer), 5000);
      }

      const defineWebGLMethod = (target, name, value) => {
        try {
          Object.defineProperty(target, name, {
            value,
            configurable: false,
            enumerable: false,
            writable: false
          });
        } catch (_) {}
      };
      const installWebGLPolicyFor = (ctor) => {
        if (!ctor || !ctor.prototype || ctor.prototype.__otfWebGLPolicy) return;
        const originalGetParameter = ctor.prototype.getParameter;
        const originalGetExtension = ctor.prototype.getExtension;
        const originalGetSupportedExtensions = ctor.prototype.getSupportedExtensions;
        if (typeof originalGetParameter === 'function') {
          defineWebGLMethod(ctor.prototype, 'getParameter', function(parameter) {
            if (parameter === 37445) return 'OTF Browser';
            if (parameter === 37446) return 'OTF WebGL';
            return originalGetParameter.call(this, parameter);
          });
        }
        if (typeof originalGetExtension === 'function') {
          defineWebGLMethod(ctor.prototype, 'getExtension', function(name) {
            if (String(name).toLowerCase() === 'webgl_debug_renderer_info') {
              return null;
            }
            return originalGetExtension.call(this, name);
          });
        }
        if (typeof originalGetSupportedExtensions === 'function') {
          defineWebGLMethod(ctor.prototype, 'getSupportedExtensions', function() {
            const extensions = originalGetSupportedExtensions.call(this) || [];
            return extensions.filter((name) =>
              String(name).toLowerCase() !== 'webgl_debug_renderer_info'
            );
          });
        }
        Object.defineProperty(ctor.prototype, '__otfWebGLPolicy', {
          value: true,
          configurable: false
        });
      };
      installWebGLPolicyFor(globalThis.WebGLRenderingContext);
      installWebGLPolicyFor(globalThis.WebGL2RenderingContext);
    }).toString() + ')();';

    const wrapWorkerCtor = (name) => {
      const OriginalWorker = globalThis[name];
      if (typeof OriginalWorker !== 'function' || OriginalWorker.__otfWorkerPolicy) return;
      const WrappedWorker = function(scriptURL, options) {
        const originalUrl = new URL(String(scriptURL), globalThis.location.href).href;
        const isModule = !!(options && typeof options === 'object' && options.type === 'module');
        const importSource = isModule
          ? 'import ' + JSON.stringify(originalUrl) + ';\n'
          : 'importScripts(' + JSON.stringify(originalUrl) + ');\n';
        const bootstrap = new Blob([
          policySource,
          '\n',
          importSource
        ], { type: 'application/javascript' });
        const bootstrapUrl = URL.createObjectURL(bootstrap);
        try {
          return new OriginalWorker(bootstrapUrl, options);
        } finally {
          setTimeout(() => URL.revokeObjectURL(bootstrapUrl), 60000);
        }
      };
      WrappedWorker.prototype = OriginalWorker.prototype;
      Object.setPrototypeOf(WrappedWorker, OriginalWorker);
      Object.defineProperty(WrappedWorker, '__otfWorkerPolicy', { value: true });
      try { globalThis[name] = WrappedWorker; } catch (_) {}
    };

    wrapWorkerCtor('Worker');
    wrapWorkerCtor('SharedWorker');
  };
  installWorkerPolicy();
})();
)JS";
  return kScript;
}

}  // namespace otf
