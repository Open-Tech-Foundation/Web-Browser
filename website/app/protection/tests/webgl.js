import { hashText, runWebGLContextProbe, trackSessionHistory, storageKeys } from "./helpers.js";

export default {
  module: 'webgl',
  category: 'privacy',
  produces: [
    {
      id: 'webgl-profile',
      label: 'WebGL identity',
      entropy: 'high',
      description: 'UNMASKED_VENDOR / RENDERER — direct GPU model disclosure.',
    },
    {
      id: 'webgl-debug',
      label: 'WebGL debug renderer',
      entropy: 'high',
      description: 'Whether WEBGL_debug_renderer_info is reachable.',
    },
    {
      id: 'webgl-render-output',
      label: 'WebGL rendered output',
      entropy: 'high',
      description: 'Stability of the rendered framebuffer hash across sessions.',
    },
    {
      id: 'webgl-readpixels-noise',
      label: 'WebGL readPixels noise',
      entropy: 'high',
      description: 'gl.readPixels can bypass canvas-noise policy.',
    },
  ],
  async run(ctx) {
    const canvas = document.createElement('canvas');
    canvas.width = 64;
    canvas.height = 64;
    const renderHash = async (gl) => {
      gl.clearColor(0.18, 0.52, 0.38, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
      const pixels = new Uint8Array(64 * 64 * 4);
      gl.readPixels(0, 0, 64, 64, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      return hashText([...pixels].join(','));
    };
    const gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
    if (!gl) {
      const rows = [['result', 'No WebGL context']];
      ctx.set('webgl-profile', 'warn', 'WebGL unavailable', 'No WebGL context was created.', rows);
      ctx.set('webgl-debug', 'warn', 'WebGL unavailable', 'Cannot verify debug renderer exposure without WebGL.', rows);
      ctx.set('webgl-render-output', 'warn', 'WebGL unavailable', 'Cannot verify rendered-output stability without WebGL.', rows);
      ctx.set('webgl-readpixels-noise', 'warn', 'WebGL unavailable', '', rows);
      return;
    }
    const hash = await renderHash(gl);
    const history = trackSessionHistory(storageKeys.webgl, hash);
    const uniqueSessionHashes = new Set(history).size;
    const sessionCount = history.length;
    const allUnique = uniqueSessionHashes === sessionCount;

    const webgl2Canvas = document.createElement('canvas');
    webgl2Canvas.width = 64;
    webgl2Canvas.height = 64;
    const gl2 = webgl2Canvas.getContext('webgl2');
    const webgl2Hash = gl2 ? await renderHash(gl2) : 'unavailable';
    const exportHash = await hashText(canvas.toDataURL('image/png'));
    let offscreenHash = 'unavailable';
    if (typeof OffscreenCanvas === 'function') {
      try {
        const offscreen = new OffscreenCanvas(64, 64);
        const offscreenGl = offscreen.getContext('webgl') || offscreen.getContext('webgl2');
        if (offscreenGl) offscreenHash = await renderHash(offscreenGl);
      } catch (error) {
        offscreenHash = `${error.name}: ${error.message}`;
      }
    }
    const contextProbes = await Promise.all([runWebGLContextProbe(), runWebGLContextProbe()]);
    const methodNames = ['webgl1ReadPixels', 'webgl1Export', 'webgl2ReadPixels', 'offscreenWebGL'];
    const changedMethods = methodNames.filter((name) =>
      contextProbes[0] && contextProbes[1] &&
      contextProbes[0][name] && contextProbes[1][name] &&
      contextProbes[0][name] !== 'unavailable' && contextProbes[1][name] !== 'unavailable' &&
      contextProbes[0][name] !== contextProbes[1][name]);
    const probeError = contextProbes.find((p) => p && p.error)?.error;
    const readPixelsNoiseActive = changedMethods.includes('webgl1ReadPixels')
      || changedMethods.includes('webgl2ReadPixels')
      || changedMethods.includes('offscreenWebGL');

    ctx.set('webgl-readpixels-noise',
      readPixelsNoiseActive ? 'ok' : 'fail',
      readPixelsNoiseActive ? 'WebGL readPixels output varies' : 'WebGL readPixels unprotected',
      'gl.readPixels returns raw GPU pixel data — not perturbed by the canvas noise policy.',
      [['changed methods', changedMethods.join(', ') || 'none']]);

    const renderedStatus = sessionCount < 2
      ? (changedMethods.length > 0 ? 'ok' : 'warn')
      : allUnique ? 'ok' : 'fail';
    ctx.set('webgl-render-output', renderedStatus,
      sessionCount < 2
        ? (changedMethods.length > 0 ? 'Rendered output varies across contexts' : 'Awaiting second session for baseline')
        : allUnique
          ? `All ${sessionCount} sessions produced different WebGL hashes`
          : 'Duplicate WebGL hash across sessions',
      `Unique session hashes: ${uniqueSessionHashes}/${sessionCount}, changed context methods: ${changedMethods.length}`,
      [
        ['session history (newest last)', history.join(', ')],
        ['sessions recorded', String(sessionCount)],
        ['unique session hashes', `${uniqueSessionHashes}/${sessionCount}`],
        ['WebGL2 readPixels hash', webgl2Hash],
        ['canvas export hash after WebGL', exportHash],
        ['OffscreenCanvas WebGL hash', offscreenHash],
        ['automated context probe 1', JSON.stringify(contextProbes[0])],
        ['automated context probe 2', JSON.stringify(contextProbes[1])],
        ['automated changed methods', changedMethods.join(', ') || 'none'],
        ['automated probe error', probeError || 'none'],
      ]);

    const debugExtension = gl.getExtension('WEBGL_debug_renderer_info');
    let vendor = 'unavailable';
    let renderer = 'unavailable';
    if (debugExtension) {
      vendor = gl.getParameter(debugExtension.UNMASKED_VENDOR_WEBGL);
      renderer = gl.getParameter(debugExtension.UNMASKED_RENDERER_WEBGL);
    } else {
      try { vendor = gl.getParameter(37445); renderer = gl.getParameter(37446); }
      catch { vendor = 'hidden'; renderer = 'hidden'; }
    }
    const extensions = gl.getSupportedExtensions() || [];
    const sensitiveHidden = debugExtension === null && !extensions.includes('WEBGL_debug_renderer_info');
    const genericIdentity = /^(hidden|null|undefined|WebKit|OTF Browser)$/i.test(String(vendor)) &&
      /^(hidden|null|undefined|WebKit WebGL|OTF WebGL)$/i.test(String(renderer));
    const profileStatus = sensitiveHidden ? 'ok' : genericIdentity ? 'warn' : 'fail';

    ctx.set('webgl-profile', profileStatus,
      profileStatus === 'ok' ? 'GPU identity hidden'
        : profileStatus === 'warn' ? 'Generic GPU identity' : 'Raw GPU identity exposed',
      `Vendor: ${String(vendor)}, renderer: ${String(renderer)}`,
      [
        ['debug extension', String(debugExtension)],
        ['unmasked vendor', String(vendor)],
        ['unmasked renderer', String(renderer)],
        ['extension listed', String(extensions.includes('WEBGL_debug_renderer_info'))],
      ]);
    ctx.set('webgl-debug', sensitiveHidden ? 'ok' : 'fail',
      sensitiveHidden ? 'Debug renderer hidden' : 'Debug renderer available',
      `WEBGL_debug_renderer_info listed: ${extensions.includes('WEBGL_debug_renderer_info')}`,
      [['extensions count', String(extensions.length)]]);
  },
};
