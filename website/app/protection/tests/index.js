// Registry — ordered list of test modules. The order is also the default
// list order in the UI. Group by category, then by entropy bucket.
import screen from "./screen.js";
import hardware from "./hardware.js";
import sideEffects from "./side-effects.js";
import fonts from "./fonts.js";
import fontAdvanced from "./font-advanced.js";
import layoutMetrics from "./layout-metrics.js";
import canvas from "./canvas.js";
import webgl from "./webgl.js";
import fingerprintSurfaces from "./fingerprint-surfaces.js";
import audio from "./audio.js";
import localFontApi from "./local-font-api.js";
import webgpu from "./webgpu.js";
import navigationScheme from "./navigation-scheme.js";

export default [
  // Privacy modules
  screen,
  hardware,
  sideEffects,
  fonts,
  fontAdvanced,
  layoutMetrics,
  canvas,
  webgl,
  fingerprintSurfaces,
  audio,            // needsReload
  localFontApi,     // needsGesture
  // Security modules
  webgpu,
  navigationScheme,
];
