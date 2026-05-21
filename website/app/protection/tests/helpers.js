// Shared utilities for protection tests. Pure functions only; modules
// import what they need.

export const storageKeys = {
  canvas: 'browserFingerprintTest:canvasHistory',
  webgl: 'browserFingerprintTest:webglHistory',
  layout: 'browserFingerprintTest:layoutHistory',
};

const MAX_SESSIONS = 5;

// Maintains a ring buffer of per-session hashes in localStorage.
// sessionStorage holds an "already recorded this session" flag so a reload
// of the same page doesn't push a duplicate entry.
export const trackSessionHistory = (key, hash) => {
  const sessionFlag = key + ':session';
  let history = [];
  try { history = JSON.parse(localStorage.getItem(key) || '[]'); } catch (_) {}
  if (!Array.isArray(history)) history = [];
  const alreadyRecorded = sessionStorage.getItem(sessionFlag) !== null;
  if (!alreadyRecorded) {
    sessionStorage.setItem(sessionFlag, hash);
    history.push(hash);
    if (history.length > MAX_SESSIONS) history = history.slice(-MAX_SESSIONS);
    localStorage.setItem(key, JSON.stringify(history));
  }
  return history;
};

export const hashText = async (text) => {
  if (globalThis.crypto && crypto.subtle) {
    const bytes = new TextEncoder().encode(text);
    const digest = await crypto.subtle.digest('SHA-256', bytes);
    return [...new Uint8Array(digest)]
      .map((byte) => byte.toString(16).padStart(2, '0'))
      .join('');
  }
  let hash = 2166136261;
  for (let i = 0; i < text.length; i += 1) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0).toString(16);
};

export const hashBlob = async (blob) => {
  if (!blob) return 'unavailable';
  const buffer = await blob.arrayBuffer();
  return hashText([...new Uint8Array(buffer)].join(','));
};

export const canvasToBlob = (canvas) => new Promise((resolve) => {
  if (!canvas || typeof canvas.toBlob !== 'function') {
    resolve(null);
    return;
  }
  canvas.toBlob((blob) => resolve(blob), 'image/png');
});

export const functionLooksWrapped = (fn) =>
  typeof fn === 'function' && !/\[native code\]/.test(Function.prototype.toString.call(fn));

export const short = (value) => String(value).slice(0, 24);

// Draws the canonical browser-fingerprint test scene into a 2d context.
// Same shapes as the original test page so cross-version comparisons line up.
export const drawFingerprintScene = (canvas) => {
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = '#fffaf0';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#102820';
  ctx.font = '700 28px Georgia';
  ctx.fillText('Browser fingerprint test', 24, 48);
  ctx.fillStyle = '#c86f2d';
  ctx.fillRect(24, 76, 160, 24);
  ctx.fillStyle = '#1a6b8f';
  ctx.beginPath();
  ctx.arc(260, 82, 34, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = '#16110b';
  ctx.lineWidth = 4;
  ctx.strokeRect(330, 24, 56, 82);
};

// Runs the canonical canvas-fingerprint scene inside a fresh, hidden iframe
// and returns the resulting hashes. Used to verify canvas noise actually
// differs between contexts within the same page.
export const runCanvasContextProbe = () => new Promise((resolve) => {
  const token = `canvas-probe-${Date.now()}-${Math.random()}`;
  const iframe = document.createElement('iframe');
  iframe.hidden = true;
  iframe.setAttribute('aria-hidden', 'true');

  const cleanup = () => {
    window.removeEventListener('message', onMessage);
    iframe.remove();
  };
  const onMessage = (event) => {
    if (!event.data || event.data.token !== token) return;
    cleanup();
    resolve(event.data.result);
  };

  window.addEventListener('message', onMessage);
  iframe.srcdoc = `
    <!doctype html>
    <meta charset="utf-8">
    <canvas id="canvas" width="420" height="140"></canvas>
    <script>
      const token = ${JSON.stringify(token)};
      const hashText = (text) => {
        let hash = 2166136261;
        for (let i = 0; i < text.length; i += 1) {
          hash ^= text.charCodeAt(i);
          hash = Math.imul(hash, 16777619);
        }
        return (hash >>> 0).toString(16);
      };
      const hashBytes = (bytes) => hashText(Array.from(bytes).join(','));
      const blobToHash = (blob) => new Promise((resolve) => {
        if (!blob) { resolve('unavailable'); return; }
        const reader = new FileReader();
        reader.onload = () => resolve(hashText(String(reader.result || '')));
        reader.onerror = () => resolve('read-error');
        reader.readAsDataURL(blob);
      });
      const canvasToBlob = (canvas) => new Promise((resolve) => {
        if (!canvas || typeof canvas.toBlob !== 'function') { resolve(null); return; }
        canvas.toBlob((blob) => resolve(blob), 'image/png');
      });
      const drawCanvas = (canvas) => {
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = '#fffaf0';
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        ctx.fillStyle = '#102820';
        ctx.font = '700 28px Georgia';
        ctx.fillText('Browser fingerprint test', 24, 48);
        ctx.fillStyle = '#c86f2d';
        ctx.fillRect(24, 76, 160, 24);
        ctx.fillStyle = '#1a6b8f';
        ctx.beginPath();
        ctx.arc(260, 82, 34, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#16110b';
        ctx.lineWidth = 4;
        ctx.strokeRect(330, 24, 56, 82);
      };
      (async () => {
        const canvas = document.getElementById('canvas');
        drawCanvas(canvas);
        const ctx = canvas.getContext('2d');
        const result = {
          toDataURL: hashText(canvas.toDataURL('image/png')),
          toBlob: await blobToHash(await canvasToBlob(canvas)),
          getImageData: hashBytes(ctx.getImageData(0, 0, canvas.width, canvas.height).data),
          offscreenConvertToBlob: 'unavailable',
          offscreenTransferToImageBitmap: 'unavailable'
        };
        if (typeof OffscreenCanvas === 'function') {
          try {
            const offscreen = new OffscreenCanvas(canvas.width, canvas.height);
            const offscreenContext = offscreen.getContext('2d');
            if (offscreenContext) {
              offscreenContext.drawImage(canvas, 0, 0);
              if (typeof offscreen.convertToBlob === 'function') {
                result.offscreenConvertToBlob = await blobToHash(
                  await offscreen.convertToBlob({ type: 'image/png' })
                );
              }
              if (typeof offscreen.transferToImageBitmap === 'function') {
                const bitmap = offscreen.transferToImageBitmap();
                const bitmapCanvas = document.createElement('canvas');
                bitmapCanvas.width = bitmap.width;
                bitmapCanvas.height = bitmap.height;
                const bitmapContext = bitmapCanvas.getContext('2d');
                if (bitmapContext) {
                  bitmapContext.drawImage(bitmap, 0, 0);
                  result.offscreenTransferToImageBitmap = hashText(
                    bitmapCanvas.toDataURL('image/png')
                  );
                }
                if (typeof bitmap.close === 'function') bitmap.close();
              }
            }
          } catch (error) {
            const value = error.name + ': ' + error.message;
            if (result.offscreenConvertToBlob === 'unavailable') result.offscreenConvertToBlob = value;
            if (result.offscreenTransferToImageBitmap === 'unavailable') result.offscreenTransferToImageBitmap = value;
          }
        }
        parent.postMessage({ token, result }, '*');
      })().catch((error) => {
        parent.postMessage({ token, result: { error: error.name + ': ' + error.message } }, '*');
      });
    <\/script>
  `;
  document.body.appendChild(iframe);
  setTimeout(() => { cleanup(); resolve({ error: 'probe-timeout' }); }, 5000);
});

export const runWebGLContextProbe = () => new Promise((resolve) => {
  const token = `webgl-probe-${Date.now()}-${Math.random()}`;
  const iframe = document.createElement('iframe');
  iframe.hidden = true;
  iframe.setAttribute('aria-hidden', 'true');
  const cleanup = () => {
    window.removeEventListener('message', onMessage);
    iframe.remove();
  };
  const onMessage = (event) => {
    if (!event.data || event.data.token !== token) return;
    cleanup();
    resolve(event.data.result);
  };
  window.addEventListener('message', onMessage);
  iframe.srcdoc = `
    <!doctype html>
    <meta charset="utf-8">
    <script>
      const token = ${JSON.stringify(token)};
      const hashText = (text) => {
        let hash = 2166136261;
        for (let i = 0; i < text.length; i += 1) {
          hash ^= text.charCodeAt(i);
          hash = Math.imul(hash, 16777619);
        }
        return (hash >>> 0).toString(16);
      };
      const renderHash = (gl) => {
        gl.clearColor(0.18, 0.52, 0.38, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        const pixels = new Uint8Array(64 * 64 * 4);
        gl.readPixels(0, 0, 64, 64, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        return hashText(Array.from(pixels).join(','));
      };
      const createCanvas = () => {
        const canvas = document.createElement('canvas');
        canvas.width = 64; canvas.height = 64;
        return canvas;
      };
      try {
        const canvas = createCanvas();
        const gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
        const webgl2Canvas = createCanvas();
        const gl2 = webgl2Canvas.getContext('webgl2');
        const result = {
          webgl1ReadPixels: gl ? renderHash(gl) : 'unavailable',
          webgl1Export: gl ? hashText(canvas.toDataURL('image/png')) : 'unavailable',
          webgl2ReadPixels: gl2 ? renderHash(gl2) : 'unavailable',
          offscreenWebGL: 'unavailable'
        };
        if (typeof OffscreenCanvas === 'function') {
          const offscreen = new OffscreenCanvas(64, 64);
          const offscreenGl = offscreen.getContext('webgl') || offscreen.getContext('webgl2');
          if (offscreenGl) result.offscreenWebGL = renderHash(offscreenGl);
        }
        parent.postMessage({ token, result }, '*');
      } catch (error) {
        parent.postMessage({ token, result: { error: error.name + ': ' + error.message } }, '*');
      }
    <\/script>
  `;
  document.body.appendChild(iframe);
  setTimeout(() => { cleanup(); resolve({ error: 'probe-timeout' }); }, 5000);
});
