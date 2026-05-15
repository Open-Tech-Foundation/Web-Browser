#include "otf_page_policy.h"

#include "include/views/cef_display.h"
#include "otf_utils.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace otf {
namespace {

struct ScreenProfile {
  const char* id;
  int width;
  int height;
  int avail_height;
  double dpr;
};

const ScreenProfile kScreenProfiles[] = {
    {"small-laptop", 1366, 768, 728, 1.0},
    {"desktop-laptop", 1440, 900, 860, 1.0},
    {"desktop-1080p", 1920, 1080, 1040, 1.0},
    {"hidpi-1080p", 1920, 1080, 1040, 2.0},
    {"ultrawide-1080p", 2560, 1080, 1040, 1.0},
};

std::string GetFingerprintProfileFilePath() {
  const std::string settings_path = GetSettingsFilePath();
  if (settings_path.empty()) {
    return "";
  }
  return (std::filesystem::path(settings_path).parent_path() /
          "fingerprint_profile").string();
}

const ScreenProfile* FindScreenProfileById(const std::string& id) {
  for (const auto& profile : kScreenProfiles) {
    if (id == profile.id) {
      return &profile;
    }
  }
  return nullptr;
}

std::optional<std::string> LoadPersistedScreenProfileId() {
  const std::string path = GetFingerprintProfileFilePath();
  if (path.empty()) {
    return std::nullopt;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    return std::nullopt;
  }

  std::string id;
  std::getline(input, id);
  if (!FindScreenProfileById(id)) {
    return std::nullopt;
  }
  return id;
}

void SavePersistedScreenProfileId(const std::string& id) {
  const std::string path = GetFingerprintProfileFilePath();
  if (path.empty()) {
    return;
  }

  std::ofstream output(path, std::ios::trunc);
  if (output.is_open()) {
    output << id;
  }
}

const ScreenProfile& ChooseNearestScreenProfile() {
  int width = 1920;
  int height = 1080;
  double dpr = 1.0;

  CefRefPtr<CefDisplay> display = CefDisplay::GetPrimaryDisplay();
  if (display) {
    const CefRect bounds = display->GetBounds();
    if (bounds.width > 0 && bounds.height > 0) {
      width = bounds.width;
      height = bounds.height;
    }
    const float scale = display->GetDeviceScaleFactor();
    if (scale > 0.0f) {
      dpr = scale;
    }
  }

  const ScreenProfile* best = &kScreenProfiles[0];
  double best_score = std::numeric_limits<double>::max();
  for (const auto& profile : kScreenProfiles) {
    const double width_delta =
        static_cast<double>(width - profile.width) / profile.width;
    const double height_delta =
        static_cast<double>(height - profile.height) / profile.height;
    const double dpr_delta = dpr - profile.dpr;
    const double score = (width_delta * width_delta) +
                         (height_delta * height_delta) +
                         (dpr_delta * dpr_delta * 0.35);
    if (score < best_score) {
      best = &profile;
      best_score = score;
    }
  }
  return *best;
}

const ScreenProfile& GetStableScreenProfile() {
  static const ScreenProfile* profile = []() {
    if (const auto persisted_id = LoadPersistedScreenProfileId()) {
      if (const ScreenProfile* persisted =
              FindScreenProfileById(*persisted_id)) {
        return persisted;
      }
    }

    const ScreenProfile& chosen = ChooseNearestScreenProfile();
    SavePersistedScreenProfileId(chosen.id);
    return &chosen;
  }();
  return *profile;
}

std::string BuildScreenProfileJson(const ScreenProfile& profile) {
  std::ostringstream json;
  json << "{"
       << "\"id\":" << JsonString(profile.id) << ","
       << "\"width\":" << profile.width << ","
       << "\"height\":" << profile.height << ","
       << "\"availWidth\":" << profile.width << ","
       << "\"availHeight\":" << profile.avail_height << ","
       << "\"colorDepth\":24,"
       << "\"pixelDepth\":24,"
       << "\"devicePixelRatio\":" << profile.dpr
       << "}";
  return json.str();
}

void ReplaceAll(std::string& value,
                const std::string& needle,
                const std::string& replacement) {
  size_t pos = 0;
  while ((pos = value.find(needle, pos)) != std::string::npos) {
    value.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

}  // namespace

bool ShouldInjectPagePolicy(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string BuildPagePolicyScript() {
  std::string script = R"JS(
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

  // Screen: expose a stable common profile instead of exact device dimensions.
  const screenProfile = __OTF_SCREEN_PROFILE__;
  const defineGetter = (target, name, value) => {
    try {
      Object.defineProperty(target, name, {
        get: () => value,
        configurable: false,
        enumerable: true
      });
    } catch (_) {}
  };
  const installScreenPolicy = () => {
    try {
      const ScreenCtor = globalThis.Screen;
      if (ScreenCtor && ScreenCtor.prototype && !ScreenCtor.prototype.__otfScreenPolicy) {
        defineGetter(ScreenCtor.prototype, 'width', screenProfile.width);
        defineGetter(ScreenCtor.prototype, 'height', screenProfile.height);
        defineGetter(ScreenCtor.prototype, 'availWidth', screenProfile.availWidth);
        defineGetter(ScreenCtor.prototype, 'availHeight', screenProfile.availHeight);
        defineGetter(ScreenCtor.prototype, 'colorDepth', screenProfile.colorDepth);
        defineGetter(ScreenCtor.prototype, 'pixelDepth', screenProfile.pixelDepth);
        Object.defineProperty(ScreenCtor.prototype, '__otfScreenPolicy', {
          value: true,
          configurable: false,
          enumerable: false
        });
      }
      if (!globalThis.__otfDevicePixelRatioPolicy) {
        defineGetter(globalThis, 'devicePixelRatio', screenProfile.devicePixelRatio);
        Object.defineProperty(globalThis, '__otfDevicePixelRatioPolicy', {
          value: true,
          configurable: false,
          enumerable: false
        });
      }
      Object.defineProperty(globalThis, '__otfScreenProfile', {
        value: Object.freeze({ ...screenProfile }),
        configurable: false,
        enumerable: false,
        writable: false
      });
    } catch (_) {}
  };
  installScreenPolicy();

  // Hardware: normalize CPU and memory signals to a common desktop profile.
  const installHardwarePolicy = () => {
    try {
      const NavigatorCtor = globalThis.Navigator;
      if (NavigatorCtor && NavigatorCtor.prototype &&
          !NavigatorCtor.prototype.__otfHardwarePolicy) {
        defineGetter(NavigatorCtor.prototype, 'hardwareConcurrency', 4);
        defineGetter(NavigatorCtor.prototype, 'deviceMemory', 4);
        Object.defineProperty(NavigatorCtor.prototype, '__otfHardwarePolicy', {
          value: true,
          configurable: false,
          enumerable: false
        });
      }
      Object.defineProperty(globalThis, '__otfHardwareProfile', {
        value: Object.freeze({
          hardwareConcurrency: 4,
          deviceMemory: 4
        }),
        configurable: false,
        enumerable: false,
        writable: false
      });
    } catch (_) {}
  };
  installHardwarePolicy();

  // Fonts: reduce local font probing to a small common anonymity set.
  const allowedFontNames = [
    'arial',
    'helvetica',
    'times new roman',
    'courier new',
    'serif',
    'sans-serif',
    'monospace'
  ];
  const extractPrimaryFontFamily = (font) => {
    const value = String(font || '');
    const familyMatch = value.match(/(?:\d+(?:\.\d+)?(?:px|pt|em|rem|%)?(?:\/[^\s]+)?\s+)(.+)$/i);
    const familyList = familyMatch ? familyMatch[1] : value;
    const firstFamily = familyList.split(',')[0] || '';
    return firstFamily.trim().replace(/^["']|["']$/g, '').toLowerCase();
  };
  const fontUsesAllowedFamily = (font) => {
    const primaryFamily = extractPrimaryFontFamily(font);
    return allowedFontNames.includes(primaryFamily);
  };
  const normalizeFontForMeasurement = (font) => {
    const value = String(font || '10px sans-serif');
    if (fontUsesAllowedFamily(value)) return value;
    const replaced = value.replace(/((?:\d+(?:\.\d+)?(?:px|pt|em|rem|%)?(?:\/[^\s]+)?\s+))(.+)$/i, '$1Arial');
    return replaced === value ? '10px Arial' : replaced;
  };
  const installFontPolicy = () => {
    try {
      const FontFaceSetCtor = globalThis.FontFaceSet;
      if (FontFaceSetCtor && FontFaceSetCtor.prototype &&
          !FontFaceSetCtor.prototype.__otfFontPolicy) {
        const originalCheck = FontFaceSetCtor.prototype.check;
        const originalLoad = FontFaceSetCtor.prototype.load;
        const originalForEach = FontFaceSetCtor.prototype.forEach;
        const emptyFontIterator = function() {
          return [][Symbol.iterator]();
        };
        if (typeof originalCheck === 'function') {
          defineMethod(FontFaceSetCtor.prototype, 'check', function(font, ...args) {
            return fontUsesAllowedFamily(font) && originalCheck.call(this, font, ...args);
          });
        }
        if (typeof originalLoad === 'function') {
          defineMethod(FontFaceSetCtor.prototype, 'load', function(font, ...args) {
            if (!fontUsesAllowedFamily(font)) {
              return Promise.resolve([]);
            }
            return originalLoad.call(this, font, ...args);
          });
        }
        if (typeof originalForEach === 'function') {
          defineMethod(FontFaceSetCtor.prototype, 'forEach', function(callback, thisArg) {
            if (typeof callback !== 'function') return undefined;
            return originalForEach.call(this, (fontFace, key, set) => {
              try {
                const family = fontFace && fontFace.family;
                if (fontUsesAllowedFamily(family)) {
                  callback.call(thisArg, fontFace, key, set);
                }
              } catch (_) {}
            });
          });
        }
        defineMethod(FontFaceSetCtor.prototype, 'entries', emptyFontIterator);
        defineMethod(FontFaceSetCtor.prototype, 'keys', emptyFontIterator);
        defineMethod(FontFaceSetCtor.prototype, 'values', emptyFontIterator);
        if (typeof Symbol !== 'undefined' && Symbol.iterator) {
          defineMethod(FontFaceSetCtor.prototype, Symbol.iterator, emptyFontIterator);
        }
        defineGetter(FontFaceSetCtor.prototype, 'size', 4);
        Object.defineProperty(FontFaceSetCtor.prototype, '__otfFontPolicy', {
          value: true,
          configurable: false,
          enumerable: false
        });
      }
      const fontSet = globalThis.document && globalThis.document.fonts;
      if (fontSet && !fontSet.__otfFontPolicy) {
        defineMethod(fontSet, 'check', function(font, ...args) {
          return fontUsesAllowedFamily(font);
        });
        defineMethod(fontSet, 'load', function(font, ...args) {
          if (!fontUsesAllowedFamily(font)) {
            return Promise.resolve([]);
          }
          const load = FontFaceSetCtor && FontFaceSetCtor.prototype &&
              FontFaceSetCtor.prototype.load;
          return typeof load === 'function' ? load.call(this, font, ...args) : Promise.resolve([]);
        });
        const emptyFontIterator = function() {
          return [][Symbol.iterator]();
        };
        defineMethod(fontSet, 'forEach', function() {});
        defineMethod(fontSet, 'entries', emptyFontIterator);
        defineMethod(fontSet, 'keys', emptyFontIterator);
        defineMethod(fontSet, 'values', emptyFontIterator);
        if (typeof Symbol !== 'undefined' && Symbol.iterator) {
          defineMethod(fontSet, Symbol.iterator, emptyFontIterator);
        }
        defineGetter(fontSet, 'size', 4);
        Object.defineProperty(fontSet, '__otfFontPolicy', {
          value: true,
          configurable: false,
          enumerable: false
        });
      }
      Object.defineProperty(globalThis, '__otfFontProfile', {
        value: Object.freeze({
          allowedFonts: ['Arial', 'Helvetica', 'Times New Roman', 'Courier New']
        }),
        configurable: false,
        enumerable: false,
        writable: false
      });
    } catch (_) {}
  };
  installFontPolicy();

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
      const originalMeasureText = Canvas2D.prototype.measureText;
      if (typeof originalGetImageData === 'function') {
        defineMethod(Canvas2D.prototype, 'getImageData', function(...args) {
          return perturbImageData(originalGetImageData.apply(this, args));
        });
      }
      if (typeof originalMeasureText === 'function') {
        defineMethod(Canvas2D.prototype, 'measureText', function(...args) {
          const currentFont = this.font;
          if (fontUsesAllowedFamily(currentFont)) {
            return originalMeasureText.apply(this, args);
          }
          try {
            this.font = normalizeFontForMeasurement(currentFont);
            return originalMeasureText.apply(this, args);
          } finally {
            try { this.font = currentFont; } catch (_) {}
          }
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
  const makeRange = (first, second) => {
    try {
      return new Int32Array([first, second]);
    } catch (_) {
      return [first, second];
    }
  };

  const webGLParameterProfile = new Map([
    [7936, 'WebKit'],
    [7937, 'WebKit WebGL'],
    [3386, makeRange(8192, 8192)],
    [3379, 8192],
    [34076, 8192],
    [34024, 8192],
    [33901, makeRange(1, 64)],
    [33902, makeRange(1, 1)],
    [34921, 16],
    [36347, 256],
    [35660, 16],
    [36348, 16],
    [36349, 256],
    [34930, 16],
    [35661, 32],
    [35658, 1024],
    [35657, 1024],
    [35371, 12],
    [35373, 12],
    [35374, 24],
    [35375, 36],
    [35376, 16384],
    [35377, 65536],
    [35379, 65536],
    [35380, 256],
    [35659, 64],
    [37154, 64],
    [37157, 64],
    [35978, 64],
    [35979, 4],
    [35968, 4],
    [35076, -8],
    [35077, 7],
    [34852, 4],
    [36063, 4],
    [36183, 4],
    [32883, 2048],
    [35071, 256],
    [34045, 2]
  ]);

  const blockedWebGLExtensions = new Set([
    'ext_disjoint_timer_query',
    'ext_disjoint_timer_query_webgl2',
    'ext_texture_filter_anisotropic',
    'webkit_ext_texture_filter_anisotropic',
    'moz_ext_texture_filter_anisotropic',
    'khr_parallel_shader_compile',
    'webgl_debug_renderer_info',
    'webgl_debug_shaders'
  ]);

  const installWebGLPolicyFor = (ctor) => {
    if (!ctor || !ctor.prototype || ctor.prototype.__otfWebGLPolicy) return;
    const isWebGL2 = ctor === globalThis.WebGL2RenderingContext;
    const originalGetParameter = ctor.prototype.getParameter;
    const originalGetExtension = ctor.prototype.getExtension;
    const originalGetSupportedExtensions = ctor.prototype.getSupportedExtensions;
    const originalGetShaderPrecisionFormat = ctor.prototype.getShaderPrecisionFormat;
    const originalReadPixels = ctor.prototype.readPixels;

    if (typeof originalGetParameter === 'function') {
      defineMethod(ctor.prototype, 'getParameter', function(parameter) {
        if (parameter === 7938) return isWebGL2 ? 'WebGL 2.0' : 'WebGL 1.0';
        if (parameter === 35724) {
          return isWebGL2 ? 'WebGL GLSL ES 3.00' : 'WebGL GLSL ES 1.0';
        }
        if (parameter === 37445) return 'OTF Browser';
        if (parameter === 37446) return 'OTF WebGL';
        if (webGLParameterProfile.has(parameter)) {
          return webGLParameterProfile.get(parameter);
        }
        return originalGetParameter.call(this, parameter);
      });
    }
    if (typeof originalGetExtension === 'function') {
      defineMethod(ctor.prototype, 'getExtension', function(name) {
        if (blockedWebGLExtensions.has(String(name).toLowerCase())) {
          return null;
        }
        return originalGetExtension.call(this, name);
      });
    }
    if (typeof originalGetSupportedExtensions === 'function') {
      defineMethod(ctor.prototype, 'getSupportedExtensions', function() {
        const extensions = originalGetSupportedExtensions.call(this) || [];
        return extensions.filter((name) =>
          !blockedWebGLExtensions.has(String(name).toLowerCase())
        );
      });
    }
    if (typeof originalGetShaderPrecisionFormat === 'function') {
      defineMethod(ctor.prototype, 'getShaderPrecisionFormat', function(...args) {
        const result = originalGetShaderPrecisionFormat.apply(this, args);
        if (!result) return result;
        try {
          return {
            rangeMin: 127,
            rangeMax: 127,
            precision: 23
          };
        } catch (_) {
          return result;
        }
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
      const screenProfile = __OTF_SCREEN_PROFILE__;
      const defineGetter = (target, name, value) => {
        try {
          Object.defineProperty(target, name, {
            get: () => value,
            configurable: false,
            enumerable: true
          });
        } catch (_) {}
      };
      const installScreenPolicy = () => {
        try {
          const ScreenCtor = globalThis.Screen;
          if (ScreenCtor && ScreenCtor.prototype && !ScreenCtor.prototype.__otfScreenPolicy) {
            defineGetter(ScreenCtor.prototype, 'width', screenProfile.width);
            defineGetter(ScreenCtor.prototype, 'height', screenProfile.height);
            defineGetter(ScreenCtor.prototype, 'availWidth', screenProfile.availWidth);
            defineGetter(ScreenCtor.prototype, 'availHeight', screenProfile.availHeight);
            defineGetter(ScreenCtor.prototype, 'colorDepth', screenProfile.colorDepth);
            defineGetter(ScreenCtor.prototype, 'pixelDepth', screenProfile.pixelDepth);
            Object.defineProperty(ScreenCtor.prototype, '__otfScreenPolicy', {
              value: true,
              configurable: false
            });
          }
          if (!globalThis.__otfDevicePixelRatioPolicy) {
            defineGetter(globalThis, 'devicePixelRatio', screenProfile.devicePixelRatio);
            Object.defineProperty(globalThis, '__otfDevicePixelRatioPolicy', {
              value: true,
              configurable: false
            });
          }
          Object.defineProperty(globalThis, '__otfScreenProfile', {
            value: Object.freeze({ ...screenProfile }),
            configurable: false
          });
        } catch (_) {}
      };
      installScreenPolicy();
      const installHardwarePolicy = () => {
        try {
          const NavigatorCtor = globalThis.Navigator;
          if (NavigatorCtor && NavigatorCtor.prototype &&
              !NavigatorCtor.prototype.__otfHardwarePolicy) {
            defineGetter(NavigatorCtor.prototype, 'hardwareConcurrency', 4);
            defineGetter(NavigatorCtor.prototype, 'deviceMemory', 4);
            Object.defineProperty(NavigatorCtor.prototype, '__otfHardwarePolicy', {
              value: true,
              configurable: false
            });
          }
          Object.defineProperty(globalThis, '__otfHardwareProfile', {
            value: Object.freeze({
              hardwareConcurrency: 4,
              deviceMemory: 4
            }),
            configurable: false
          });
        } catch (_) {}
      };
      installHardwarePolicy();
      const allowedFontNames = [
        'arial',
        'helvetica',
        'times new roman',
        'courier new',
        'serif',
        'sans-serif',
        'monospace'
      ];
      const extractPrimaryFontFamily = (font) => {
        const value = String(font || '');
        const familyMatch = value.match(/(?:\d+(?:\.\d+)?(?:px|pt|em|rem|%)?(?:\/[^\s]+)?\s+)(.+)$/i);
        const familyList = familyMatch ? familyMatch[1] : value;
        const firstFamily = familyList.split(',')[0] || '';
        return firstFamily.trim().replace(/^["']|["']$/g, '').toLowerCase();
      };
      const fontUsesAllowedFamily = (font) => {
        const primaryFamily = extractPrimaryFontFamily(font);
        return allowedFontNames.includes(primaryFamily);
      };
      const normalizeFontForMeasurement = (font) => {
        const value = String(font || '10px sans-serif');
        if (fontUsesAllowedFamily(value)) return value;
        const replaced = value.replace(/((?:\d+(?:\.\d+)?(?:px|pt|em|rem|%)?(?:\/[^\s]+)?\s+))(.+)$/i, '$1Arial');
        return replaced === value ? '10px Arial' : replaced;
      };
      const installFontPolicy = () => {
        try {
          const FontFaceSetCtor = globalThis.FontFaceSet;
          if (FontFaceSetCtor && FontFaceSetCtor.prototype &&
              !FontFaceSetCtor.prototype.__otfFontPolicy) {
            const originalCheck = FontFaceSetCtor.prototype.check;
            const originalLoad = FontFaceSetCtor.prototype.load;
            const emptyFontIterator = function() {
              return [][Symbol.iterator]();
            };
            if (typeof originalCheck === 'function') {
              defineMethod(FontFaceSetCtor.prototype, 'check', function(font, ...args) {
                return fontUsesAllowedFamily(font) && originalCheck.call(this, font, ...args);
              });
            }
            if (typeof originalLoad === 'function') {
              defineMethod(FontFaceSetCtor.prototype, 'load', function(font, ...args) {
                if (!fontUsesAllowedFamily(font)) {
                  return Promise.resolve([]);
                }
                return originalLoad.call(this, font, ...args);
              });
            }
            defineMethod(FontFaceSetCtor.prototype, 'entries', emptyFontIterator);
            defineMethod(FontFaceSetCtor.prototype, 'keys', emptyFontIterator);
            defineMethod(FontFaceSetCtor.prototype, 'values', emptyFontIterator);
            if (typeof Symbol !== 'undefined' && Symbol.iterator) {
              defineMethod(FontFaceSetCtor.prototype, Symbol.iterator, emptyFontIterator);
            }
            defineGetter(FontFaceSetCtor.prototype, 'size', 4);
            Object.defineProperty(FontFaceSetCtor.prototype, '__otfFontPolicy', {
              value: true,
              configurable: false
            });
          }
          Object.defineProperty(globalThis, '__otfFontProfile', {
            value: Object.freeze({
              allowedFonts: ['Arial', 'Helvetica', 'Times New Roman', 'Courier New']
            }),
            configurable: false
          });
        } catch (_) {}
      };
      installFontPolicy();
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
      const makeRange = (first, second) => {
        try { return new Int32Array([first, second]); }
        catch (_) { return [first, second]; }
      };
      const webGLParameterProfile = new Map([
        [7936, 'WebKit'], [7937, 'WebKit WebGL'], [3386, makeRange(8192, 8192)],
        [3379, 8192], [34076, 8192], [34024, 8192],
        [33901, makeRange(1, 64)], [33902, makeRange(1, 1)],
        [34921, 16], [36347, 256], [35660, 16], [36348, 16],
        [36349, 256], [34930, 16], [35661, 32], [35658, 1024],
        [35657, 1024], [35371, 12], [35373, 12], [35374, 24],
        [35375, 36], [35376, 16384], [35377, 65536], [35379, 65536],
        [35380, 256], [35659, 64], [37154, 64], [37157, 64],
        [35978, 64], [35979, 4], [35968, 4], [35076, -8],
        [35077, 7], [34852, 4], [36063, 4], [36183, 4],
        [32883, 2048], [35071, 256], [34045, 2]
      ]);
      const blockedWebGLExtensions = new Set([
        'ext_disjoint_timer_query',
        'ext_disjoint_timer_query_webgl2',
        'ext_texture_filter_anisotropic',
        'webkit_ext_texture_filter_anisotropic',
        'moz_ext_texture_filter_anisotropic',
        'khr_parallel_shader_compile',
        'webgl_debug_renderer_info',
        'webgl_debug_shaders'
      ]);
      const installWebGLPolicyFor = (ctor) => {
        if (!ctor || !ctor.prototype || ctor.prototype.__otfWebGLPolicy) return;
        const isWebGL2 = ctor === globalThis.WebGL2RenderingContext;
        const originalGetParameter = ctor.prototype.getParameter;
        const originalGetExtension = ctor.prototype.getExtension;
        const originalGetSupportedExtensions = ctor.prototype.getSupportedExtensions;
        const originalGetShaderPrecisionFormat = ctor.prototype.getShaderPrecisionFormat;
        if (typeof originalGetParameter === 'function') {
          defineWebGLMethod(ctor.prototype, 'getParameter', function(parameter) {
            if (parameter === 7938) return isWebGL2 ? 'WebGL 2.0' : 'WebGL 1.0';
            if (parameter === 35724) {
              return isWebGL2 ? 'WebGL GLSL ES 3.00' : 'WebGL GLSL ES 1.0';
            }
            if (parameter === 37445) return 'OTF Browser';
            if (parameter === 37446) return 'OTF WebGL';
            if (webGLParameterProfile.has(parameter)) {
              return webGLParameterProfile.get(parameter);
            }
            return originalGetParameter.call(this, parameter);
          });
        }
        if (typeof originalGetExtension === 'function') {
          defineWebGLMethod(ctor.prototype, 'getExtension', function(name) {
            if (blockedWebGLExtensions.has(String(name).toLowerCase())) {
              return null;
            }
            return originalGetExtension.call(this, name);
          });
        }
        if (typeof originalGetSupportedExtensions === 'function') {
          defineWebGLMethod(ctor.prototype, 'getSupportedExtensions', function() {
            const extensions = originalGetSupportedExtensions.call(this) || [];
            return extensions.filter((name) =>
              !blockedWebGLExtensions.has(String(name).toLowerCase())
            );
          });
        }
        if (typeof originalGetShaderPrecisionFormat === 'function') {
          defineWebGLMethod(ctor.prototype, 'getShaderPrecisionFormat', function(...args) {
            const result = originalGetShaderPrecisionFormat.apply(this, args);
            return result ? { rangeMin: 127, rangeMax: 127, precision: 23 } : result;
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
  ReplaceAll(script, "__OTF_SCREEN_PROFILE__",
             BuildScreenProfileJson(GetStableScreenProfile()));
  return script;
}

}  // namespace otf
