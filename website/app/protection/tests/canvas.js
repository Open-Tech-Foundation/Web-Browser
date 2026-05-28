import {
  hashText, hashBlob, canvasToBlob, runCanvasContextProbe,
  trackSessionHistory, storageKeys, drawFingerprintScene, short,
} from "./helpers.js";

export default {
  module: 'canvas',
  category: 'privacy',
  produces: [
    {
      id: 'canvas-fingerprint',
      label: 'Canvas fingerprint',
      entropy: 'high',
      description: '2D canvas rendering hash — classic GPU/driver/font tell.',
    },
    {
      id: 'canvas-bitmap-transfer',
      label: 'Canvas bitmap transfer',
      entropy: 'high',
      description: 'OffscreenCanvas.transferToImageBitmap can bypass canvas noise.',
    },
  ],
  async run(ctx) {
    // Render into an offscreen canvas (the preview canvas in the UI is
    // drawn separately by the page when the row is expanded).
    const canvas = document.createElement('canvas');
    canvas.width = 420;
    canvas.height = 140;
    drawFingerprintScene(canvas);

    const attempts = [];
    for (let i = 0; i < 3; i += 1) attempts.push(canvas.toDataURL('image/png'));
    const hashes = [];
    for (const dataUrl of attempts) hashes.push(await hashText(dataUrl));
    const dataUrl = attempts[0];
    const hash = hashes[0];
    const blobHash = await hashBlob(await canvasToBlob(canvas));
    const c2d = canvas.getContext('2d');
    let imageDataHash = 'unavailable';
    if (c2d && typeof c2d.getImageData === 'function') {
      try {
        const imageData = c2d.getImageData(0, 0, canvas.width, canvas.height);
        imageDataHash = await hashText([...imageData.data].join(','));
      } catch (error) {
        imageDataHash = `${error.name}: ${error.message}`;
      }
    }
    let offscreenBlobHash = 'unavailable';
    let offscreenBitmapHash = 'unavailable';
    if (typeof OffscreenCanvas === 'function') {
      try {
        const offscreen = new OffscreenCanvas(canvas.width, canvas.height);
        const offscreenContext = offscreen.getContext('2d');
        if (offscreenContext) {
          offscreenContext.drawImage(canvas, 0, 0);
          if (typeof offscreen.convertToBlob === 'function') {
            offscreenBlobHash = await hashBlob(await offscreen.convertToBlob({ type: 'image/png' }));
          }
          if (typeof offscreen.transferToImageBitmap === 'function') {
            const bitmap = offscreen.transferToImageBitmap();
            const bitmapCanvas = document.createElement('canvas');
            bitmapCanvas.width = bitmap.width;
            bitmapCanvas.height = bitmap.height;
            const bitmapContext = bitmapCanvas.getContext('2d');
            if (bitmapContext) {
              bitmapContext.drawImage(bitmap, 0, 0);
              offscreenBitmapHash = await hashText(bitmapCanvas.toDataURL('image/png'));
            }
            if (typeof bitmap.close === 'function') bitmap.close();
          }
        }
      } catch (error) {
        const value = `${error.name}: ${error.message}`;
        if (offscreenBlobHash === 'unavailable') offscreenBlobHash = value;
        if (offscreenBitmapHash === 'unavailable') offscreenBitmapHash = value;
      }
    }
    const contextProbes = await Promise.all([runCanvasContextProbe(), runCanvasContextProbe()]);
    const methodNames = ['toDataURL', 'toBlob', 'getImageData', 'offscreenConvertToBlob', 'offscreenTransferToImageBitmap'];
    const changedMethods = methodNames.filter((name) =>
      contextProbes[0] && contextProbes[1] &&
      contextProbes[0][name] && contextProbes[1][name] &&
      contextProbes[0][name] !== 'unavailable' && contextProbes[1][name] !== 'unavailable' &&
      contextProbes[0][name] !== contextProbes[1][name]);
    const probeError = contextProbes.find((p) => p && p.error)?.error;
    const uniqueAttempts = new Set(hashes).size;
    const { history, isFirstSession } = trackSessionHistory(storageKeys.canvas, hash);
    const uniqueSessionHashes = new Set(history).size;
    const sessionCount = history.length;
    const allUnique = uniqueSessionHashes === sessionCount;
    const contextChanged = changedMethods.length > 0;
    const bitmapTransferProtected = changedMethods.includes('offscreenTransferToImageBitmap');

    ctx.set('canvas-bitmap-transfer',
      bitmapTransferProtected ? 'ok' : 'fail',
      bitmapTransferProtected ? 'Bitmap transfer output varies' : 'Bitmap transfer unprotected',
      'OffscreenCanvas.transferToImageBitmap bypasses canvas noise — pixels read from the resulting bitmap are unperturbed.',
      [
        ['offscreen transferToImageBitmap hash', offscreenBitmapHash],
        ['changed methods', changedMethods.join(', ') || 'none'],
      ]);

    const status = isFirstSession
      ? 'pending-restart'
      : sessionCount < 2
        ? (contextChanged ? 'ok' : 'warn')
        : allUnique ? 'ok' : 'fail';
    ctx.set('canvas-fingerprint', status,
      isFirstSession
        ? 'Restart your browser and re-run to compare across sessions'
        : sessionCount < 2
          ? (contextChanged ? 'Changes across automated fresh contexts' : 'Awaiting second session for baseline')
          : allUnique
            ? `All ${sessionCount} sessions produced different canvas hashes`
            : 'Duplicate canvas hash across sessions — noise may be stable',
      `Unique session hashes: ${uniqueSessionHashes}/${sessionCount}, changed context methods: ${changedMethods.length}`,
      [
        ['session history (newest last)', history.join(', ')],
        ['sessions recorded', String(sessionCount)],
        ['unique session hashes', `${uniqueSessionHashes}/${sessionCount}`],
        ['toDataURL attempt hashes', hashes.join(', ')],
        ['toDataURL unique attempts', `${uniqueAttempts}/3`],
        ['toBlob hash', blobHash],
        ['getImageData hash', imageDataHash],
        ['OffscreenCanvas convertToBlob hash', offscreenBlobHash],
        ['OffscreenCanvas transferToImageBitmap hash', offscreenBitmapHash],
        ['automated context probe 1', JSON.stringify(contextProbes[0])],
        ['automated context probe 2', JSON.stringify(contextProbes[1])],
        ['automated changed methods', changedMethods.join(', ') || 'none'],
        ['automated probe error', probeError || 'none'],
        ['data URL prefix', short(dataUrl)],
      ]);

    // Hand the page a "preview" hook so it can draw the same scene into a
    // canvas in the expanded row, without baking <canvas> into the JSX.
    ctx.setExtra('canvas-fingerprint', { previewKind: 'canvas-fingerprint' });
  },
};
