import { functionLooksWrapped } from "./helpers.js";

// Bundles three runtime-surface probes that share machinery: canvas
// measureText/font check, getImageData noise, and which built-in functions
// have been wrapped by the page policy. The Worker constructor check lives
// here too because it reuses the same `functionLooksWrapped` helper.
export default {
  module: 'side-effects',
  category: 'privacy',
  produces: [
    {
      id: 'getImageData-noise',
      label: 'Canvas pixel noise',
      entropy: 'high',
      description: 'Per-pixel canvas noise injection — defeats stable canvas hashes.',
    },
    {
      id: 'worker-surface',
      label: 'Worker execution surface',
      entropy: 'low',
      description: 'Whether Worker contexts are isolated from page-level protections.',
    },
  ],
  async run(ctx) {
    // 1. canvas.measureText — web fonts normalized to Arial.
    const mc = document.createElement('canvas');
    const mctx = mc.getContext('2d');
    let arialW = 'unavailable', robotoW = 'unavailable', measureNormalized = false;
    if (mctx && typeof mctx.measureText === 'function') {
      mctx.font = '72px Arial';
      arialW = mctx.measureText('Hello mmmMMM').width;
      mctx.font = '72px "Roboto"';
      robotoW = mctx.measureText('Hello mmmMMM').width;
      measureNormalized = typeof arialW === 'number' && typeof robotoW === 'number' &&
        Math.abs(arialW - robotoW) < 0.5;
    }

    // 2. document.fonts.check — non-allowed fonts always return false.
    let arialCheck = 'unavailable', robotoCheck = 'unavailable';
    try {
      if (document.fonts && typeof document.fonts.check === 'function') {
        arialCheck = document.fonts.check('12px Arial');
        robotoCheck = document.fonts.check('12px "Roboto"');
      }
    } catch (_) {}
    const fontCheckBlocked = robotoCheck === false;

    // 3. getImageData pixel noise — a solid red canvas should remain 255-red;
    //    any deviation shows the noise policy is active.
    const nc = document.createElement('canvas');
    nc.width = 8; nc.height = 8;
    const nctx = nc.getContext('2d');
    let pixelDeviation = 'unavailable';
    let noiseActive = false;
    let getImageDataWrapped = 'unavailable';
    let imageDataType = 'unavailable';
    let rawRedSample = 'unavailable';
    try {
      getImageDataWrapped = functionLooksWrapped(globalThis.CanvasRenderingContext2D?.prototype?.getImageData);
    } catch (_) {}
    if (nctx && typeof nctx.getImageData === 'function') {
      nctx.fillStyle = '#ff0000';
      nctx.fillRect(0, 0, 8, 8);
      const id = nctx.getImageData(0, 0, 8, 8);
      imageDataType = id ? id.constructor?.name || typeof id : 'null';
      const reds = [];
      if (id && id.data) {
        rawRedSample = id.data[0];
        for (let i = 0; i < id.data.length; i += 4) reds.push(id.data[i]);
      }
      pixelDeviation = reds.length ? 255 - Math.min(...reds) : 'no data';
      noiseActive = typeof pixelDeviation === 'number' && pixelDeviation > 0;
    }

    ctx.set('getImageData-noise',
      noiseActive ? 'ok' : getImageDataWrapped === false ? 'fail' : 'warn',
      noiseActive ? 'getImageData pixel noise active'
        : getImageDataWrapped === false ? 'getImageData not wrapped by policy'
        : 'getImageData wrapped but noise not visible',
      `deviation: ${pixelDeviation}, wrapped: ${getImageDataWrapped}`,
      [
        ['canvas measureText — Arial width', String(typeof arialW === 'number' ? Math.round(arialW * 100) / 100 : arialW)],
        ['canvas measureText — Roboto width', String(typeof robotoW === 'number' ? Math.round(robotoW * 100) / 100 : robotoW)],
        ['canvas measureText normalized (Roboto→Arial)', String(measureNormalized)],
        ['document.fonts.check Arial', String(arialCheck)],
        ['document.fonts.check Roboto (should be false)', String(robotoCheck)],
        ['font check blocked for non-allowed fonts', String(fontCheckBlocked)],
        ['getImageData wrapped', String(getImageDataWrapped)],
        ['getImageData return type', String(imageDataType)],
        ['getImageData raw red pixel [0]', String(rawRedSample)],
        ['getImageData max red pixel deviation from 255', String(pixelDeviation)],
        ['getImageData noise active', String(noiseActive)],
        ['note', 'Chart.js/D3 labels affected by measureText; color pickers affected by pixel noise'],
      ]);

    const workerWrapped = functionLooksWrapped(globalThis.Worker);
    const workerAvailable = typeof globalThis.Worker === 'function';
    const injected = Boolean(globalThis.__otfPagePolicyInjected);
    const canvasWrapped = functionLooksWrapped(globalThis.CanvasRenderingContext2D?.prototype?.getImageData);
    const toDataUrlWrapped = functionLooksWrapped(globalThis.HTMLCanvasElement?.prototype?.toDataURL);
    const webglWrapped = functionLooksWrapped(globalThis.WebGLRenderingContext?.prototype?.getParameter);

    ctx.set('worker-surface',
      workerWrapped ? 'ok' : workerAvailable ? 'warn' : 'ok',
      workerWrapped ? 'Worker constructor protected'
        : workerAvailable ? 'Worker API available' : 'Worker API unavailable',
      workerAvailable
        ? 'Worker contexts can expose additional fingerprinting surfaces.'
        : 'No Worker constructor was exposed.',
      [
        ['user agent', navigator.userAgent],
        ['page policy injected', String(injected)],
        ['canvas getImageData wrapped', String(canvasWrapped)],
        ['canvas toDataURL wrapped', String(toDataUrlWrapped)],
        ['webgl getParameter wrapped', String(webglWrapped)],
        ['worker constructor wrapped', String(workerWrapped)],
      ]);
  },
};
