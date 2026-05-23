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

std::string ResolveScreenProfileJson() {
  // Main-process-only. Reads ~/.otf-browser/fingerprint_profile (or picks
  // the nearest match for the primary display) and serializes the result
  // so the renderer can be handed a static JSON blob without ever touching
  // the filesystem itself.
  return BuildScreenProfileJson(GetStableScreenProfile());
}

std::string BuildPagePolicyScript(const std::string& screen_profile_json) {
  std::string script = R"JS(
(() => {
  // Defined as a function so we can both invoke it in this realm and stringify
  // it (Function.prototype.toString) to bootstrap the same policy inside any
  // worker realms we create below. Single source of truth — no JS fork.
  function applyPagePolicy() {
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
  // In worker realms globalThis.Navigator is undefined; fall back to the
  // prototype of the live navigator object (WorkerNavigator.prototype).
  const installHardwarePolicy = () => {
    try {
      const NavigatorCtor = globalThis.Navigator;
      const proto = NavigatorCtor
        ? NavigatorCtor.prototype
        : Object.getPrototypeOf(navigator);
      if (proto && !proto.__otfHardwarePolicy) {
        defineGetter(proto, 'hardwareConcurrency', 4);
        defineGetter(proto, 'deviceMemory', 4);
        Object.defineProperty(proto, '__otfHardwarePolicy', {
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

  // ===== shared native-toString plumbing — include ONCE, before all overrides =====
  const makeNative = (() => {
    const nativeToString = Function.prototype.toString;
    const fakeSources = new WeakMap();

    const patchedToString = function toString() {
      if (fakeSources.has(this)) return fakeSources.get(this);
      return nativeToString.call(this);
    };
    // the patched toString must itself look native
    fakeSources.set(patchedToString, 'function toString() { [native code] }');

    Object.defineProperty(Function.prototype, 'toString', {
      value: patchedToString,
      writable: true,
      enumerable: false,
      configurable: true,
    });

    // register a function so its .toString() reports a native signature
    return function makeNative(fn, signature) {
      fakeSources.set(fn, signature);
      return fn;
    };
  })();

  (() => {
    'use strict';

    if (typeof Navigator === 'undefined') return;

    const SPOOFED_PLATFORM = 'Linux x86_64';

    const proto = Navigator.prototype;
    const original = Object.getOwnPropertyDescriptor(proto, 'platform');
    if (!original || typeof original.get !== 'function') return;

    const getter = makeNative(
      function platform() {
        return SPOOFED_PLATFORM;
      },
      'function get platform() { [native code] }'
    );

    // Install the getter, mirroring the original descriptor flags
    Object.defineProperty(proto, 'platform', {
      get: getter,
      set: original.set,
      enumerable: original.enumerable,
      configurable: original.configurable,
    });
  })();

  // Storage estimate: spoof ~180 GiB quota with 50 MiB usage.
  (() => {
    'use strict';

    if (!navigator.storage || typeof navigator.storage.estimate !== 'function') return;

    const STD_QUOTA = 193273528320; // ~180 GiB
    const STD_USAGE = 52428800;     // 50 MiB

    const proto = Object.getPrototypeOf(navigator.storage);
    const original = Object.getOwnPropertyDescriptor(proto, 'estimate');
    if (!original || typeof original.value !== 'function') return;

    const estimate = makeNative(
      function estimate() {
        return Promise.resolve({
          quota: STD_QUOTA,
          usage: STD_USAGE,
          usageDetails: {},
        });
      },
      'function estimate() { [native code] }'
    );

    Object.defineProperty(proto, 'estimate', {
      value: estimate,
      writable: original.writable,
      enumerable: original.enumerable,
      configurable: original.configurable,
    });
  })();

  (() => {
  'use strict';

  // --- per-session+origin seed -------------------------------------------
  // Stable within a session (survives "render twice and compare"),
  // different across sessions and across origins (no cross-site linking).
  const sessionSeed = (() => {
    // one random 32-bit value generated once per page-context
    const a = new Uint32Array(1);
    crypto.getRandomValues(a);
    return a[0];
  })();

  const hashStr = (str, seed) => {
    let h = seed >>> 0;
    for (let i = 0; i < str.length; i++) {
      h = Math.imul(h ^ str.charCodeAt(i), 0x01000193) >>> 0;
    }
    return h >>> 0;
  };

  const originSeed = hashStr(location.origin, sessionSeed);

  // Small, deterministic PRNG (mulberry32) seeded per origin.
  const makeRng = (seed) => {
    let s = seed >>> 0;
    return () => {
      s = (s + 0x6D2B79F5) >>> 0;
      let t = s;
      t = Math.imul(t ^ (t >>> 15), t | 1);
      t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  };

  // --- perturbation -------------------------------------------------------
  // Amplitude far below anything audible/functional; only the
  // least-significant precision (the fingerprint) is disturbed.
  const NOISE = 1e-7;

  const perturb = (data) => {
    // Re-seed per call FROM the origin seed so the SAME buffer contents
    // get the SAME perturbation within a session — deterministic, not
    // fresh-random each read.
    const rng = makeRng(originSeed ^ (data.length >>> 0));
    for (let i = 0; i < data.length; i++) {
      data[i] = data[i] + (rng() * 2 - 1) * NOISE;
    }
    return data;
  };

  // --- patch the read-back surfaces --------------------------------------
  const AB = (typeof AudioBuffer !== 'undefined') && AudioBuffer.prototype;
  if (AB) {
    const origGetChannelData = AB.getChannelData;
    AB.getChannelData = function getChannelData(channel) {
      const data = origGetChannelData.call(this, channel);
      return perturb(data);
    };

    const origCopyFromChannel = AB.copyFromChannel;
    if (origCopyFromChannel) {
      AB.copyFromChannel = function copyFromChannel(dest, channel, start) {
        origCopyFromChannel.call(this, dest, channel, start);
        perturb(dest);
      };
    }
  }

  const AN = (typeof AnalyserNode !== 'undefined') && AnalyserNode.prototype;
  if (AN) {
    const origFloat = AN.getFloatFrequencyData;
    AN.getFloatFrequencyData = function getFloatFrequencyData(array) {
      origFloat.call(this, array);
      perturb(array);
    };

    const origByte = AN.getByteFrequencyData;
    AN.getByteFrequencyData = function getByteFrequencyData(array) {
      origByte.call(this, array);
      // byte data is 0–255 ints; nudge by at most ±1 occasionally so we
      // don't distort visualizers but still break exact-match fingerprints
      const rng = makeRng(originSeed ^ (array.length >>> 0));
      for (let i = 0; i < array.length; i++) {
        if (rng() < 0.5) {
          const d = rng() < 0.5 ? -1 : 1;
          const v = array[i] + d;
          if (v >= 0 && v <= 255) array[i] = v;
        }
      }
      return array;
    };
  }
})();

  // Layout metrics: normalize font probes with stable per-session noise.
  const layoutNoiseSeed = (() => {
    try {
      const values = new Uint32Array(1);
      globalThis.crypto.getRandomValues(values);
      return values[0] || Date.now();
    } catch (_) {
      return Date.now();
    }
  })();
  const layoutNoiseValue = (key) => {
    const text = String(key || '');
    let value = layoutNoiseSeed >>> 0;
    for (let i = 0; i < text.length; i += 1) {
      value = Math.imul(value ^ text.charCodeAt(i), 16777619) >>> 0;
    }
    value = Math.imul(value ^ (value >>> 16), 2246822507) >>> 0;
    return (((value & 1023) / 1023) - 0.5) * 0.25;
  };
  const makeDOMRect = (x, y, width, height) => {
    const safeWidth = Math.max(0, width);
    const safeHeight = Math.max(0, height);
    if (typeof DOMRect === 'function') {
      return new DOMRect(x, y, safeWidth, safeHeight);
    }
    return {
      x,
      y,
      width: safeWidth,
      height: safeHeight,
      left: x,
      top: y,
      right: x + safeWidth,
      bottom: y + safeHeight,
      toJSON() {
        return {
          x: this.x,
          y: this.y,
          width: this.width,
          height: this.height,
          left: this.left,
          top: this.top,
          right: this.right,
          bottom: this.bottom
        };
      }
    };
  };
  const makeProtectedDOMRect = (rect, key = 'default') => {
    const dx = layoutNoiseValue(key + ':x');
    const dy = layoutNoiseValue(key + ':y');
    const dw = layoutNoiseValue(key + ':w');
    const dh = layoutNoiseValue(key + ':h');
    const x = rect.x + dx;
    const y = rect.y + dy;
    return makeDOMRect(x, y, rect.width + dw, rect.height + dh);
  };
  const measureWithFallbackFont = (element, rect) => {
    try {
      if (!element || !globalThis.document || !globalThis.getComputedStyle) {
        return null;
      }
      const style = getComputedStyle(element);
      if (!shouldNormalizeFontMetricElement(element, style)) {
        return null;
      }
      const clone = element.cloneNode(true);
      const fallbackFamily = getMetricFallbackFamily();
      clone.style.setProperty('font-family', fallbackFamily, 'important');
      clone.style.setProperty('position', 'absolute', 'important');
      clone.style.setProperty('left', '-100000px', 'important');
      clone.style.setProperty('top', '-100000px', 'important');
      clone.style.setProperty('visibility', 'hidden', 'important');
      clone.style.setProperty('pointer-events', 'none', 'important');
      document.documentElement.appendChild(clone);
      const fallbackRect = element.__otfOriginalGetBoundingClientRect.call(clone);
      clone.remove();
      return makeProtectedDOMRect(
          makeDOMRect(rect.x, rect.y, fallbackRect.width, fallbackRect.height),
          getMetricNoiseKey(element, 'rect'));
    } catch (_) {
      return null;
    }
  };
  const shouldNormalizeElementFont = (element) => {
    try {
      if (!element || !globalThis.getComputedStyle) return false;
      const style = getComputedStyle(element);
      return shouldNormalizeFontMetricElement(element, style);
    } catch (_) {
      return false;
    }
  };
  const shouldNormalizeFontMetricElement = (element, style) => {
    try {
      if (!style) return false;
      return isLikelyFontMetricProbe(element, style);
    } catch (_) {
      return false;
    }
  };
  const isLikelyFontMetricProbe = (element, style) => {
    try {
      if (!element || !style) return false;
      const text = String(element.textContent || '');
      if (!text.trim()) return false;
      const fontSize = Number.parseFloat(style.fontSize || '0') || 0;
      // Body-text-sized elements are almost never probes; skip them so we
      // don't normalize getBoundingClientRect on normal page content.
      if (fontSize < 16) return false;
      const left = Math.abs(Number.parseFloat(style.left || '0') || 0);
      const top = Math.abs(Number.parseFloat(style.top || '0') || 0);
      const offscreen = left >= 5000 || top >= 5000;
      const hidden = style.visibility === 'hidden' || style.display === 'none' ||
          Number.parseFloat(style.opacity || '1') === 0;
      const positioned = style.position === 'absolute' || style.position === 'fixed';
      const nowrap = style.whiteSpace === 'nowrap';
      const inlineBlock = style.display === 'inline-block';
      // Strong signals: classic off-screen / hidden positioned probe.
      if ((positioned && offscreen) || (hidden && positioned)) return true;
      // Large rendered text — common 48–72px font-presence probe.
      // (Loosened: was length ≥ 16.)
      if (fontSize >= 48 && text.length >= 8) return true;
      // nowrap measurement at ≥24px with short text — typical inline / span
      // pattern used by fingerprintjs and similar libraries.
      // (Loosened: was size ≥ 32, length ≥ 8.)
      if (nowrap && fontSize >= 24 && text.length >= 4) return true;
      // inline-block + nowrap with a candidate font outside our allowlist —
      // strong indicator of a font-availability probe.
      if (inlineBlock && nowrap &&
          !fontUsesAllowedFamily(style.fontFamily || style.font || '')) {
        return true;
      }
      return false;
    } catch (_) {
      return false;
    }
  };
  const getMetricFallbackFamily = () => {
    return 'Arial, sans-serif';
  };
  const getMetricNoiseKey = (element, metric) => {
    try {
      return 'font-metric:' + metric + ':' + String(element && element.textContent || '').length;
    } catch (_) {
      return 'font-metric:' + metric;
    }
  };
  const createFallbackFontClone = (element) => {
    try {
      if (!element || !globalThis.document || !globalThis.getComputedStyle) {
        return null;
      }
      const style = getComputedStyle(element);
      if (!shouldNormalizeFontMetricElement(element, style)) {
        return null;
      }
      const clone = element.cloneNode(true);
      const fallbackFamily = getMetricFallbackFamily();
      clone.style.setProperty('font-family', fallbackFamily, 'important');
      clone.style.setProperty('position', 'absolute', 'important');
      clone.style.setProperty('left', '-100000px', 'important');
      clone.style.setProperty('top', '-100000px', 'important');
      clone.style.setProperty('visibility', 'hidden', 'important');
      clone.style.setProperty('pointer-events', 'none', 'important');
      document.documentElement.appendChild(clone);
      return clone;
    } catch (_) {
      return null;
    }
  };
  const noisyIntegerMetric = (value, key = 'integer') => {
    let noise = Math.round(layoutNoiseValue(key) * 24);
    if (noise === 0) {
      noise = layoutNoiseSeed % 2 === 0 ? 2 : -2;
    }
    return Math.max(0, Math.round(Number(value || 0) + noise));
  };
  const defineLayoutMetricGetter = (target, name) => {
    try {
      const descriptor = Object.getOwnPropertyDescriptor(target, name);
      if (!descriptor || typeof descriptor.get !== 'function') return;
      Object.defineProperty(target, name, {
        get() {
          if (!shouldNormalizeElementFont(this)) {
            return descriptor.get.call(this);
          }
          const clone = createFallbackFontClone(this);
          if (!clone) {
            return noisyIntegerMetric(descriptor.get.call(this), getMetricNoiseKey(this, name));
          }
          try {
            return noisyIntegerMetric(descriptor.get.call(clone), getMetricNoiseKey(this, name));
          } finally {
            try { clone.remove(); } catch (_) {}
          }
        },
        configurable: false,
        enumerable: descriptor.enumerable
      });
    } catch (_) {}
  };
  const installLayoutMetricPolicy = () => {
    try {
      const ElementCtor = globalThis.Element;
      if (!ElementCtor || !ElementCtor.prototype ||
          ElementCtor.prototype.__otfLayoutMetricPolicy) {
        return;
      }
      const originalGetBoundingClientRect =
          ElementCtor.prototype.getBoundingClientRect;
      if (typeof originalGetBoundingClientRect === 'function') {
        Object.defineProperty(ElementCtor.prototype, '__otfOriginalGetBoundingClientRect', {
          value: originalGetBoundingClientRect,
          configurable: false,
          enumerable: false,
          writable: false
        });
        defineMethod(ElementCtor.prototype, 'getBoundingClientRect', function(...args) {
          const rect = originalGetBoundingClientRect.apply(this, args);
          return measureWithFallbackFont(this, rect) || rect;
        });
      }
      defineLayoutMetricGetter(ElementCtor.prototype, 'clientWidth');
      defineLayoutMetricGetter(ElementCtor.prototype, 'clientHeight');
      defineLayoutMetricGetter(ElementCtor.prototype, 'scrollWidth');
      defineLayoutMetricGetter(ElementCtor.prototype, 'scrollHeight');
      const HTMLElementCtor = globalThis.HTMLElement;
      if (HTMLElementCtor && HTMLElementCtor.prototype) {
        defineLayoutMetricGetter(HTMLElementCtor.prototype, 'offsetWidth');
        defineLayoutMetricGetter(HTMLElementCtor.prototype, 'offsetHeight');
      }
      Object.defineProperty(ElementCtor.prototype, '__otfLayoutMetricPolicy', {
        value: true,
        configurable: false,
        enumerable: false
      });
    } catch (_) {}
  };
  installLayoutMetricPolicy();

  // Range + SVG: normalize font-sensitive measurement APIs used for fingerprinting.
  const installRangeSvgMetricPolicy = () => {
    try {
      // ── Range ──────────────────────────────────────────────────────────────
      const RangeCtor = globalThis.Range;
      if (RangeCtor && RangeCtor.prototype && !RangeCtor.prototype.__otfRangePolicy) {
        const origRangeRect = RangeCtor.prototype.getBoundingClientRect;
        const origClientRects = RangeCtor.prototype.getClientRects;

        const isRangeFontProbe = (range) => {
          try {
            const container = range.commonAncestorContainer;
            const el = container && container.nodeType === 1
                ? container
                : container && container.parentElement;
            if (!el || !globalThis.getComputedStyle) return false;
            return isLikelyFontMetricProbe(el, getComputedStyle(el));
          } catch (_) { return false; }
        };

        const cloneRangeWithFallbackFont = (range) => {
          try {
            const container = range.commonAncestorContainer;
            const el = container && container.nodeType === 1
                ? container
                : container && container.parentElement;
            if (!el || !globalThis.document) return null;
            const clone = el.cloneNode(true);
            clone.style.setProperty('font-family', 'Arial, sans-serif', 'important');
            clone.style.setProperty('position', 'absolute', 'important');
            clone.style.setProperty('left', '-100000px', 'important');
            clone.style.setProperty('top', '-100000px', 'important');
            clone.style.setProperty('visibility', 'hidden', 'important');
            document.documentElement.appendChild(clone);
            const cloneRange = document.createRange();
            cloneRange.selectNodeContents(clone);
            return { clone, cloneRange };
          } catch (_) { return null; }
        };

        if (typeof origRangeRect === 'function') {
          defineMethod(RangeCtor.prototype, 'getBoundingClientRect', function(...args) {
            if (!isRangeFontProbe(this)) return origRangeRect.apply(this, args);
            const cloned = cloneRangeWithFallbackFont(this);
            if (!cloned) return origRangeRect.apply(this, args);
            try {
              const rect = origRangeRect.call(cloned.cloneRange);
              cloned.cloneRange.detach && cloned.cloneRange.detach();
              cloned.clone.remove();
              return makeProtectedDOMRect(rect, 'range:rect');
            } catch (_) {
              try { cloned.cloneRange.detach && cloned.cloneRange.detach(); } catch (_) {}
              try { cloned.clone.remove(); } catch (_) {}
              return origRangeRect.apply(this, args);
            }
          });
        }

        if (typeof origClientRects === 'function') {
          defineMethod(RangeCtor.prototype, 'getClientRects', function(...args) {
            if (!isRangeFontProbe(this)) return origClientRects.apply(this, args);
            const cloned = cloneRangeWithFallbackFont(this);
            if (!cloned) return origClientRects.apply(this, args);
            try {
              const rects = origClientRects.call(cloned.cloneRange);
              cloned.cloneRange.detach && cloned.cloneRange.detach();
              cloned.clone.remove();
              return rects;
            } catch (_) {
              try { cloned.cloneRange.detach && cloned.cloneRange.detach(); } catch (_) {}
              try { cloned.clone.remove(); } catch (_) {}
              return origClientRects.apply(this, args);
            }
          });
        }

        Object.defineProperty(RangeCtor.prototype, '__otfRangePolicy', {
          value: true, configurable: false, enumerable: false
        });
      }

      // ── SVG ────────────────────────────────────────────────────────────────
      const isSvgFontProbe = (element) => {
        try {
          const svg = element.ownerSVGElement || element;
          if (!globalThis.getComputedStyle) return false;
          const style = getComputedStyle(svg);
          const left = Math.abs(Number.parseFloat(style.left || '0') || 0);
          const top = Math.abs(Number.parseFloat(style.top || '0') || 0);
          const positioned = style.position === 'absolute' || style.position === 'fixed';
          return positioned && (left >= 5000 || top >= 5000);
        } catch (_) { return false; }
      };

      const cloneSvgWithFallbackFont = (element) => {
        try {
          const svg = element.ownerSVGElement;
          if (!svg || !globalThis.document) return null;
          const cloneSvg = svg.cloneNode(true);
          const texts = cloneSvg.querySelectorAll('text,tspan,textPath');
          texts.forEach((t) => t.setAttribute('font-family', 'Arial, monospace'));
          cloneSvg.style.setProperty('position', 'absolute', 'important');
          cloneSvg.style.setProperty('left', '-100000px', 'important');
          cloneSvg.style.setProperty('top', '-100000px', 'important');
          cloneSvg.style.setProperty('visibility', 'hidden', 'important');
          document.documentElement.appendChild(cloneSvg);
          const idx = Array.from(svg.querySelectorAll('text,tspan,textPath')).indexOf(element);
          const cloneEl = idx >= 0
              ? cloneSvg.querySelectorAll('text,tspan,textPath')[idx]
              : cloneSvg.querySelector('text');
          return { cloneSvg, cloneEl };
        } catch (_) { return null; }
      };

      const SVGGraphicsCtor = globalThis.SVGGraphicsElement;
      if (SVGGraphicsCtor && SVGGraphicsCtor.prototype &&
          !SVGGraphicsCtor.prototype.__otfSvgPolicy) {
        const origGetBBox = SVGGraphicsCtor.prototype.getBBox;
        if (typeof origGetBBox === 'function') {
          defineMethod(SVGGraphicsCtor.prototype, 'getBBox', function(...args) {
            if (!isSvgFontProbe(this)) return origGetBBox.apply(this, args);
            const cloned = cloneSvgWithFallbackFont(this);
            if (!cloned || !cloned.cloneEl) {
              if (cloned) try { cloned.cloneSvg.remove(); } catch (_) {}
              return origGetBBox.apply(this, args);
            }
            try {
              const box = origGetBBox.call(cloned.cloneEl, ...args);
              cloned.cloneSvg.remove();
              return makeProtectedDOMRect(
                makeDOMRect(box.x, box.y, box.width, box.height), 'svg:bbox');
            } catch (_) {
              try { cloned.cloneSvg.remove(); } catch (_) {}
              return origGetBBox.apply(this, args);
            }
          });
        }
        Object.defineProperty(SVGGraphicsCtor.prototype, '__otfSvgPolicy', {
          value: true, configurable: false, enumerable: false
        });
      }

      const SVGTextCtor = globalThis.SVGTextContentElement;
      if (SVGTextCtor && SVGTextCtor.prototype &&
          !SVGTextCtor.prototype.__otfSvgTextPolicy) {
        const origGetTextLength = SVGTextCtor.prototype.getComputedTextLength;
        if (typeof origGetTextLength === 'function') {
          defineMethod(SVGTextCtor.prototype, 'getComputedTextLength', function(...args) {
            if (!isSvgFontProbe(this)) return origGetTextLength.apply(this, args);
            const cloned = cloneSvgWithFallbackFont(this);
            if (!cloned || !cloned.cloneEl) {
              if (cloned) try { cloned.cloneSvg.remove(); } catch (_) {}
              return origGetTextLength.apply(this, args);
            }
            try {
              const length = origGetTextLength.call(cloned.cloneEl, ...args);
              cloned.cloneSvg.remove();
              return length + layoutNoiseValue('svg:textlen') * 2;
            } catch (_) {
              try { cloned.cloneSvg.remove(); } catch (_) {}
              return origGetTextLength.apply(this, args);
            }
          });
        }
        Object.defineProperty(SVGTextCtor.prototype, '__otfSvgTextPolicy', {
          value: true, configurable: false, enumerable: false
        });
      }
    } catch (_) {}
  };
  installRangeSvgMetricPolicy();

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
  // Chain-wrap from whichever layer of the GPU → GPUAdapter → GPUDevice
  // prototype chain is actually exposed at policy-injection time. Each wrap
  // patches the next layer's prototype on first use, so by the time a caller
  // can touch device.createComputePipeline*, those methods are already
  // replaced. No polling needed.
  const COMPUTE_DENIED_MSG =
      'WebGPU compute pipelines are disabled by browser policy.';
  const patchGPUDeviceProto = (proto) => {
    if (!proto || proto.__otfWebGPUComputePolicy) return;
    defineMethod(proto, 'createComputePipeline', () => {
      throw makeSecurityError(COMPUTE_DENIED_MSG);
    });
    defineMethod(proto, 'createComputePipelineAsync', () =>
      Promise.reject(makeSecurityError(COMPUTE_DENIED_MSG))
    );
    Object.defineProperty(proto, '__otfWebGPUComputePolicy', {
      value: true, configurable: false, enumerable: false, writable: false
    });
  };
  const patchGPUAdapterProto = (proto) => {
    if (!proto || proto.__otfGPUAdapterPolicy) return;
    const originalRequestDevice = proto.requestDevice;
    if (typeof originalRequestDevice === 'function') {
      defineMethod(proto, 'requestDevice', function(...args) {
        return originalRequestDevice.apply(this, args).then((device) => {
          if (device) patchGPUDeviceProto(Object.getPrototypeOf(device));
          return device;
        });
      });
    }
    Object.defineProperty(proto, '__otfGPUAdapterPolicy', {
      value: true, configurable: false, enumerable: false, writable: false
    });
  };
  const patchGPUProto = (proto) => {
    if (!proto || proto.__otfGPUPolicy) return;
    const originalRequestAdapter = proto.requestAdapter;
    if (typeof originalRequestAdapter === 'function') {
      defineMethod(proto, 'requestAdapter', function(...args) {
        return originalRequestAdapter.apply(this, args).then((adapter) => {
          if (adapter) patchGPUAdapterProto(Object.getPrototypeOf(adapter));
          return adapter;
        });
      });
    }
    Object.defineProperty(proto, '__otfGPUPolicy', {
      value: true, configurable: false, enumerable: false, writable: false
    });
  };
  // Patch every layer that's exposed up-front. If a layer isn't, the wrapper
  // installed on the layer above takes care of it when its returned value
  // first resolves.
  if (globalThis.GPUDevice && globalThis.GPUDevice.prototype) {
    patchGPUDeviceProto(globalThis.GPUDevice.prototype);
  }
  if (globalThis.GPUAdapter && globalThis.GPUAdapter.prototype) {
    patchGPUAdapterProto(globalThis.GPUAdapter.prototype);
  }
  if (globalThis.GPU && globalThis.GPU.prototype) {
    patchGPUProto(globalThis.GPU.prototype);
  } else {
    // Even the GPU constructor isn't exposed yet. Hook navigator.gpu by
    // shadowing the getter so we can patch on first access.
    try {
      const nav = globalThis.navigator;
      const proto = nav && Object.getPrototypeOf(nav);
      const desc = proto && Object.getOwnPropertyDescriptor(proto, 'gpu');
      if (desc && typeof desc.get === 'function') {
        const originalGetter = desc.get;
        Object.defineProperty(proto, 'gpu', {
          configurable: desc.configurable,
          enumerable: desc.enumerable,
          get() {
            const gpu = originalGetter.call(this);
            if (gpu) patchGPUProto(Object.getPrototypeOf(gpu));
            return gpu;
          }
        });
      }
    } catch (_) {}
  }
  // Debug marker so we can confirm from devtools that the policy attached.
  // Read with: globalThis.__otfWebGPUPolicyState
  Object.defineProperty(globalThis, '__otfWebGPUPolicyState', {
    value: Object.freeze({
      hadGPU: !!globalThis.GPU,
      hadGPUAdapter: !!globalThis.GPUAdapter,
      hadGPUDevice: !!globalThis.GPUDevice
    }),
    configurable: false, enumerable: false, writable: false
  });

  // Canvas: protect readback/export surfaces used for fingerprinting.
  // Tiny additive noise (±2 per channel) drawn fresh per call. Additive
  // perturbation is preferred over XOR because XOR is reversible and, with
  // a stable seed, the output itself becomes a stable per-session fingerprint
  // signal. Fresh randomness per call also breaks linkability across reads
  // of the same canvas.
  const perturbPixelBuffer = (data) => {
    try {
      if (!data || typeof data.length !== 'number') return data;
      for (let i = 0; i < data.length; i += 4) {
        let r = data[i] + ((Math.random() * 5) | 0) - 2;
        data[i] = r < 0 ? 0 : r > 255 ? 255 : r;
        if (i + 1 < data.length) {
          let g = data[i + 1] + ((Math.random() * 5) | 0) - 2;
          data[i + 1] = g < 0 ? 0 : g > 255 ? 255 : g;
        }
        if (i + 2 < data.length) {
          let b = data[i + 2] + ((Math.random() * 5) | 0) - 2;
          data[i + 2] = b < 0 ? 0 : b > 255 ? 255 : b;
        }
      }
    } catch (_) {}
    return data;
  };
  const perturbImageData = (imageData) => {
    try {
      if (!imageData || !imageData.width || !imageData.height) return imageData;
      const src = imageData.data;
      if (!src) return imageData;
      const w = imageData.width;
      const h = imageData.height;
      const dst = new ImageData(w, h);
      const dstData = dst.data;
      for (let i = 0; i < src.length; i++) dstData[i] = src[i];
      perturbPixelBuffer(dstData);
      return dst;
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
          const perturbed = perturbImageData(imageData);
          originalPutImageData.call(copyContext, perturbed, 0, 0);
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

    const OffscreenCanvasCtor = globalThis.OffscreenCanvas;
    if (OffscreenCanvasCtor && OffscreenCanvasCtor.prototype &&
        !OffscreenCanvasCtor.prototype.__otfCanvasPolicy) {
      const originalConvertToBlob = OffscreenCanvasCtor.prototype.convertToBlob;
      if (typeof originalConvertToBlob === 'function') {
        defineMethod(OffscreenCanvasCtor.prototype, 'convertToBlob', function(...args) {
          try {
            const copy = new OffscreenCanvas(this.width, this.height);
            const copyContext = copy.getContext('2d');
            if (copyContext && typeof copyContext.drawImage === 'function') {
              copyContext.drawImage(this, 0, 0);
              const imageData = copyContext.getImageData(0, 0, copy.width, copy.height);
              const perturbed = perturbImageData(imageData);
              copyContext.putImageData(perturbed, 0, 0);
              return originalConvertToBlob.apply(copy, args);
            }
          } catch (_) {}
          return originalConvertToBlob.apply(this, args);
        });
      }
      Object.defineProperty(OffscreenCanvasCtor.prototype, '__otfCanvasPolicy', {
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
            perturbPixelBuffer(pixels);
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

  // Block javascript: URLs in iframe src (and setAttribute) — CEF's
  // OnBeforeBrowse doesn't fire for javascript: because Chromium treats
  // it as script execution, not navigation.
  const installIframeSchemePolicy = () => {
    try {
      const IframeProto = globalThis.HTMLIFrameElement &&
          globalThis.HTMLIFrameElement.prototype;
      if (!IframeProto || IframeProto.__otfIframePolicy) return;

      const JS_RE = /^\s*javascript:/i;
      const srcDesc = Object.getOwnPropertyDescriptor(IframeProto, 'src');
      if (srcDesc && typeof srcDesc.set === 'function') {
        Object.defineProperty(IframeProto, 'src', {
          get: srcDesc.get,
          set(value) {
            if (typeof value === 'string' && JS_RE.test(value)) return;
            return srcDesc.set.call(this, value);
          },
          configurable: false,
          enumerable: true,
        });
      }

      const origSetAttr = IframeProto.setAttribute;
      if (typeof origSetAttr === 'function') {
        IframeProto.setAttribute = function(name, value) {
          if (name === 'src' && typeof value === 'string' && JS_RE.test(value)) {
            return;
          }
          return origSetAttr.call(this, name, value);
        };
      }

      Object.defineProperty(IframeProto, '__otfIframePolicy', {
        value: true, configurable: false,
      });
    } catch (_) {}
  };
  installIframeSchemePolicy();

  (() => {
    'use strict';
    const md = navigator.mediaDevices;
    if (!md) return;

    const origGUM = md.getUserMedia.bind(md);

    // ---- one fixed list EVERYONE reports, regardless of real hardware ----
    const FIXED = [
      { deviceId: 'fixed-audioinput',  kind: 'audioinput',  groupId: 'fixed-group-a' },
      { deviceId: 'fixed-audiooutput', kind: 'audiooutput', groupId: 'fixed-group-a' },
      { deviceId: 'fixed-videoinput',  kind: 'videoinput',  groupId: 'fixed-group-v' },
    ];

    const STD_AUDIO_SETTINGS = {
      sampleRate: 48000, channelCount: 1, sampleSize: 16,
      echoCancellation: true, autoGainControl: true, noiseSuppression: true, latency: 0.01,
    };
    const STD_VIDEO_SETTINGS = {
      width: 1280, height: 720, frameRate: 30, aspectRatio: 1280 / 720,
      facingMode: 'user', resizeMode: 'none',
    };
    const STD_AUDIO_CAPS = {
      sampleRate: { min: 48000, max: 48000 }, channelCount: { min: 1, max: 1 },
      echoCancellation: [true, false], autoGainControl: [true, false], noiseSuppression: [true, false],
    };
    const STD_VIDEO_CAPS = {
      width: { min: 1, max: 1280 }, height: { min: 1, max: 720 }, frameRate: { min: 1, max: 30 },
      aspectRatio: { min: 0.001, max: 1280 }, facingMode: ['user'], resizeMode: ['none', 'crop-and-scale'],
    };

    // ---- enumerateDevices: always the same list for everyone ----
    md.enumerateDevices = async function enumerateDevices() {
      // labels populated after permission; mirror that with a simple gate
      let granted = false;
      try {
        if (navigator.permissions) {
          const p = await navigator.permissions.query({ name: 'microphone' }).catch(() => null);
          granted = p && p.state === 'granted';
        }
      } catch (_) {}
      return FIXED.map(d => ({
        deviceId: d.deviceId,
        kind: d.kind,
        groupId: d.groupId,
        label: granted ? `Standard ${d.kind}` : '',
        toJSON() { return this; },
      }));
    };

    // ---- getUserMedia: try real hardware; if absent, it fails naturally ----
    md.getUserMedia = async function getUserMedia(constraints) {
      const c = JSON.parse(JSON.stringify(constraints || {}));
      for (const media of ['audio', 'video']) {
        if (c[media] && typeof c[media] === 'object' && c[media].deviceId) {
          delete c[media].deviceId; // open the real default device of that kind
        }
      }

      // If hardware is missing, origGUM rejects (NotFoundError) — let it propagate silently.
      const stream = await origGUM(c);

      stream.getTracks().forEach(track => {
        const isAudio = track.kind === 'audio';
        const kind = isAudio ? 'audioinput' : 'videoinput';
        const fixedId = `fixed-${kind}`;
        const fixedGroup = isAudio ? 'fixed-group-a' : 'fixed-group-v';
        const stdSettings = isAudio ? STD_AUDIO_SETTINGS : STD_VIDEO_SETTINGS;
        const stdCaps = isAudio ? STD_AUDIO_CAPS : STD_VIDEO_CAPS;

        track.getSettings = function getSettings() {
          return { deviceId: fixedId, groupId: fixedGroup, ...stdSettings };
        };
        track.getCapabilities = function getCapabilities() {
          return { deviceId: fixedId, groupId: fixedGroup, ...stdCaps };
        };
        try {
          Object.defineProperty(track, 'label', { get: () => `Standard ${track.kind}`, configurable: true });
        } catch (_) {}
      });

      return stream;
    };

    if (md.getSupportedConstraints) {
      md.getSupportedConstraints = function getSupportedConstraints() {
        return {
          deviceId: true, groupId: true, sampleRate: true, channelCount: true, sampleSize: true,
          echoCancellation: true, autoGainControl: true, noiseSuppression: true,
          width: true, height: true, frameRate: true, aspectRatio: true,
          facingMode: true, resizeMode: true, latency: true,
        };
      };
    }

    try {
      Object.defineProperty(md, 'ondevicechange', { get: () => null, set: () => {}, configurable: true });
    } catch (_) {}
    const origAdd = md.addEventListener && md.addEventListener.bind(md);
    if (origAdd) {
      md.addEventListener = function (type, ...rest) {
        if (type === 'devicechange') return;
        return origAdd(type, ...rest);
      };
    }
  })();

  // Battery API: remove the surface entirely. No fingerprint, no API.
  (() => {
    'use strict';
    try { delete Navigator.prototype.getBattery; } catch (_) {}
    try { delete navigator.getBattery; } catch (_) {}
    if (typeof BatteryManager !== 'undefined') {
      try { delete window.BatteryManager; } catch (_) {}
    }
  })();

  // Network Information API: remove the surface entirely.
  // Also targets WorkerNavigator.prototype so workers don't leak it either.
  (() => {
    'use strict';
    try { delete Navigator.prototype.connection; } catch (_) {}
    try { delete navigator.connection; } catch (_) {}
    // Remove from the live navigator prototype — covers WorkerNavigator in worker realms.
    try { delete Object.getPrototypeOf(navigator).connection; } catch (_) {}
    // vendor-prefixed variants
    try { delete Navigator.prototype.mozConnection; } catch (_) {}
    try { delete Navigator.prototype.webkitConnection; } catch (_) {}
    if (typeof NetworkInformation !== 'undefined') {
      try { delete globalThis.NetworkInformation; } catch (_) {}
      // delete only removes own properties; if NetworkInformation lives on the
      // prototype chain (as it does in worker scopes), shadow it with undefined.
      if (typeof NetworkInformation !== 'undefined') {
        try {
          Object.defineProperty(globalThis, 'NetworkInformation', {
            value: undefined, configurable: true, writable: true, enumerable: false,
          });
        } catch (_) {}
      }
    }
  })();

  // Keyboard API: remove the surface entirely.
  (() => {
    'use strict';
    try { delete Navigator.prototype.keyboard; } catch (_) {}
    try { delete navigator.keyboard; } catch (_) {}
    if (typeof Keyboard !== 'undefined') {
      try { delete window.Keyboard; } catch (_) {}
    }
    if (typeof KeyboardLayoutMap !== 'undefined') {
      try { delete window.KeyboardLayoutMap; } catch (_) {}
    }
  })();

  // Service Worker API: remove the surface entirely. Service Workers run in
  // a separate realm that bypasses page policy injection, and they can persist
  // cached responses, background sync, and push subscriptions across sessions.
  (() => {
    'use strict';
    try {
      Object.defineProperty(Navigator.prototype, 'serviceWorker', {
        get: () => undefined, configurable: true,
      });
    } catch (_) {}
    // Note: ServiceWorker/ServiceWorkerRegistration/ServiceWorkerContainer
    // constructors live on Window.prototype and cannot be removed from JS.
    // They are inert without navigator.serviceWorker — no registration path exists.
  })();

  // Do Not Track: always report doNotTrack = "1" so pages honour the preference.
  (() => {
    'use strict';
    try {
      if (typeof Navigator === 'undefined') return;
      const proto = Navigator.prototype;
      if (proto.__otfDntPolicy) return;
      const desc = Object.getOwnPropertyDescriptor(proto, 'doNotTrack');
      Object.defineProperty(proto, 'doNotTrack', {
        get: makeNative(
          function doNotTrack() { return '1'; },
          'function get doNotTrack() { [native code] }'
        ),
        set: undefined,
        configurable: false,
        enumerable: desc ? desc.enumerable : true,
      });
      Object.defineProperty(proto, '__otfDntPolicy', {
        value: true, configurable: false, enumerable: false, writable: false,
      });
    } catch (_) {}
  })();

  // Third-party cookie blocking: in cross-origin sub-frames, shadow
  // document.cookie so embedded trackers cannot read or write cookies via JS.
  // Network-level cookies (HTTP headers) are handled separately by the browser.
  // Accessing window.top.location.href throws a SecurityError in cross-origin
  // frames, which is the reliable signal that we are a third-party context.
  (() => {
    'use strict';
    try {
      if (typeof window === 'undefined' || window === window.top) return;
      let isThirdParty = false;
      try {
        void window.top.location.href;
      } catch (_) {
        isThirdParty = true;
      }
      if (!isThirdParty) return;

      const docProto = globalThis.Document && globalThis.Document.prototype
          ? globalThis.Document.prototype
          : Object.getPrototypeOf(document);
      if (!docProto || docProto.__otfCookiePolicy) return;

      const emptyCookieGetter = makeNative(
        function cookie() { return ''; },
        'function get cookie() { [native code] }'
      );
      const emptyCookieSetter = makeNative(
        function cookie(_val) {},
        'function set cookie(_val) { [native code] }'
      );

      const existing = Object.getOwnPropertyDescriptor(docProto, 'cookie');
      Object.defineProperty(docProto, 'cookie', {
        get: emptyCookieGetter,
        set: emptyCookieSetter,
        configurable: false,
        enumerable: existing ? existing.enumerable : true,
      });
      Object.defineProperty(docProto, '__otfCookiePolicy', {
        value: true, configurable: false, enumerable: false, writable: false,
      });
    } catch (_) {}
  })();

  // --- Math fingerprinting protection ------------------------------------
  // Wraps transcendental/trig Math functions to add a tiny deterministic
  // per-(session, function, input) perturbation.  The delta is consistent
  // within a page load (same args → same result) but changes every reload,
  // so exact-value fingerprints stop working without affecting real math code.
  (() => {
    // Noise amplitude: ~1e-13 absolute — affects only the last ~2 significant
    // digits of a 64-bit double, well below any practical precision threshold.
    const MATH_NOISE_AMP = 8e-13; // layoutNoiseValue range is [-0.125,0.125]

    const noisyMath = (name) => {
      const orig = Math[name];
      if (typeof orig !== 'function') return;
      const wrapped = makeNative(
        function(...args) {
          const result = orig.apply(this, args);
          if (!isFinite(result) || Math.abs(result) < MATH_NOISE_AMP) return result;
          const key = 'math.' + name + '.' + args.join(',');
          return result + layoutNoiseValue(key) * MATH_NOISE_AMP;
        },
        'function ' + name + '() { [native code] }'
      );
      Math[name] = wrapped;
    };

    for (const fn of [
      'sin', 'cos', 'tan',
      'asin', 'acos', 'atan', 'atan2',
      'sinh', 'cosh', 'tanh',
      'asinh', 'acosh', 'atanh',
      'exp', 'expm1',
      'log', 'log2', 'log10', 'log1p',
      'sqrt', 'cbrt', 'pow', 'hypot',
    ]) { noisyMath(fn); }
  })();

  // Workers: re-apply the same policy in each worker realm that spawns workers
  // of its own. policySource and wrapWorkerCtor live INSIDE applyPagePolicy so
  // that applyPagePolicy.toString() captures them — making the bootstrap blob
  // fully self-contained at any nesting depth (dedicated → nested → ...).
  const policySource = '(' + applyPagePolicy.toString() + ')();';

  // Intercept URL.createObjectURL so that any JS blob becomes a policy-first
  // blob before it is handed to SharedWorker (whose constructor is non-writable
  // in Chromium's blink IDL and cannot be replaced by any JS technique).
  // We track blobs we already patched to avoid double-injection.
  const otfPatchedBlobs = new WeakSet();
  if (typeof URL !== 'undefined' && typeof URL.createObjectURL === 'function') {
    const _origCreateObjectURL = URL.createObjectURL.bind(URL);
    URL.createObjectURL = function(obj) {
      if (obj instanceof Blob && !otfPatchedBlobs.has(obj) &&
          (obj.type === '' || obj.type === 'application/javascript' ||
           obj.type === 'text/javascript')) {
        const patched = new Blob(
          [policySource, '\n', obj],
          { type: obj.type || 'application/javascript' }
        );
        otfPatchedBlobs.add(patched);
        return _origCreateObjectURL(patched);
      }
      return _origCreateObjectURL(obj);
    };
  }

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
      // Mark as already patched so the URL.createObjectURL intercept above
      // does not prepend policy a second time to this bootstrap blob.
      otfPatchedBlobs.add(bootstrap);
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
    // Walk the prototype chain to find the object that actually owns the
    // property (e.g. SharedWorker lives on Window.prototype, not on globalThis
    // itself). Overriding the actual owner avoids the defineProperty failure
    // that occurs when trying to shadow an inherited non-configurable property.
    let owner = globalThis;
    let proto = globalThis;
    while (proto) {
      if (Object.prototype.hasOwnProperty.call(proto, name)) { owner = proto; break; }
      proto = Object.getPrototypeOf(proto);
    }
    try {
      Object.defineProperty(owner, name, {
        value: WrappedWorker, configurable: true, writable: true, enumerable: false,
      });
    } catch (_) {}
    if (globalThis[name] !== WrappedWorker) {
      try { globalThis[name] = WrappedWorker; } catch (_) {}
    }
  };
  wrapWorkerCtor('Worker');
  wrapWorkerCtor('SharedWorker');

  }  // applyPagePolicy

  applyPagePolicy();
})();
)JS";
  // Substitute the caller-provided profile JSON. If empty (e.g. a renderer
  // that didn't receive extra_info — defensive only, shouldn't happen),
  // fall back to a known-good profile so the script is still valid JS.
  // ChooseNearestScreenProfile() returns null-display defaults safely.
  std::string profile_json = screen_profile_json;
  if (profile_json.empty()) {
    profile_json = BuildScreenProfileJson(kScreenProfiles[0]);
  }
  ReplaceAll(script, "__OTF_SCREEN_PROFILE__", profile_json);
  return script;
}

}  // namespace otf
