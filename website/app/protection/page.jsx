import { onMount } from "@opentf/web";

export default function FingerprintsPage() {
  onMount(() => {
    const storageKeys = {
      canvas: 'browserFingerprintTest:canvasHistory',
      webgl: 'browserFingerprintTest:webglHistory',
      layout: 'browserFingerprintTest:layoutHistory'
    };

    // Maintains a ring buffer of per-session hashes in localStorage.
    // sessionStorage is used as a "already recorded this session" flag so
    // reloading the same page doesn't push a duplicate entry.
    const MAX_SESSIONS = 5;
    const trackSessionHistory = (key, hash) => {
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

    const short = (value) => String(value).slice(0, 24);
    const scoreValue = {
      ok: 1,
      warn: 0.5,
      fail: 0
    };

    const updateReportScore = () => {
      const items = [...document.querySelectorAll('[data-report-item]')];
      // 'pending' is a real status for tests that need a user gesture
      // (e.g. local-font-api). Treat it the same as 'checking' — not yet
      // complete, doesn't count toward the score, holds back the export
      // button until the user actually runs it.
      const scoredItems = items.filter((item) =>
        item.dataset.status &&
        item.dataset.status !== 'checking' &&
        item.dataset.status !== 'pending');
      const total = items.length;
      const current = scoredItems.reduce((sum, item) => sum + (scoreValue[item.dataset.status] ?? 0), 0);
      const score = total ? Math.round((current / total) * 100) : 0;
      const scoreNode = document.getElementById('fingerprint-score-value');
      const labelNode = document.getElementById('fingerprint-score-label');
      const fillNode = document.getElementById('fingerprint-score-fill');
      if (!scoreNode || !labelNode || !fillNode) return;
      scoreNode.textContent = `${score}`;
      labelNode.textContent = `${scoredItems.length}/${total} tests complete`;
      fillNode.style.width = `${score}%`;
      if (total > 0 && scoredItems.length === total) {
        const exportBtn = document.getElementById('export-json-btn');
        if (exportBtn) exportBtn.hidden = false;
        const startBtn = document.getElementById('start-tests-btn');
        if (startBtn) {
          startBtn.disabled = false;
          startBtn.textContent = 'Re-run Test Suite';
        }
      }
    };

    const setActiveSection = (section) => {
      document.querySelectorAll('[data-section-tab]').forEach((button) => {
        const active = button.dataset.sectionTab === section;
        button.classList.toggle('active', active);
        button.setAttribute('aria-selected', String(active));
      });
      document.querySelectorAll('[data-test-section]').forEach((node) => {
        node.hidden = node.dataset.testSection !== section;
      });
    };

    const setReportItem = (id, status, behavior, detail) => {
      const item = document.querySelector(`[data-report-item="${id}"]`);
      if (!item) return;
      const icon = item.querySelector('.report-icon');
      const behaviorNode = item.querySelector('.report-behavior');
      const detailNode = item.querySelector('.report-detail');
      item.dataset.status = status;
      icon.className = 'report-icon ' + status;
      icon.textContent = status === 'ok' ? '✓'
        : status === 'fail' ? '×'
        : status === 'pending' ? '?'
        : '!';
      behaviorNode.textContent = behavior;
      detailNode.textContent = detail || '';
      updateReportScore();
    };

    const setCard = (id, status, label, rows) => {
      const card = document.getElementById(id);
      if (!card) return;
      const badge = card.querySelector('.status');
      const dl = card.querySelector('dl');
      badge.className = 'status ' + status;
      badge.textContent = label;
      dl.innerHTML = '';
      for (const [key, value] of rows) {
        const dt = document.createElement('dt');
        const dd = document.createElement('dd');
        dt.textContent = key;
        dd.textContent = value;
        dl.append(dt, dd);
      }
    };

    const setAdvancedFontCard = (status, label, cases, details) => {
      const card = document.getElementById('font-advanced-card');
      if (!card) return;
      const badge = card.querySelector('.status');
      const dl = card.querySelector('dl');
      badge.className = 'status ' + status;
      badge.textContent = label;
      dl.innerHTML = '';

      const list = document.createElement('div');
      list.className = 'case-list';
      for (const item of cases) {
        const row = document.createElement('section');
        row.className = 'case-row ' + item.status.toLowerCase();

        const heading = document.createElement('div');
        heading.className = 'case-heading';
        const title = document.createElement('strong');
        title.textContent = item.title;
        const pill = document.createElement('span');
        pill.className = 'case-pill ' + item.status.toLowerCase();
        pill.textContent = item.status;
        heading.append(title, pill);

        const expected = document.createElement('p');
        expected.innerHTML = `<span>Expected</span>${item.expected}`;
        const current = document.createElement('p');
        current.innerHTML = `<span>Current</span>${item.current}`;
        const why = document.createElement('p');
        why.innerHTML = `<span>Why</span>${item.why}`;

        row.append(heading, expected, current, why);
        list.append(row);
      }
      dl.append(list);

      for (const [key, value] of details) {
        const dt = document.createElement('dt');
        const dd = document.createElement('dd');
        dt.textContent = key;
        dd.textContent = value;
        dl.append(dt, dd);
      }
    };

    const hashText = async (text) => {
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

    const hashBlob = async (blob) => {
      if (!blob) return 'unavailable';
      const buffer = await blob.arrayBuffer();
      return hashText([...new Uint8Array(buffer)].join(','));
    };

    const canvasToBlob = (canvas) => new Promise((resolve) => {
      if (!canvas || typeof canvas.toBlob !== 'function') {
        resolve(null);
        return;
      }
      canvas.toBlob((blob) => resolve(blob), 'image/png');
    });

    const runCanvasContextProbe = () => new Promise((resolve) => {
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
            if (!blob) {
              resolve('unavailable');
              return;
            }
            const reader = new FileReader();
            reader.onload = () => resolve(hashText(String(reader.result || '')));
            reader.onerror = () => resolve('read-error');
            reader.readAsDataURL(blob);
          });
          const canvasToBlob = (canvas) => new Promise((resolve) => {
            if (!canvas || typeof canvas.toBlob !== 'function') {
              resolve(null);
              return;
            }
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
                if (result.offscreenConvertToBlob === 'unavailable') {
                  result.offscreenConvertToBlob = value;
                }
                if (result.offscreenTransferToImageBitmap === 'unavailable') {
                  result.offscreenTransferToImageBitmap = value;
                }
              }
            }
            parent.postMessage({ token, result }, '*');
          })().catch((error) => {
            parent.postMessage({
              token,
              result: { error: error.name + ': ' + error.message }
            }, '*');
          });
        <\/script>
      `;
      document.body.appendChild(iframe);

      setTimeout(() => {
        cleanup();
        resolve({ error: 'probe-timeout' });
      }, 5000);
    });

    const runWebGLContextProbe = () => new Promise((resolve) => {
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
            canvas.width = 64;
            canvas.height = 64;
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
              if (offscreenGl) {
                result.offscreenWebGL = renderHash(offscreenGl);
              }
            }
            parent.postMessage({ token, result }, '*');
          } catch (error) {
            parent.postMessage({
              token,
              result: { error: error.name + ': ' + error.message }
            }, '*');
          }
        <\/script>
      `;
      document.body.appendChild(iframe);

      setTimeout(() => {
        cleanup();
        resolve({ error: 'probe-timeout' });
      }, 5000);
    });

    const functionLooksWrapped = (fn) =>
      typeof fn === 'function' && !/\[native code\]/.test(Function.prototype.toString.call(fn));

    const runSideEffectsTest = () => {
      // 1. canvas.measureText — web fonts normalized to Arial
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

      // 2. document.fonts.check — non-allowed fonts always return false
      let arialCheck = 'unavailable', robotoCheck = 'unavailable';
      try {
        if (document.fonts && typeof document.fonts.check === 'function') {
          arialCheck = document.fonts.check('12px Arial');
          robotoCheck = document.fonts.check('12px "Roboto"');
        }
      } catch (_) {}
      const fontCheckBlocked = robotoCheck === false;

      // 3. getImageData pixel noise — solid red canvas should return 255 red, noise shifts it
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

      setReportItem('getImageData-noise',
        noiseActive ? 'ok' : getImageDataWrapped === false ? 'fail' : 'warn',
        noiseActive ? 'getImageData pixel noise active' : getImageDataWrapped === false ? 'getImageData not wrapped by policy' : 'getImageData wrapped but noise not visible',
        `deviation: ${pixelDeviation}, wrapped: ${getImageDataWrapped}`);
      const allActive = measureNormalized && fontCheckBlocked && noiseActive;
      setCard('side-effects-card',
        allActive ? 'ok' : noiseActive === false ? 'fail' : 'warn',
        allActive ? 'All surfaces active' : 'Some surfaces inactive', [
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
          ['note', 'Chart.js/D3 labels affected by measureText; color pickers affected by pixel noise']
        ]);
    };

    const runInjectionTest = () => {
      const injected = Boolean(globalThis.__otfPagePolicyInjected);
      const canvasWrapped = functionLooksWrapped(globalThis.CanvasRenderingContext2D?.prototype?.getImageData);
      const toDataUrlWrapped = functionLooksWrapped(globalThis.HTMLCanvasElement?.prototype?.toDataURL);
      const webglWrapped = functionLooksWrapped(globalThis.WebGLRenderingContext?.prototype?.getParameter);
      const workerWrapped = functionLooksWrapped(globalThis.Worker);
      const workerAvailable = typeof globalThis.Worker === 'function';
      setReportItem('worker-surface', workerWrapped ? 'ok' : workerAvailable ? 'warn' : 'ok',
        workerWrapped ? 'Worker constructor protected' : workerAvailable ? 'Worker API available' : 'Worker API unavailable',
        workerAvailable ? 'Worker contexts can expose additional fingerprinting surfaces.' : 'No Worker constructor was exposed.');
      setCard('policy-card', injected ? 'ok' : 'warn',
        injected ? 'Browser policy marker detected' : 'Generic browser runtime', [
          ['user agent', navigator.userAgent],
          ['global marker', String(injected)],
          ['canvas getImageData wrapped', String(canvasWrapped)],
          ['canvas toDataURL wrapped', String(toDataUrlWrapped)],
          ['webgl getParameter wrapped', String(webglWrapped)],
          ['worker constructor wrapped', String(workerWrapped)]
        ]);
    };

    const runScreenTest = () => {
      const width = Number(screen.width);
      const height = Number(screen.height);
      const availWidth = Number(screen.availWidth);
      const availHeight = Number(screen.availHeight);
      const dpr = Number(globalThis.devicePixelRatio || 1);
      const colorDepth = Number(screen.colorDepth);
      const pixelDepth = Number(screen.pixelDepth);
      const profile = globalThis.__otfScreenProfile;
      const knownProfiles = new Set([
        '1280x720@1', '1280x720@2',
        '1280x800@1', '1280x800@2',
        '1280x1024@1',
        '1360x768@1',
        '1366x768@1',
        '1440x900@1', '1440x900@2',
        '1600x900@1', '1600x900@2',
        '1680x1050@1',
        '1920x1080@1', '1920x1080@2',
        '1920x1200@1',
        '2560x1080@1',
        '2560x1440@1', '2560x1440@2',
        '2880x1800@2',
        '3440x1440@1',
        '3840x2160@1', '3840x2160@2',
      ]);
      const signature = `${width}x${height}@${dpr}`;
      const commonProfile = knownProfiles.has(signature);
      const validGeometry =
        width > 0 &&
        height > 0 &&
        availWidth > 0 &&
        availHeight > 0 &&
        availWidth <= width &&
        availHeight <= height &&
        dpr > 0;
      const validDepth =
        colorDepth > 0 &&
        pixelDepth > 0 &&
        colorDepth === pixelDepth;
      const internallyConsistent = validGeometry && validDepth;
      const status = profile || (commonProfile && internallyConsistent)
        ? 'ok'
        : internallyConsistent
          ? 'warn'
          : 'fail';
      setReportItem('screen-dimensions', status,
        status === 'ok' ? 'Common screen profile' : status === 'warn' ? 'Raw or uncommon profile' : 'Invalid screen values',
        `${width} × ${height}, available ${availWidth} × ${availHeight}, DPR ${dpr}x`);
      setCard('screen-card', status,
        status === 'ok' ? 'Common profile' : status === 'warn' ? 'Raw or uncommon profile' : 'Invalid values', [
          ['resolution', `${width} × ${height}`],
          ['available area', `${availWidth} × ${availHeight}`],
          ['device pixel ratio', `${dpr}x`],
          ['color depth', `${colorDepth} bits`],
          ['pixel depth', `${pixelDepth} bits`],
          ['known bucket', String(commonProfile)],
          ['valid geometry', String(validGeometry)],
          ['valid color depth', String(validDepth)],
          ['browser profile', profile ? JSON.stringify(profile) : 'none']
        ]);
    };

    const runHardwareTest = () => {
      const cores = Number(navigator.hardwareConcurrency || 0);
      const memory = Number(navigator.deviceMemory || 0);
      const profile = globalThis.__otfHardwareProfile;
      const normalizedCores = [2, 4, 8].includes(cores);
      const normalizedMemory = [0.5, 1, 2, 4, 8].includes(memory);
      const normalized = normalizedCores && normalizedMemory;
      const valid =
        Number.isFinite(cores) &&
        Number.isFinite(memory) &&
        cores > 0 &&
        memory > 0;
      const status = normalized ? 'ok' : valid ? 'warn' : 'fail';
      setReportItem('hardware-profile', status,
        normalized ? 'Normalized hardware profile' : valid ? 'Raw hardware profile' : 'Invalid hardware values',
        `CPU cores: ${cores || 'unavailable'}, memory: ${memory || 'unavailable'} GB`);
      setCard('hardware-card', status,
        normalized ? 'Normalized profile' : valid ? 'Raw profile' : 'Invalid values', [
          ['hardware concurrency', cores ? String(cores) : 'unavailable'],
          ['device memory', memory ? `${memory} GB` : 'unavailable'],
          ['cores normalized', String(normalizedCores)],
          ['memory normalized', String(normalizedMemory)],
          ['browser profile', profile ? JSON.stringify(profile) : 'none']
        ]);
    };

    const runFontTest = () => {
      const allowedFonts = ['Arial', 'Helvetica', 'Times New Roman', 'Courier New'];
      const probeFonts = ['Calibri', 'Segoe UI', 'Roboto', 'Ubuntu', 'Noto Sans', 'DejaVu Sans', 'SF Pro Display'];
      const profile = globalThis.__otfFontProfile;
      const fontSet = document.fonts;
      const fontCheckAvailable = !!(fontSet && typeof fontSet.check === 'function');
      const checkFont = (name) => {
        try {
          return fontCheckAvailable && fontSet.check(`12px "${name}"`);
        } catch {
          return false;
        }
      };
      const allowedDetected = allowedFonts.filter(checkFont);
      const extraDetected = probeFonts.filter(checkFont);
      const fontSetSize = fontSet && typeof fontSet.size === 'number' ? fontSet.size : 'unavailable';
      let enumeratedFonts = [];
      try {
        if (fontSet && typeof fontSet.forEach === 'function') {
          fontSet.forEach((fontFace) => {
            if (fontFace && fontFace.family) {
              enumeratedFonts.push(fontFace.family);
            }
          });
        }
      } catch {}
      enumeratedFonts = [...new Set(enumeratedFonts)];

      const canvas = document.createElement('canvas');
      const ctx = canvas.getContext('2d');
      let canvasNormalized = false;
      let arialWidth = 'unavailable';
      let rareWidth = 'unavailable';
      if (ctx && typeof ctx.measureText === 'function') {
        ctx.font = '72px Arial';
        arialWidth = ctx.measureText('mmmmmmmmmm').width;
        ctx.font = '72px "Calibri"';
        rareWidth = ctx.measureText('mmmmmmmmmm').width;
        canvasNormalized = Math.abs(arialWidth - rareWidth) < 0.01;
      }

      const metricText = 'mmmMMMmmmlllmmmLLL₹▁₺₸ẞॿmmmiiimmmIIImmmwwwmmmWWW';
      const measureDomFont = (fontFamily) => {
        const node = document.createElement('span');
        node.textContent = metricText;
        node.style.cssText = [
          'position:absolute',
          'left:-10000px',
          'top:-10000px',
          'white-space:nowrap',
          'font-size:72px',
          `font-family:${fontFamily}, monospace`
        ].join(';');
        document.body.appendChild(node);
        const rect = node.getBoundingClientRect();
        const result = {
          width: rect.width,
          height: rect.height,
          display: `${Math.round(rect.width * 100) / 100}x${Math.round(rect.height * 100) / 100}`
        };
        node.remove();
        return result;
      };
      const domMetrics = probeFonts.map((font) => [font, measureDomFont(`"${font}"`)]);
      const domUniqueMetrics = new Set(domMetrics.map(([, metric]) => metric.display)).size;
      const domWidths = domMetrics.map(([, metric]) => metric.width);
      const domHeights = domMetrics.map(([, metric]) => metric.height);
      const domWidthRange = Math.max(...domWidths) - Math.min(...domWidths);
      const domHeightRange = Math.max(...domHeights) - Math.min(...domHeights);
      const domMetricsNormalized = domWidthRange < 1 && domHeightRange < 1;

      const limitedApi =
        fontCheckAvailable &&
        extraDetected.length === 0 &&
        allowedDetected.length <= allowedFonts.length &&
        enumeratedFonts.length <= allowedFonts.length;
      const status = limitedApi && canvasNormalized && domMetricsNormalized
        ? 'ok'
        : limitedApi && (canvasNormalized || domMetricsNormalized)
          ? 'warn'
          : 'fail';
      setReportItem('font-surface', status,
        status === 'ok' ? 'Font probing limited' : status === 'warn' ? 'Font API partially limited' : 'Additional fonts exposed',
        `Allowed detected: ${allowedDetected.length}, extra detected: ${extraDetected.length}, DOM range: ${domWidthRange.toFixed(2)}x${domHeightRange.toFixed(2)}`);
      setCard('font-card', status,
        status === 'ok' ? 'Limited font surface' : status === 'warn' ? 'Partially limited' : 'Extra fonts exposed', [
          ['document.fonts.check', fontCheckAvailable ? 'available' : 'unavailable'],
          ['font set size', String(fontSetSize)],
          ['allowed fonts detected', allowedDetected.join(', ') || 'none'],
          ['extra probe fonts detected', extraDetected.join(', ') || 'none'],
          ['enumerated fonts', enumeratedFonts.join(', ') || 'none'],
          ['canvas Arial width', String(arialWidth)],
          ['canvas Calibri width', String(rareWidth)],
          ['canvas measurement normalized', String(canvasNormalized)],
          ['DOM font metrics unique count', String(domUniqueMetrics)],
          ['DOM font metrics width range', domWidthRange.toFixed(4)],
          ['DOM font metrics height range', domHeightRange.toFixed(4)],
          ['DOM font metrics normalized', String(domMetricsNormalized)],
          ['DOM font metrics probes', domMetrics.map(([font, metric]) => `${font}: ${metric.display}`).join(' | ')],
          ['browser profile', profile ? JSON.stringify(profile) : 'none']
        ]);
    };

    const runLayoutMetricsTest = async () => {
      const node = document.createElement('span');
      node.textContent = 'mmmMMMmmmlllmmmLLL₹▁₺₸ẞॿmmmiiimmmIIImmmwwwmmmWWW';
      node.style.cssText = [
        'position:absolute',
        'left:-10000px',
        'top:-10000px',
        'white-space:nowrap',
        'font-size:72px',
        'font-family:"Calibri", monospace'
      ].join(';');
      document.body.appendChild(node);

      const rects = [];
      for (let i = 0; i < 8; i += 1) {
        const rect = node.getBoundingClientRect();
        rects.push({
          x: Number(rect.x).toFixed(4),
          y: Number(rect.y).toFixed(4),
          width: Number(rect.width).toFixed(4),
          height: Number(rect.height).toFixed(4),
          left: Number(rect.left).toFixed(4),
          top: Number(rect.top).toFixed(4),
          right: Number(rect.right).toFixed(4),
          bottom: Number(rect.bottom).toFixed(4)
        });
      }
      node.remove();

      const widthValues = rects.map((rect) => rect.width);
      const heightValues = rects.map((rect) => rect.height);
      const uniqueWidths = new Set(widthValues).size;
      const uniqueHeights = new Set(heightValues).size;
      const uniqueRects = new Set(rects.map((rect) => JSON.stringify(rect))).size;
      const hash = await hashText(JSON.stringify(rects));
      const history = trackSessionHistory(storageKeys.layout, hash);
      const uniqueSessionHashes = new Set(history).size;
      const sessionCount = history.length;
      const allSessionsUnique = uniqueSessionHashes === sessionCount;
      const status = sessionCount < 2 ? 'warn' : allSessionsUnique ? 'ok' : 'fail';

      setReportItem('layout-metrics', status,
        sessionCount < 2
          ? 'Awaiting second session for baseline'
          : allSessionsUnique
            ? `All ${sessionCount} sessions produced different layout hashes`
            : 'Duplicate layout hash found across sessions — noise may be stable',
        `Unique rects this run: ${uniqueRects}/8, sessions recorded: ${sessionCount}, unique session hashes: ${uniqueSessionHashes}/${sessionCount}`);
      setCard('layout-card', status,
        sessionCount < 2 ? 'Need another session' : allSessionsUnique ? 'Cross-session noise confirmed' : 'Session hash collision', [
          ['session history (newest last)', history.join(', ')],
          ['sessions recorded', String(sessionCount)],
          ['unique session hashes', `${uniqueSessionHashes}/${sessionCount}`],
          ['all sessions unique', String(allSessionsUnique)],
          ['unique rects this run', `${uniqueRects}/8`],
          ['unique widths', String(uniqueWidths)],
          ['unique heights', String(uniqueHeights)],
          ['width samples', widthValues.join(', ')],
          ['height samples', heightValues.join(', ')],
          ['first rect', JSON.stringify(rects[0])],
          ['last rect', JSON.stringify(rects[rects.length - 1])]
        ]);
    };

    const runAdvancedFontSurfaceTest = async () => {
      const probeFonts = ['Calibri', 'Segoe UI', 'Roboto', 'Ubuntu', 'Noto Sans', 'DejaVu Sans', 'Courier New'];
      const probeText = 'mmmMMMmmmlllmmmLLL₹▁₺₸ẞॿmmmiiimmmIIImmmwwwmmmWWW';
      const metric = (width, height) => `${Number(width).toFixed(2)}x${Number(height).toFixed(2)}`;

      const measureRange = (fontFamily) => {
        const node = document.createElement('span');
        node.textContent = probeText;
        node.style.cssText = [
          'position:absolute',
          'left:-10000px',
          'top:-10000px',
          'white-space:nowrap',
          'font-size:72px',
          `font-family:${fontFamily}, monospace`
        ].join(';');
        document.body.appendChild(node);
        const range = document.createRange();
        range.selectNodeContents(node);
        const rect = range.getBoundingClientRect();
        const clientRects = [...range.getClientRects()].map((item) => metric(item.width, item.height));
        range.detach?.();
        node.remove();
        return {
          rect: metric(rect.width, rect.height),
          clientRects: clientRects.join(', ') || 'none'
        };
      };

      const measureSvg = (fontFamily) => {
        const namespace = 'http://www.w3.org/2000/svg';
        const svg = document.createElementNS(namespace, 'svg');
        const text = document.createElementNS(namespace, 'text');
        svg.setAttribute('width', '1');
        svg.setAttribute('height', '1');
        svg.style.cssText = 'position:absolute;left:-10000px;top:-10000px;';
        text.setAttribute('x', '0');
        text.setAttribute('y', '80');
        text.setAttribute('font-size', '72');
        text.setAttribute('font-family', `${fontFamily}, monospace`);
        text.textContent = probeText;
        svg.appendChild(text);
        document.body.appendChild(svg);
        let bbox = 'unavailable';
        let computedLength = 'unavailable';
        try {
          const box = text.getBBox();
          bbox = metric(box.width, box.height);
        } catch (error) {
          bbox = `${error.name}: ${error.message}`;
        }
        try {
          computedLength = Number(text.getComputedTextLength()).toFixed(2);
        } catch (error) {
          computedLength = `${error.name}: ${error.message}`;
        }
        svg.remove();
        return { bbox, computedLength };
      };

      const testFontFaceLocal = async (fontFamily) => {
        if (typeof FontFace !== 'function') return 'FontFace unavailable';
        const family = `Probe${Math.random().toString(16).slice(2)}`;
        try {
          const face = new FontFace(family, `local("${fontFamily}")`);
          const loaded = await Promise.race([
            face.load(),
            new Promise((_, reject) => setTimeout(() => reject(new Error('timeout')), 2500))
          ]);
          return loaded && loaded.status ? loaded.status : 'loaded';
        } catch (error) {
          return `${error.name}: ${error.message}`;
        }
      };

      // The Local Font Access API requires a user gesture and a permission
      // grant. Auto-running it here from the test harness produces
      // misleading results: with no gesture the call rejects or returns
      // empty regardless of whether the browser would actually protect a
      // legitimate caller. Set up a manual "Run check" button instead so
      // the user clicks it, the prompt fires, they Allow, and we measure
      // what the browser does *after* a real grant — the only state where
      // a protection claim is meaningful. See setupLocalFontApiCheck below.

      const rangeMetrics = probeFonts.map((font) => [font, measureRange(`"${font}"`)]);
      const svgMetrics = probeFonts.map((font) => [font, measureSvg(`"${font}"`)]);
      const fontFaceResults = await Promise.all(probeFonts.slice(0, 4).map(async (font) => [
        font,
        await testFontFaceLocal(font)
      ]));

      const uniqueRangeRects = new Set(rangeMetrics.map(([, value]) => value.rect)).size;
      const uniqueRangeClientRects = new Set(rangeMetrics.map(([, value]) => value.clientRects)).size;
      const uniqueSvgBBoxes = new Set(svgMetrics.map(([, value]) => value.bbox)).size;
      const uniqueSvgLengths = new Set(svgMetrics.map(([, value]) => value.computedLength)).size;
      const fontFaceLoaded = fontFaceResults.filter(([, value]) => value === 'loaded').length;
      const leaks = [
        uniqueRangeRects > 1,
        uniqueRangeClientRects > 1,
        uniqueSvgBBoxes > 1,
        uniqueSvgLengths > 1,
        fontFaceLoaded > 0,
      ].filter(Boolean).length;
      const status = leaks === 0 ? 'ok' : leaks <= 2 ? 'warn' : 'fail';
      const caseRows = [
        {
          title: 'Range rectangles',
          status: uniqueRangeRects <= 1 && uniqueRangeClientRects <= 1 ? 'PASS' : 'FAIL',
          expected: 'Same rectangle for every requested font.',
          current: `${uniqueRangeRects} bounding values, ${uniqueRangeClientRects} client-rect values.`,
          why: 'Range metrics should not reveal the resolved local font.'
        },
        {
          title: 'SVG text box',
          status: uniqueSvgBBoxes <= 1 ? 'PASS' : 'FAIL',
          expected: 'Same SVG text box for every requested font.',
          current: `${uniqueSvgBBoxes} unique box values.`,
          why: 'SVG getBBox can bypass normal DOM metric protection.'
        },
        {
          title: 'SVG text length',
          status: uniqueSvgLengths <= 1 ? 'PASS' : 'FAIL',
          expected: 'Same SVG text length for every requested font.',
          current: `${uniqueSvgLengths} unique text-length values.`,
          why: 'Different lengths expose font-specific glyph metrics.'
        },
        {
          title: 'FontFace local()',
          status: fontFaceLoaded === 0 ? 'PASS' : 'FAIL',
          expected: 'Local font loads should fail or be neutralized.',
          current: `${fontFaceLoaded} local font loads succeeded.`,
          why: 'local() can directly test whether a font exists.'
        },
      ];
      const failedCases = caseRows.filter((item) => item.status === 'FAIL').map((item) => item.title);
      const behavior = failedCases.length === 0
        ? 'All advanced font probes matched expected behavior'
        : `${failedCases.length} advanced font probe${failedCases.length === 1 ? '' : 's'} failed`;

      const fontFaceLocalStatus = fontFaceLoaded === 0 ? 'ok' : 'fail';
      setReportItem('fontface-local',
        fontFaceLocalStatus,
        fontFaceLocalStatus === 'ok' ? 'FontFace local() blocked or timed out' : `${fontFaceLoaded} local font load(s) succeeded`,
        'FontFace local() can probe whether a specific font is installed without any metric APIs.');
      setReportItem('font-advanced', status,
        behavior,
        failedCases.length ? `Failed: ${failedCases.join(', ')}` : 'No advanced font leaks detected.');
      setAdvancedFontCard(status,
        failedCases.length ? `${failedCases.length} failed test${failedCases.length === 1 ? '' : 's'}` : 'All tests passed',
        caseRows,
        [
          ['Range unique rects', String(uniqueRangeRects)],
          ['Range unique client rects', String(uniqueRangeClientRects)],
          ['Range probes', rangeMetrics.map(([font, value]) => `${font}: ${value.rect} | ${value.clientRects}`).join(' || ')],
          ['SVG unique bboxes', String(uniqueSvgBBoxes)],
          ['SVG unique text lengths', String(uniqueSvgLengths)],
          ['SVG probes', svgMetrics.map(([font, value]) => `${font}: bbox ${value.bbox}, len ${value.computedLength}`).join(' || ')],
          ['FontFace local() results', fontFaceResults.map(([font, value]) => `${font}: ${value}`).join(' | ')],
        ]);
    };

    const drawCanvas = () => {
      const canvas = document.getElementById('canvas');
      if (!canvas) return;
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
      return canvas;
    };

    const runCanvasTest = async () => {
      const canvas = drawCanvas();
      if (!canvas) return;
      const attempts = [];
      for (let i = 0; i < 3; i += 1) {
        attempts.push(canvas.toDataURL('image/png'));
      }
      const hashes = [];
      for (const dataUrl of attempts) {
        hashes.push(await hashText(dataUrl));
      }
      const dataUrl = attempts[0];
      const hash = hashes[0];
      const blobHash = await hashBlob(await canvasToBlob(canvas));
      const ctx = canvas.getContext('2d');
      let imageDataHash = 'unavailable';
      if (ctx && typeof ctx.getImageData === 'function') {
        try {
          const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
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
      const contextProbes = await Promise.all([
        runCanvasContextProbe(),
        runCanvasContextProbe()
      ]);
      const contextMethodNames = [
        'toDataURL',
        'toBlob',
        'getImageData',
        'offscreenConvertToBlob',
        'offscreenTransferToImageBitmap'
      ];
      const contextChangedMethods = contextMethodNames.filter((name) =>
        contextProbes[0] &&
        contextProbes[1] &&
        contextProbes[0][name] &&
        contextProbes[1][name] &&
        contextProbes[0][name] !== 'unavailable' &&
        contextProbes[1][name] !== 'unavailable' &&
        contextProbes[0][name] !== contextProbes[1][name]
      );
      const contextProbeError = contextProbes.find((probe) => probe && probe.error)?.error;
      const uniqueAttempts = new Set(hashes).size;
      const canvasHistory = trackSessionHistory(storageKeys.canvas, hash);
      const uniqueCanvasSessionHashes = new Set(canvasHistory).size;
      const canvasSessionCount = canvasHistory.length;
      const allCanvasSessionsUnique = uniqueCanvasSessionHashes === canvasSessionCount;
      const automatedContextChanged = contextChangedMethods.length > 0;
      const bitmapTransferProtected = contextChangedMethods.includes('offscreenTransferToImageBitmap');
      setReportItem('canvas-bitmap-transfer',
        bitmapTransferProtected ? 'ok' : 'fail',
        bitmapTransferProtected ? 'Bitmap transfer output varies' : 'Bitmap transfer unprotected',
        'OffscreenCanvas.transferToImageBitmap bypasses canvas noise — pixels read from the resulting bitmap are unperturbed.');
      const canvasStatus = canvasSessionCount < 2
        ? (automatedContextChanged ? 'ok' : 'warn')
        : allCanvasSessionsUnique ? 'ok' : 'fail';
      setReportItem('canvas-fingerprint', canvasStatus,
        canvasSessionCount < 2
          ? (automatedContextChanged ? 'Changes across automated fresh contexts' : 'Awaiting second session for baseline')
          : allCanvasSessionsUnique
            ? `All ${canvasSessionCount} sessions produced different canvas hashes`
            : 'Duplicate canvas hash across sessions — noise may be stable',
        `Unique session hashes: ${uniqueCanvasSessionHashes}/${canvasSessionCount}, changed context methods: ${contextChangedMethods.length}`);
      setCard('canvas-card', canvasStatus,
        canvasStatus === 'ok' ? 'Cross-session noise confirmed' : canvasStatus === 'warn' ? 'Need another session' : 'Session hash collision', [
          ['session history (newest last)', canvasHistory.join(', ')],
          ['sessions recorded', String(canvasSessionCount)],
          ['unique session hashes', `${uniqueCanvasSessionHashes}/${canvasSessionCount}`],
          ['toDataURL attempt hashes', hashes.join(', ')],
          ['toDataURL unique attempts', `${uniqueAttempts}/3`],
          ['toBlob hash', blobHash],
          ['getImageData hash', imageDataHash],
          ['OffscreenCanvas convertToBlob hash', offscreenBlobHash],
          ['OffscreenCanvas transferToImageBitmap hash', offscreenBitmapHash],
          ['automated context probe 1', JSON.stringify(contextProbes[0])],
          ['automated context probe 2', JSON.stringify(contextProbes[1])],
          ['automated changed methods', contextChangedMethods.join(', ') || 'none'],
          ['automated probe error', contextProbeError || 'none'],
          ['data URL prefix', short(dataUrl)]
        ]);
    };

    const runWebGLTest = async () => {
      const canvas = document.getElementById('webgl-canvas');
      if (!canvas) return;
      const renderHash = async (gl) => {
        gl.clearColor(0.18, 0.52, 0.38, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        const pixels = new Uint8Array(64 * 64 * 4);
        gl.readPixels(0, 0, 64, 64, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        return hashText([...pixels].join(','));
      };
      const gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
      if (!gl) {
        setReportItem('webgl-profile', 'warn', 'WebGL unavailable', 'No WebGL context was created.');
        setReportItem('webgl-debug', 'warn', 'WebGL unavailable', 'Cannot verify debug renderer exposure without WebGL.');
        setReportItem('webgl-render-output', 'warn', 'WebGL unavailable', 'Cannot verify rendered-output stability without WebGL.');
        setCard('webgl-card', 'warn', 'WebGL unavailable', [['result', 'No WebGL context']]);
        return;
      }
      const hash = await renderHash(gl);
      const webglHistory = trackSessionHistory(storageKeys.webgl, hash);
      const uniqueWebglSessionHashes = new Set(webglHistory).size;
      const webglSessionCount = webglHistory.length;
      const allWebglSessionsUnique = uniqueWebglSessionHashes === webglSessionCount;

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
          if (offscreenGl) {
            offscreenHash = await renderHash(offscreenGl);
          }
        } catch (error) {
          offscreenHash = `${error.name}: ${error.message}`;
        }
      }
      const contextProbes = await Promise.all([
        runWebGLContextProbe(),
        runWebGLContextProbe()
      ]);
      const contextMethodNames = [
        'webgl1ReadPixels',
        'webgl1Export',
        'webgl2ReadPixels',
        'offscreenWebGL'
      ];
      const contextChangedMethods = contextMethodNames.filter((name) =>
        contextProbes[0] &&
        contextProbes[1] &&
        contextProbes[0][name] &&
        contextProbes[1][name] &&
        contextProbes[0][name] !== 'unavailable' &&
        contextProbes[1][name] !== 'unavailable' &&
        contextProbes[0][name] !== contextProbes[1][name]
      );
      const contextProbeError = contextProbes.find((probe) => probe && probe.error)?.error;
      const readPixelsNoiseActive = contextChangedMethods.includes('webgl1ReadPixels') ||
        contextChangedMethods.includes('webgl2ReadPixels') ||
        contextChangedMethods.includes('offscreenWebGL');
      setReportItem('webgl-readpixels-noise',
        readPixelsNoiseActive ? 'ok' : 'fail',
        readPixelsNoiseActive ? 'WebGL readPixels output varies' : 'WebGL readPixels unprotected',
        'gl.readPixels returns raw GPU pixel data — not perturbed by the canvas noise policy.');
      const renderedOutputStatus = webglSessionCount < 2
        ? (contextChangedMethods.length > 0 ? 'ok' : 'warn')
        : allWebglSessionsUnique ? 'ok' : 'fail';
      setReportItem('webgl-render-output', renderedOutputStatus,
        webglSessionCount < 2
          ? (contextChangedMethods.length > 0 ? 'Rendered output varies across contexts' : 'Awaiting second session for baseline')
          : allWebglSessionsUnique
            ? `All ${webglSessionCount} sessions produced different WebGL hashes`
            : 'Duplicate WebGL hash across sessions',
        `Unique session hashes: ${uniqueWebglSessionHashes}/${webglSessionCount}, changed context methods: ${contextChangedMethods.length}`);

      const debugExtension = gl.getExtension('WEBGL_debug_renderer_info');
      let vendor = 'unavailable';
      let renderer = 'unavailable';
      if (debugExtension) {
        vendor = gl.getParameter(debugExtension.UNMASKED_VENDOR_WEBGL);
        renderer = gl.getParameter(debugExtension.UNMASKED_RENDERER_WEBGL);
      } else {
        try {
          vendor = gl.getParameter(37445);
          renderer = gl.getParameter(37446);
        } catch {
          vendor = 'hidden';
          renderer = 'hidden';
        }
      }
      const extensions = gl.getSupportedExtensions() || [];
      const sensitiveDebugHidden = debugExtension === null && !extensions.includes('WEBGL_debug_renderer_info');
      const genericIdentity = /^(hidden|null|undefined|WebKit|OTF Browser)$/i.test(String(vendor)) &&
        /^(hidden|null|undefined|WebKit WebGL|OTF WebGL)$/i.test(String(renderer));
      const profileStatus = sensitiveDebugHidden ? 'ok' : genericIdentity ? 'warn' : 'fail';
      setReportItem('webgl-profile', profileStatus,
        profileStatus === 'ok' ? 'GPU identity hidden' : profileStatus === 'warn' ? 'Generic GPU identity' : 'Raw GPU identity exposed',
        `Vendor: ${String(vendor)}, renderer: ${String(renderer)}`);
      setReportItem('webgl-debug', sensitiveDebugHidden ? 'ok' : 'fail',
        sensitiveDebugHidden ? 'Debug renderer hidden' : 'Debug renderer available',
        `WEBGL_debug_renderer_info listed: ${extensions.includes('WEBGL_debug_renderer_info')}`);
      setCard('webgl-card', profileStatus,
        profileStatus === 'ok' ? 'GPU identity hidden' : profileStatus === 'warn' ? 'Generic identity' : 'Raw identity exposed', [
          ['debug extension', String(debugExtension)],
          ['unmasked vendor', String(vendor)],
          ['unmasked renderer', String(renderer)],
          ['extension listed', String(extensions.includes('WEBGL_debug_renderer_info'))],
          ['session history (newest last)', webglHistory.join(', ')],
          ['sessions recorded', String(webglSessionCount)],
          ['unique session hashes', `${uniqueWebglSessionHashes}/${webglSessionCount}`],
          ['WebGL2 readPixels hash', webgl2Hash],
          ['canvas export hash after WebGL', exportHash],
          ['OffscreenCanvas WebGL hash', offscreenHash],
          ['automated context probe 1', JSON.stringify(contextProbes[0])],
          ['automated context probe 2', JSON.stringify(contextProbes[1])],
          ['automated changed methods', contextChangedMethods.join(', ') || 'none'],
          ['automated probe error', contextProbeError || 'none']
        ]);
    };

    const runNavigationSchemeTest = async () => {
      // Test 1: window.open — does the browser allow opening a javascript: URL?
      let jsOpenBlocked = true;
      try {
        const w = window.open('javascript:void(0)');
        if (w) { jsOpenBlocked = false; w.close(); }
      } catch (_) { jsOpenBlocked = true; }

      // Test 2: iframe navigation to dangerous schemes — does the browser
      // allow loading data:, javascript:, or file: URLs in an iframe?
      const runIframeProbe = (url, token, timeoutMs = 2000) => new Promise((resolve) => {
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
          resolve('allowed');
        };
        window.addEventListener('message', onMessage);
        iframe.src = url;
        document.body.appendChild(iframe);
        setTimeout(() => {
          cleanup();
          resolve('blocked');
        }, timeoutMs);
      });

      const dataToken = 'nav-data-' + Date.now() + '-' + Math.random();
      const jsToken = 'nav-js-' + Date.now() + '-' + Math.random();
      const fileToken = 'nav-file-' + Date.now() + '-' + Math.random();

      const [dataIframeBlocked, jsIframeBlocked, fileIframeBlocked] = await Promise.all([
        runIframeProbe(`data:text/html,<script>parent.postMessage({token:"${dataToken}",result:"loaded"},"*")<\/script>`, dataToken),
        runIframeProbe(`javascript:void(parent.postMessage({token:"${jsToken}",result:"executed"},"*"))`, jsToken),
        runIframeProbe('file:///etc/passwd', fileToken),
      ]);

      // Test 3: <a> tag with javascript: href — does clicking it execute?
      let anchorJsBlocked = true;
      const canary = '__otf_scheme_anchor_' + Date.now();
      window[canary] = 'secure';
      try {
        const a = document.createElement('a');
        a.href = "javascript:void(window['" + canary + "']='pwned')";
        a.click();
        if (window[canary] === 'pwned') anchorJsBlocked = false;
      } catch (_) {}

      const allBlocked = jsOpenBlocked && dataIframeBlocked !== 'allowed' && jsIframeBlocked !== 'allowed' && fileIframeBlocked !== 'allowed' && anchorJsBlocked;
      const schemesAllowed = [];
      if (jsOpenBlocked !== true) schemesAllowed.push('window.open javascript');
      if (dataIframeBlocked === 'allowed') schemesAllowed.push('iframe data:');
      if (jsIframeBlocked === 'allowed') schemesAllowed.push('iframe javascript:');
      if (fileIframeBlocked === 'allowed') schemesAllowed.push('iframe file:');
      if (anchorJsBlocked !== true) schemesAllowed.push('anchor javascript:');
      const status = allBlocked ? 'ok' : schemesAllowed.length <= 2 ? 'warn' : 'fail';

      setReportItem('navigation-scheme', status,
        allBlocked
          ? 'All dangerous navigation schemes blocked'
          : schemesAllowed.length + ' scheme(s) allowed: ' + schemesAllowed.join(', '),
        `javascript window.open: ${jsOpenBlocked ? 'blocked' : 'allowed'} | ` +
        `iframe data:: ${dataIframeBlocked} | ` +
        `iframe javascript:: ${jsIframeBlocked} | ` +
        `iframe file:: ${fileIframeBlocked} | ` +
        `javascript anchor: ${anchorJsBlocked ? 'blocked' : 'allowed'}`);
    };

    const runDownloadImageTest = async () => {
      const cefAvailable = typeof window.cefQuery === 'function';
      if (!cefAvailable) {
        setReportItem('download-image', 'warn', 'Not running in OTF Browser',
          'download-image: is a cefQuery handler only available in OTF Browser.');
        return;
      }
      let vulnerable = true;
      let detail = 'timeout or error';
      try {
        vulnerable = await new Promise((resolve) => {
          const timer = setTimeout(() => resolve(true), 5000);
          window.cefQuery({
            request: 'download-image:file:///etc/passwd',
            onSuccess: (response) => {
              clearTimeout(timer);
              const path = (response || '').substring(0, 120);
              detail = 'downloaded to: ' + path;
              resolve(true);
            },
            onFailure: () => {
              clearTimeout(timer);
              detail = 'blocked by handler';
              resolve(false);
            },
          });
        });
      } catch (_) { detail = 'cefQuery threw'; }
      setReportItem('download-image', vulnerable ? 'fail' : 'ok',
        vulnerable ? 'download-image: allows file:// scheme — arbitrary file read' : 'download-image: blocks file:// scheme',
        detail);
    };

    const runFingerprintSurfacesTest = () => {
      const set = (id, exposed, behavior, detail) => {
        setReportItem(id, exposed ? 'fail' : 'ok', behavior, detail);
      };
      const exists = typeof globalThis !== 'undefined' ? globalThis : window;

      // Disk space — navigator.storage.estimate() reveals quota/usage
      if (exists.navigator && exists.navigator.storage && typeof exists.navigator.storage.estimate === 'function') {
        exists.navigator.storage.estimate().then((info) => {
          const bytes = info?.quota || 0;
          const expectedQuota = 193273528320;
          set('fp-disk-space', bytes > 0 && bytes !== expectedQuota,
            bytes > 0 ? `Storage quota exposed: ${(bytes / 1073741824).toFixed(1)} GB` : 'Storage API blocked',
            `quota: ${bytes}, usage: ${info?.usage || 0}`);
        }).catch(() => set('fp-disk-space', false, 'Storage estimate threw', ''));
      } else {
        set('fp-disk-space', false, 'Storage estimate unavailable', '');
      }

      // Audio fingerprint — OfflineAudioContext produces detectable output
      try {
        const ctx = new (exists.OfflineAudioContext || exists.webkitOfflineAudioContext)(1, 44100, 44100);
        const osc = ctx.createOscillator();
        osc.type = 'sawtooth';
        osc.frequency.value = 440;
        const dst = ctx.createGain();
        dst.gain.value = 0.5; // quieter to avoid loud buzz
        osc.connect(dst);
        dst.connect(ctx.destination);
        osc.start();
        ctx.startRendering();
        ctx.oncomplete = (e) => {
          const buffer = e.renderedBuffer;
          const data = buffer.getChannelData(0);
          let hash = 0;
          for (let i = 0; i < Math.min(data.length, 4410); i++) {
            hash = ((hash << 5) - hash + ((data[i] * 100000) | 0)) | 0;
          }
          set('fp-audio', true, 'Audio fingerprint surface exposed', `hash: ${hash}`);
        };
        ctx.onerror = () => set('fp-audio', false, 'Audio context threw');
      } catch (_) { set('fp-audio', false, 'Audio context unavailable'); }

      // Battery API
      if (exists.navigator && typeof exists.navigator.getBattery === 'function') {
        exists.navigator.getBattery().then((battery) => {
          const hasRealVals = battery && (typeof battery.level === 'number' || typeof battery.charging === 'boolean');
          set('fp-battery', !!hasRealVals,
            hasRealVals ? 'Battery API exposed' : 'Battery API normalized',
            hasRealVals ? `level: ${battery.level}, charging: ${battery.charging}` : '');
        }).catch(() => set('fp-battery', false, 'getBattery threw', ''));
      } else {
        set('fp-battery', false, 'getBattery unavailable', '');
      }

      // navigator.platform + Sec-CH-UA-Platform (via userAgentData)
      const platform = exists.navigator?.platform || '';
      const uaPlatform = exists.navigator?.userAgentData?.platform || '(not exposed)';
      const platformNormalized = !platform || /^(Win32|Linux(\s+x86_64|\s+i\d+)?|MacIntel|Mac ARM|iPhone|iPad|iPod|Android|WebOS|OpenBSD|FreeBSD)$/i.test(platform);
      set('fp-platform', !platformNormalized,
        platformNormalized ? `Platform: ${platform || 'unavailable'}` : `Raw platform exposed: ${platform}`,
        `navigator.platform: ${platform} | userAgentData.platform: ${uaPlatform}`);

      // navigator.plugins
      const pluginsLen = exists.navigator?.plugins?.length || 0;
      const mimeLen = exists.navigator?.mimeTypes?.length || 0;
      set('fp-plugins', false,
        `plugins: ${pluginsLen}`,
        `plugins: ${pluginsLen}`);
      set('fp-mime-types', false,
        `mimeTypes: ${mimeLen}`,
        `mimeTypes: ${mimeLen}`);

      // navigator.connection
      const conn = exists.navigator?.connection || exists.navigator?.mozConnection || exists.navigator?.webkitConnection;
      set('fp-connection', !!conn,
        conn ? 'Network Connection API exposed' : 'Connection API blocked',
        conn ? `type: ${conn.effectiveType || '?'}, downlink: ${conn.downlink || '?'}` : '');

      // navigator.maxTouchPoints
      const mtp = exists.navigator?.maxTouchPoints;
      set('fp-max-touch-points', mtp > 0,
        mtp > 0 ? `maxTouchPoints: ${mtp}` : 'Touch points hidden',
        `maxTouchPoints: ${mtp}`);

      // screen.colorDepth
      const colorDepth = exists.screen?.colorDepth;
      set('fp-color-depth', colorDepth !== undefined && colorDepth !== 24,
        colorDepth === 24 ? 'colorDepth normalized (24)' : `colorDepth: ${colorDepth}`,
        `colorDepth: ${colorDepth}, pixelDepth: ${exists.screen?.pixelDepth}`);

      // CSS color-gamut
      const cg = matchMedia('(color-gamut: srgb)').matches ? 'srgb' :
                 matchMedia('(color-gamut: p3)').matches ? 'p3' :
                 matchMedia('(color-gamut: rec2020)').matches ? 'rec2020' : 'unknown';
      set('fp-color-gamut', cg !== 'srgb',
        cg === 'srgb' ? 'color-gamut: srgb (normalized)' : `color-gamut: ${cg}`,
        cg);

      // navigator.keyboard
      const hasKeyboard = !!(exists.navigator?.keyboard);
      set('fp-keyboard', hasKeyboard,
        hasKeyboard ? 'Keyboard API exposed' : 'Keyboard API blocked',
        '');

      // navigator.pdfViewerEnabled
      const pdf = exists.navigator?.pdfViewerEnabled;
      set('fp-pdf-viewer', pdf !== undefined,
        pdf ? 'PDF viewer enabled' : pdf === false ? 'PDF viewer disabled/blocked' : 'unavailable',
        `pdfViewerEnabled: ${pdf}`);

      // CSS pointer
      const pointer = matchMedia('(pointer: fine)').matches ? 'fine' :
                      matchMedia('(pointer: coarse)').matches ? 'coarse' : 'none';
      set('fp-pointer', pointer === 'fine',
        pointer === 'fine' ? 'Pointer: fine (exact device)' : `Pointer: ${pointer}`,
        pointer);

      // CSS hover
      const hover = matchMedia('(hover: hover)').matches ? 'hover' :
                    matchMedia('(hover: none)').matches ? 'none' : 'unknown';
      set('fp-hover', hover === 'hover',
        hover === 'hover' ? 'Hover: hover (exact device)' : `Hover: ${hover}`,
        hover);

      // CSS forced-colors
      const forced = matchMedia('(forced-colors: active)').matches;
      set('fp-forced-colors', false, forced ? 'Forced colors active' : 'Forced colors not active', '');

      // CSS dynamic-range
      const dr = matchMedia('(dynamic-range: high)').matches ? 'high' : 'standard';
      set('fp-dynamic-range', dr === 'high',
        dr === 'high' ? 'Dynamic range: high' : 'Dynamic range: standard',
        dr);

      // CSS prefers-reduced-motion
      const rm = matchMedia('(prefers-reduced-motion: reduce)').matches;
      set('fp-reduced-motion', rm,
        rm ? 'prefers-reduced-motion: reduce' : 'prefers-reduced-motion: no-preference',
        '');

      // speechSynthesis.getVoices()
      if (exists.speechSynthesis && typeof exists.speechSynthesis.getVoices === 'function') {
        const voices = exists.speechSynthesis.getVoices();
        if (voices.length === 0) {
          // Voices load async; re-check after a short delay
          exists.speechSynthesis.addEventListener('voiceschanged', () => {
            const v = exists.speechSynthesis.getVoices();
            set('fp-speech-voices', v.length > 0,
              v.length > 0 ? `${v.length} voice(s) exposed` : 'No voices',
              v.map((x) => x.name).join(', '));
          }, { once: true });
          setTimeout(() => {
            const v = exists.speechSynthesis.getVoices();
            set('fp-speech-voices', v.length > 0,
              v.length > 0 ? `${v.length} voice(s) exposed` : 'No voices exposed',
              v.map((x) => x.name).join(', '));
          }, 2000);
        } else {
          set('fp-speech-voices', voices.length > 0,
            `${voices.length} voice(s) exposed`,
            voices.map((x) => x.name).join(', '));
        }
      } else {
        set('fp-speech-voices', false, 'speechSynthesis unavailable', '');
      }

      // mediaDevices.enumerateDevices()
      if (exists.navigator && exists.navigator.mediaDevices && typeof exists.navigator.mediaDevices.enumerateDevices === 'function') {
        exists.navigator.mediaDevices.enumerateDevices().then((devices) => {
          const count = devices ? devices.length : 0;
          set('fp-enumerate-devices', count > 0,
            count > 0 ? `${count} device(s) exposed` : 'No devices exposed',
            devices ? devices.map((d) => `${d.kind}:${d.label || '?'}`).join(', ') : '');
        }).catch(() => set('fp-enumerate-devices', false, 'enumerateDevices threw', ''));
      } else {
        set('fp-enumerate-devices', false, 'enumerateDevices unavailable', '');
      }

      // navigator.getGamepads()
      if (exists.navigator && typeof exists.navigator.getGamepads === 'function') {
        try {
          const pads = exists.navigator.getGamepads();
          const count = pads ? pads.filter(Boolean).length : 0;
          set('fp-gamepads', count > 0,
            count > 0 ? `${count} gamepad(s) exposed` : 'No gamepads exposed',
            `gamepads: ${count}`);
        } catch (_) { set('fp-gamepads', false, 'getGamepads threw', ''); }
      } else {
        set('fp-gamepads', false, 'getGamepads unavailable', '');
      }

      // navigator.connection.rtt
      set('fp-connection-rtt', conn && typeof conn.rtt === 'number',
        conn && typeof conn.rtt === 'number' ? `RTT: ${conn.rtt}ms` : 'RTT not exposed',
        conn ? `rtt: ${conn.rtt}` : 'connection unavailable');
    };

    const runWebGPUTest = async () => {
      const log = document.getElementById('webgpu-log');
      if (!log) return;
      // Debug: surface the policy's own diagnostic markers so we can tell
      // whether the page-policy script ran at all and which GPU types were
      // exposed at policy-injection time.
      const diag = [
        ['policy injected', String(!!globalThis.__otfPagePolicyInjected)],
      ];
      const state = globalThis.__otfWebGPUPolicyState;
      if (state) {
        diag.push(['policy saw GPU', String(state.hadGPU)]);
        diag.push(['policy saw GPUAdapter', String(state.hadGPUAdapter)]);
        diag.push(['policy saw GPUDevice', String(state.hadGPUDevice)]);
      } else {
        diag.push(['policy WebGPU state', 'missing']);
      }
      try {
        const GPUProto = globalThis.GPU && globalThis.GPU.prototype;
        const GPUAdapterProto = globalThis.GPUAdapter && globalThis.GPUAdapter.prototype;
        const GPUDeviceProto = globalThis.GPUDevice && globalThis.GPUDevice.prototype;
        diag.push(['GPU patched', String(!!(GPUProto && GPUProto.__otfGPUPolicy))]);
        diag.push(['GPUAdapter patched', String(!!(GPUAdapterProto && GPUAdapterProto.__otfGPUAdapterPolicy))]);
        diag.push(['GPUDevice patched', String(!!(GPUDeviceProto && GPUDeviceProto.__otfWebGPUComputePolicy))]);
      } catch (_) {}

      if (!navigator.gpu) {
        log.textContent = 'navigator.gpu is unavailable in this runtime.';
        setReportItem('webgpu-compute', 'ok', 'WebGPU unavailable', 'Compute pipeline surface is not exposed in this runtime.');
        setCard('webgpu-card', 'ok', 'WebGPU unavailable', [['navigator.gpu', 'false'], ...diag]);
        return;
      }
      try {
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
          log.textContent = 'requestAdapter returned null.';
          setReportItem('webgpu-compute', 'ok', 'No WebGPU adapter', 'Compute pipeline surface is not available without an adapter.');
          setCard('webgpu-card', 'ok', 'No adapter', [['adapter', 'null'], ...diag]);
          return;
        }
        const device = await adapter.requestDevice();
        const shader = device.createShaderModule({
          code: '@compute @workgroup_size(1) fn main() {}'
        });
        device.createComputePipeline({
          layout: 'auto',
          compute: { module: shader, entryPoint: 'main' }
        });
        log.textContent = 'createComputePipeline succeeded. The policy is not active.';
        setReportItem('webgpu-compute', 'fail', 'Compute pipeline allowed', 'createComputePipeline succeeded.');
        setCard('webgpu-card', 'fail', 'Compute allowed', [
          ['navigator.gpu', 'true'],
          ['error', 'none'],
          ...diag
        ]);
      } catch (error) {
        log.textContent = `${error.name}: ${error.message}`;
        const expected = /compute pipelines are disabled/i.test(error.message);
        setReportItem('webgpu-compute', expected ? 'ok' : 'warn',
          expected ? 'Compute pipeline blocked' : 'Compute blocked by runtime',
          `${error.name}: ${error.message}`);
        setCard('webgpu-card', expected ? 'ok' : 'warn',
          expected ? 'Compute blocked' : 'Blocked by runtime', [
            ['navigator.gpu', 'true'],
            ['error name', error.name],
            ['error message', error.message],
            ...diag
          ]);
      }
    };

    // Kick off the Local Font Access API check. Must run synchronously
    // inside the Start-button click handler so transient user activation
    // is still valid when queryLocalFonts asks Chromium to show the
    // permission prompt. We don't await — the prompt resolves in parallel
    // with the rest of the suite; whenever the user responds (or the
    // 30-second timeout fires), the report row updates.
    const triggerLocalFontApiCheck = () => {
      if (typeof globalThis.queryLocalFonts !== 'function') {
        setReportItem('local-font-api', 'ok',
          'Local Font Access API not exposed',
          'globalThis.queryLocalFonts is undefined in this runtime.');
        return;
      }
      const TIMEOUT_MS = 30000;
      Promise.race([
        globalThis.queryLocalFonts(),
        new Promise((_, reject) =>
          setTimeout(() => reject(new Error('user-no-response')), TIMEOUT_MS)),
      ]).then((fonts) => {
        const count = Array.isArray(fonts) ? fonts.length : 0;
        if (count > 0) {
          const sample = fonts.slice(0, 3)
            .map((f) => f.fullName || f.postscriptName || f.family || '?')
            .join(', ');
          setReportItem('local-font-api', 'fail',
            `queryLocalFonts exposed ${count} fonts after permission grant`,
            `Sample: ${sample}`);
        } else {
          setReportItem('local-font-api', 'ok',
            'queryLocalFonts returned empty list',
            'Browser refuses to enumerate fonts even when the user granted permission.');
        }
      }).catch((error) => {
        if (error && error.message === 'user-no-response') {
          setReportItem('local-font-api', 'warn',
            'Local Font Access prompt unanswered',
            'User did not respond to the Chromium permission prompt within 30s.');
        } else {
          setReportItem('local-font-api', 'ok',
            'queryLocalFonts denied or threw',
            `${error.name}: ${error.message}`);
        }
      });
    };

    const runAllTests = async () => {
      runSideEffectsTest();
      runInjectionTest();
      runScreenTest();
      runHardwareTest();
      runFontTest();
      runLayoutMetricsTest();
      runAdvancedFontSurfaceTest();
      runCanvasTest();
      runWebGLTest();
      runWebGPUTest();
      runFingerprintSurfacesTest();
      await runNavigationSchemeTest();
      await runDownloadImageTest();
    };

    const startBtn = document.getElementById('start-tests-btn');
    if (startBtn) {
      startBtn.addEventListener('click', () => {
        // queryLocalFonts must be invoked synchronously inside this click
        // handler — Chromium consumes the transient activation here. If
        // we ran the rest of the suite first the prompt would silently
        // fail to appear.
        triggerLocalFontApiCheck();
        startBtn.disabled = true;
        startBtn.textContent = 'Running…';
        setActiveSection('privacy');
        runAllTests().catch((error) => console.error('Test suite error:', error));
      });
    }

    // ── JSON export ────────────────────────────────────────────────────
    // Minimal universal report: per-test status + a one-line summary, plus
    // the user agent so the host can be identified. No card dumps, no
    // hash histories — those are evidence of protections working, not the
    // protection state itself.
    const buildJsonExport = () => {
      const scoreNode = document.getElementById('fingerprint-score-value');
      const tests = [...document.querySelectorAll('[data-report-item]')].map((item) => {
        const behaviorEl = item.querySelector('.report-behavior');
        return {
          id: item.dataset.reportItem || null,
          section: item.dataset.testSection || null,
          status: item.dataset.status || null,
          summary: behaviorEl ? behaviorEl.textContent.trim() : '',
        };
      });
      return {
        schema: 'otf-protection-report',
        schemaVersion: 2,
        capturedAt: new Date().toISOString(),
        userAgent: navigator.userAgent,
        score: scoreNode ? Number(scoreNode.textContent) : null,
        tests,
      };
    };

    const exportBtn = document.getElementById('export-json-btn');
    const exportOut = document.getElementById('export-json-output');
    if (exportBtn && exportOut) {
      exportBtn.addEventListener('click', () => {
        exportOut.value = JSON.stringify(buildJsonExport(), null, 2);
        exportOut.hidden = false;
        exportOut.focus();
        exportOut.select();
      });
    }
  });

  return (
    <div id="fingerprints-container">
      <style>
        {`
          #fingerprints-container {
            --bg: #f4efe6;
            --ink: #17130d;
            --muted: #675f52;
            --panel: #fffaf0;
            --line: #d8ccb8;
            --good: #0f7a43;
            --bad: #a93424;
            --warn: #9a6500;
            --chip: #ece0cb;
            
            background:
              radial-gradient(circle at 10% 10%, rgba(255, 214, 139, 0.55), transparent 32rem),
              linear-gradient(135deg, #f4efe6 0%, #e7dbc7 100%);
            color: var(--ink);
            font: 15px/1.5 ui-monospace, "SFMono-Regular", "Menlo", "Consolas", monospace;
            min-height: calc(100vh - 80px);
            padding-bottom: 48px;
            text-align: left;
          }

          #fingerprints-container main {
            width: min(1120px, calc(100vw - 32px));
            margin: 0 auto;
            padding: 34px 0 0;
          }

          #fingerprints-container header {
            display: grid;
            gap: 10px;
            margin-bottom: 22px;
          }

          #fingerprints-container .eyebrow {
            margin: 0;
            color: var(--warn);
            font-size: 12px;
            font-weight: 900;
            letter-spacing: 0.12em;
            text-transform: uppercase;
          }

          #fingerprints-container h1 {
            margin: 0;
            font: 700 34px/1.1 Georgia, "Times New Roman", serif;
            letter-spacing: -0.04em;
            color: var(--ink);
          }

          #fingerprints-container p {
            margin: 0;
            color: var(--muted);
          }

          #fingerprints-container .grid {
            display: grid;
            grid-template-columns: repeat(2, minmax(0, 1fr));
            gap: 16px;
          }

          #fingerprints-container .module-heading {
            grid-column: 1 / -1;
            display: grid;
            gap: 4px;
            margin-top: 8px;
            padding: 8px 2px 0;
          }

          #fingerprints-container .module-heading h2 {
            margin: 0;
            font: 800 18px/1.2 Georgia, "Times New Roman", serif;
            letter-spacing: -0.02em;
            text-transform: none;
          }

          #fingerprints-container .module-heading p {
            max-width: 780px;
            font-size: 13px;
          }

          #fingerprints-container .test-category {
            display: inline-flex;
            width: fit-content;
            border-radius: 999px;
            padding: 4px 8px;
            margin-bottom: 10px;
            background: var(--chip);
            color: var(--muted);
            font-size: 11px;
            font-weight: 900;
            letter-spacing: 0.06em;
            text-transform: uppercase;
          }

          #fingerprints-container .report {
            display: grid;
            gap: 10px;
            margin: 0 0 18px;
          }

          #fingerprints-container .score-panel {
            display: grid;
            grid-template-columns: auto minmax(0, 1fr);
            gap: 16px;
            align-items: center;
            margin-bottom: 16px;
            padding: 16px;
            border: 1px solid var(--line);
            border-radius: 18px;
            background: rgba(255, 250, 240, 0.72);
          }

          #fingerprints-container .score-number {
            display: flex;
            align-items: baseline;
            gap: 4px;
            font: 800 44px/1 Georgia, "Times New Roman", serif;
            letter-spacing: -0.05em;
            color: var(--ink);
          }

          #fingerprints-container .score-number span:last-child {
            font-size: 18px;
            color: var(--muted);
          }

          #fingerprints-container .score-meta {
            display: grid;
            gap: 8px;
          }

          #fingerprints-container .score-title {
            color: var(--ink);
            font-weight: 900;
          }

          #fingerprints-container .section-tabs {
            display: grid;
            grid-template-columns: repeat(3, minmax(0, 1fr));
            gap: 10px;
            margin-bottom: 16px;
          }

          #fingerprints-container .section-tab {
            appearance: none;
            text-align: left;
            border: 1px solid var(--line);
            border-radius: 14px;
            padding: 12px;
            background: rgba(255, 250, 240, 0.68);
            color: inherit;
            cursor: pointer;
            font: inherit;
            width: auto;
          }

          #fingerprints-container .section-tab.active {
            border-color: rgba(22, 17, 11, 0.42);
            background: #16110b;
            color: #fffaf0;
            box-shadow: 0 14px 28px rgba(57, 42, 20, 0.18);
          }

          #fingerprints-container .section-tab strong {
            display: block;
            margin-bottom: 4px;
            color: inherit;
          }

          #fingerprints-container .section-tab span {
            color: var(--muted);
            font-size: 12px;
          }

          #fingerprints-container .section-tab.active span {
            color: rgba(255, 250, 240, 0.72);
          }

          #fingerprints-container .score-label {
            color: var(--muted);
            font-size: 12px;
          }

          #fingerprints-container .score-track {
            height: 9px;
            overflow: hidden;
            border-radius: 999px;
            background: var(--chip);
          }

          #fingerprints-container .score-fill {
            width: 0;
            height: 100%;
            border-radius: inherit;
            background: linear-gradient(90deg, var(--warn), var(--good));
            transition: width 220ms ease;
          }

          #fingerprints-container .report-row {
            display: grid;
            grid-template-columns: minmax(180px, 0.65fr) minmax(0, 1fr);
            gap: 14px;
            align-items: center;
            background: color-mix(in srgb, var(--panel) 94%, white);
            border: 1px solid var(--line);
            border-radius: 16px;
            padding: 12px 14px;
            box-shadow: 0 10px 22px rgba(57, 42, 20, 0.08);
          }

          #fingerprints-container .report-label {
            color: var(--ink);
            font-weight: 800;
          }

          #fingerprints-container .report-result {
            display: grid;
            grid-template-columns: 28px minmax(0, 1fr);
            gap: 10px;
            align-items: center;
          }

          #fingerprints-container .report-icon {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            width: 28px;
            height: 28px;
            border-radius: 999px;
            background: var(--chip);
            color: var(--warn);
            font-weight: 900;
            line-height: 1;
          }

          #fingerprints-container .report-icon.ok {
            background: rgba(15, 122, 67, 0.12);
            color: var(--good);
          }

          #fingerprints-container .report-icon.fail {
            background: rgba(169, 52, 36, 0.12);
            color: var(--bad);
          }

          #fingerprints-container .report-icon.warn {
            background: rgba(154, 101, 0, 0.13);
            color: var(--warn);
          }

          #fingerprints-container .report-text {
            min-width: 0;
          }

          #fingerprints-container .report-behavior {
            color: var(--ink);
            font-weight: 800;
            overflow-wrap: anywhere;
          }

          #fingerprints-container .report-detail {
            color: var(--muted);
            font-size: 12px;
            overflow-wrap: anywhere;
          }

          #fingerprints-container .card {
            background: color-mix(in srgb, var(--panel) 92%, white);
            border: 1px solid var(--line);
            border-radius: 18px;
            box-shadow: 0 18px 38px rgba(57, 42, 20, 0.12);
            padding: 18px;
            overflow: hidden;
          }

          #fingerprints-container .card.wide {
            grid-column: 1 / -1;
          }

          #fingerprints-container .report-card {
            margin-bottom: 16px;
          }

          #fingerprints-container .card h2 {
            margin: 0 0 12px;
            font-size: 16px;
            letter-spacing: 0.02em;
            text-transform: uppercase;
            font-family: inherit;
            color: var(--ink);
          }

          #fingerprints-container .status {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            border-radius: 999px;
            padding: 5px 10px;
            background: var(--chip);
            font-weight: 700;
            font-size: 12px;
          }

          #fingerprints-container .status.ok {
            color: var(--good);
          }

          #fingerprints-container .status.fail {
            color: var(--bad);
          }

          #fingerprints-container .status.warn {
            color: var(--warn);
          }

          #fingerprints-container dl {
            display: grid;
            grid-template-columns: 180px minmax(0, 1fr);
            gap: 8px 12px;
            margin: 14px 0 0;
          }

          #fingerprints-container dt {
            color: var(--muted);
          }

          #fingerprints-container dd {
            margin: 0;
            min-width: 0;
            overflow-wrap: anywhere;
            color: var(--ink);
          }

          #fingerprints-container .case-list {
            grid-column: 1 / -1;
            display: grid;
            gap: 10px;
          }

          #fingerprints-container .case-row {
            border: 1px solid var(--line);
            border-radius: 14px;
            padding: 12px;
            background: rgba(255, 250, 240, 0.72);
          }

          #fingerprints-container .case-row.fail {
            border-color: rgba(180, 50, 38, 0.32);
            background: rgba(180, 50, 38, 0.07);
          }

          #fingerprints-container .case-row.warn {
            border-color: rgba(151, 92, 13, 0.28);
            background: rgba(151, 92, 13, 0.07);
          }

          #fingerprints-container .case-heading {
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 12px;
            margin-bottom: 8px;
          }

          #fingerprints-container .case-heading strong {
            font-size: 14px;
            color: var(--ink);
          }

          #fingerprints-container .case-pill {
            border-radius: 999px;
            padding: 4px 8px;
            font-size: 11px;
            font-weight: 900;
            background: var(--chip);
          }

          #fingerprints-container .case-pill.pass {
            color: var(--good);
          }

          #fingerprints-container .case-pill.fail {
            color: var(--bad);
          }

          #fingerprints-container .case-row p {
            display: grid;
            grid-template-columns: 92px minmax(0, 1fr);
            gap: 10px;
            margin: 6px 0 0;
            color: var(--ink);
            line-height: 1.4;
          }

          #fingerprints-container .case-row p span {
            color: var(--muted);
            font-weight: 800;
          }

          #fingerprints-container canvas {
            display: block;
            width: 100%;
            max-width: 420px;
            height: auto;
            margin-top: 12px;
            border: 1px solid var(--line);
            border-radius: 12px;
            background: white;
          }

          #fingerprints-container pre {
            white-space: pre-wrap;
            overflow-wrap: anywhere;
            margin: 12px 0 0;
            padding: 12px;
            border-radius: 12px;
            background: rgba(32, 25, 15, 0.08);
            color: #23180c;
            font-size: 12px;
          }

          #fingerprints-container button {
            appearance: none;
            border: 0;
            border-radius: 12px;
            background: #16110b;
            color: #fffaf0;
            cursor: pointer;
            font: inherit;
            font-weight: 700;
            padding: 10px 14px;
            width: fit-content;
          }

          #fingerprints-container .header-actions {
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            gap: 12px;
          }

          #fingerprints-container #export-json-btn {
            background: transparent;
            color: #16110b;
            border: 1px solid #16110b;
          }

          #fingerprints-container #export-json-output {
            margin-top: 12px;
            width: 100%;
            font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
            font-size: 12px;
            line-height: 1.4;
            padding: 12px;
            border-radius: 12px;
            border: 1px solid var(--line);
            background: var(--panel);
            color: var(--ink);
            resize: vertical;
          }

          @media (max-width: 760px) {
            #fingerprints-container .grid {
              grid-template-columns: 1fr;
            }

            #fingerprints-container .section-tabs {
              grid-template-columns: 1fr;
            }

            #fingerprints-container dl {
              grid-template-columns: 1fr;
            }

            #fingerprints-container .report-row {
              grid-template-columns: 1fr;
            }

            #fingerprints-container .score-panel {
              grid-template-columns: 1fr;
            }
          }
        `}
      </style>
      <main>
        <header>
          <p className="eyebrow">Browser Protection Test Center</p>
          <h1>Privacy, Security, and Abuse Resistance Tests</h1>
          <p>Run this page in any browser to inspect fingerprinting exposure, security-sensitive API behavior, and protection gaps. More modules such as cookies, ads, tracking, and abuse flows can be added here.</p>
          <div className="header-actions">
            <button id="start-tests-btn">Start Test Suite</button>
            <button id="export-json-btn" type="button" hidden>Export JSON</button>
          </div>
          <textarea
            id="export-json-output"
            readOnly
            hidden
            spellCheck="false"
            aria-label="Exported JSON report"
            rows="12"
          ></textarea>
        </header>

        <section className="card wide report-card">
          <h2>Protection Score</h2>
          <div className="score-panel">
            <div className="score-number">
              <span id="fingerprint-score-value">0</span>
              <span>/100</span>
            </div>
            <div className="score-meta">
              <div className="score-title">Unified browser protection score</div>
              <div className="score-track"><div className="score-fill" id="fingerprint-score-fill"></div></div>
              <div className="score-label" id="fingerprint-score-label">0 tests complete</div>
            </div>
          </div>
          <div className="section-tabs" role="tablist" aria-label="Test categories">
            <button className="section-tab active" type="button" role="tab" aria-selected="true" data-section-tab="privacy" onClick={() => {
              document.querySelectorAll('[data-section-tab]').forEach((button) => {
                const active = button.dataset.sectionTab === 'privacy';
                button.classList.toggle('active', active);
                button.setAttribute('aria-selected', String(active));
              });
              document.querySelectorAll('[data-test-section]').forEach((node) => {
                node.hidden = node.dataset.testSection !== 'privacy';
              });
            }}>
              <strong>Privacy</strong>
              <span>Fingerprinting surfaces, local device signals, font metrics, canvas, WebGL, workers.</span>
            </button>
            <button className="section-tab" type="button" role="tab" aria-selected="false" data-section-tab="security" onClick={() => {
              document.querySelectorAll('[data-section-tab]').forEach((button) => {
                const active = button.dataset.sectionTab === 'security';
                button.classList.toggle('active', active);
                button.setAttribute('aria-selected', String(active));
              });
              document.querySelectorAll('[data-test-section]').forEach((node) => {
                node.hidden = node.dataset.testSection !== 'security';
              });
            }}>
              <strong>Security</strong>
              <span>Risky browser capabilities such as WebGPU compute that can enable abuse like crypto mining.</span>
            </button>
            <button className="section-tab" type="button" role="tab" aria-selected="false" data-section-tab="upcoming" onClick={() => {
              document.querySelectorAll('[data-section-tab]').forEach((button) => {
                const active = button.dataset.sectionTab === 'upcoming';
                button.classList.toggle('active', active);
                button.setAttribute('aria-selected', String(active));
              });
              document.querySelectorAll('[data-test-section]').forEach((node) => {
                node.hidden = node.dataset.testSection !== 'upcoming';
              });
            }}>
              <strong>Upcoming</strong>
              <span>Cookie abuse, ad/tracker behavior, storage isolation, redirect flows, and more.</span>
            </button>
          </div>
          <div className="report">
            {[
              ['screen-dimensions', 'Privacy: screen dimensions', 'privacy'],
              ['hardware-profile', 'Privacy: CPU and memory', 'privacy'],
              ['font-surface', 'Privacy: font surface', 'privacy'],
              ['fontface-local', 'Privacy: FontFace local() probe', 'privacy'],
              ['local-font-api', 'Privacy: Local Font Access API', 'privacy'],
              ['font-advanced', 'Privacy: advanced font surfaces', 'privacy'],
              ['layout-metrics', 'Privacy: layout metrics', 'privacy'],
              ['fp-disk-space', 'Privacy: storage quota', 'privacy'],
              ['fp-audio', 'Privacy: audio fingerprint', 'privacy'],
              ['fp-battery', 'Privacy: Battery API', 'privacy'],
              ['fp-platform', 'Privacy: navigator.platform', 'privacy'],

              ['fp-plugins', 'Privacy: navigator.plugins', 'privacy'],
              ['fp-mime-types', 'Privacy: navigator.mimeTypes', 'privacy'],
              ['fp-connection', 'Privacy: Network Connection API', 'privacy'],
              ['fp-max-touch-points', 'Privacy: maxTouchPoints', 'privacy'],
              ['fp-color-depth', 'Privacy: screen.colorDepth', 'privacy'],
              ['fp-color-gamut', 'Privacy: CSS color-gamut', 'privacy'],
              ['fp-keyboard', 'Privacy: Keyboard API', 'privacy'],
              ['fp-pdf-viewer', 'Privacy: pdfViewerEnabled', 'privacy'],
              ['fp-pointer', 'Privacy: CSS pointer', 'privacy'],
              ['fp-hover', 'Privacy: CSS hover', 'privacy'],
              ['fp-forced-colors', 'Privacy: CSS forced-colors', 'privacy'],
              ['fp-dynamic-range', 'Privacy: CSS dynamic-range', 'privacy'],
              ['fp-reduced-motion', 'Privacy: prefers-reduced-motion', 'privacy'],
              ['fp-speech-voices', 'Privacy: speechSynthesis voices', 'privacy'],
              ['fp-enumerate-devices', 'Privacy: media devices', 'privacy'],
              ['fp-gamepads', 'Privacy: getGamepads', 'privacy'],
              ['fp-connection-rtt', 'Privacy: connection RTT', 'privacy'],
              ['getImageData-noise', 'Privacy: canvas pixel noise', 'privacy'],
              ['canvas-fingerprint', 'Privacy: canvas fingerprint', 'privacy'],
              ['canvas-bitmap-transfer', 'Privacy: canvas bitmap transfer', 'privacy'],
              ['webgl-profile', 'Privacy: WebGL identity', 'privacy'],
              ['webgl-debug', 'Privacy: WebGL debug renderer', 'privacy'],
              ['webgl-render-output', 'Privacy: WebGL rendered output', 'privacy'],
              ['webgl-readpixels-noise', 'Privacy: WebGL readPixels noise', 'privacy'],
              ['webgpu-compute', 'Security: WebGPU compute pipeline', 'security'],
              ['download-image', 'Security: download-image arbitrary file read', 'security'],
              ['navigation-scheme', 'Security: dangerous navigation scheme blocking', 'security'],
              ['worker-surface', 'Privacy: worker execution surface', 'privacy'],
            ].map(([id, label, section]) => (
              <div className="report-row" data-report-item={id} data-status="checking" data-test-section={section} key={id}>
                <div className="report-label">{label}</div>
                <div className="report-result">
                  <span className="report-icon warn">!</span>
                  <div className="report-text">
                    <div className="report-behavior">Checking...</div>
                    <div className="report-detail">Waiting for runtime probe.</div>
                  </div>
                </div>
              </div>
            ))}
            <div className="report-row" data-test-section="upcoming" hidden>
              <div className="report-label">Upcoming: cookies, ads, and tracking</div>
              <div className="report-result">
                <span className="report-icon warn">!</span>
                <div className="report-text">
                  <div className="report-behavior">Planned test module</div>
                  <div className="report-detail">Cookie abuse, storage isolation, ad/tracker behavior, redirect chains, and related tests will appear here.</div>
                </div>
              </div>
            </div>
          </div>
        </section>

        <section className="grid">
          <div className="module-heading" data-test-section="privacy">
            <h2>Privacy: Fingerprint Resistance</h2>
            <p>These tests check whether websites can identify a device through stable hardware, rendering, font, canvas, WebGL, and worker signals.</p>
          </div>

          <article className="card" id="screen-card" data-test-section="privacy">
            <span className="test-category">Privacy</span>
            <h2>Screen</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="hardware-card" data-test-section="privacy">
            <span className="test-category">Privacy</span>
            <h2>CPU & Memory</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="font-card" data-test-section="privacy">
            <span className="test-category">Privacy</span>
            <h2>Fonts</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="font-advanced-card" data-test-section="privacy">
            <span className="test-category">Privacy</span>
            <h2>Advanced Font Surfaces</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="layout-card" data-test-section="privacy">
            <span className="test-category">Privacy</span>
            <h2>Layout Metrics</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="policy-card" data-test-section="privacy">
            <span className="test-category">Privacy</span>
            <h2>Runtime Surface</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="side-effects-card" data-test-section="privacy">
            <span className="test-category">Privacy</span>
            <h2>Surface Side-Effects</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="canvas-card" data-test-section="privacy">
            <span className="test-category">Privacy</span>
            <h2>Canvas</h2>
            <span className="status warn">Running</span>
            <canvas id="canvas" width="420" height="140"></canvas>
            <dl></dl>
          </article>

          <article className="card" id="webgl-card" data-test-section="privacy">
            <span className="test-category">Privacy</span>
            <h2>WebGL</h2>
            <span className="status warn">Running</span>
            <canvas id="webgl-canvas" width="64" height="64" style="display:none;"></canvas>
            <dl></dl>
          </article>

          <div className="module-heading" data-test-section="security" hidden>
            <h2>Security: High-Risk Capability Controls</h2>
            <p>These tests check whether powerful browser APIs are constrained when they create abuse risk, such as compute pipelines used for unwanted crypto mining.</p>
          </div>

          <article className="card" id="webgpu-card" data-test-section="security" hidden>
            <span className="test-category">Security</span>
            <h2>WebGPU Compute</h2>
            <span className="status warn">Running</span>
            <dl></dl>
            <pre id="webgpu-log"></pre>
          </article>

          <div className="module-heading" data-test-section="upcoming" hidden>
            <h2>Upcoming: Web Abuse and Tracking Tests</h2>
            <p>Planned modules will cover cookies, storage isolation, ads, trackers, redirects, permission prompts, and other attack patterns.</p>
          </div>

          <article className="card" data-test-section="upcoming" hidden>
            <span className="test-category">Upcoming</span>
            <h2>Cookie & Storage Abuse</h2>
            <span className="status warn">Planned</span>
            <dl>
              <dt>Planned checks</dt>
              <dd>Third-party cookies, partitioning, persistent storage, cache abuse, and restart behavior.</dd>
            </dl>
          </article>

          <article className="card" data-test-section="upcoming" hidden>
            <span className="test-category">Upcoming</span>
            <h2>Ads & Tracking</h2>
            <span className="status warn">Planned</span>
            <dl>
              <dt>Planned checks</dt>
              <dd>Tracker requests, ad script behavior, redirect chains, bounce tracking, and attribution surfaces.</dd>
            </dl>
          </article>
        </section>
      </main>
    </div>
  );
}
