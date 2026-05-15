import { onMount } from "@opentf/web";

export default function FingerprintsPage() {
  onMount(() => {
    const storageKeys = {
      canvas: 'browserFingerprintTest:lastCanvas',
      webgl: 'browserFingerprintTest:lastWebgl'
    };

    const short = (value) => String(value).slice(0, 24);
    const scoreValue = {
      ok: 1,
      warn: 0.5,
      fail: 0
    };

    const updateReportScore = () => {
      const items = [...document.querySelectorAll('[data-report-item]')];
      const scoredItems = items.filter((item) => item.dataset.status && item.dataset.status !== 'checking');
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
    };

    const setReportItem = (id, status, behavior, detail) => {
      const item = document.querySelector(`[data-report-item="${id}"]`);
      if (!item) return;
      const icon = item.querySelector('.report-icon');
      const behaviorNode = item.querySelector('.report-behavior');
      const detailNode = item.querySelector('.report-detail');
      item.dataset.status = status;
      icon.className = 'report-icon ' + status;
      icon.textContent = status === 'ok' ? '✓' : status === 'fail' ? '×' : '!';
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

    const functionLooksWrapped = (fn) =>
      typeof fn === 'function' && !/\[native code\]/.test(Function.prototype.toString.call(fn));

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
        '1366x768@1',
        '1440x900@1',
        '1920x1080@1',
        '1920x1080@2',
        '2560x1080@1'
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
      const normalized = cores === 4 && memory === 4;
      const valid =
        Number.isFinite(cores) &&
        Number.isFinite(memory) &&
        cores > 0 &&
        memory > 0;
      const status = normalized ? 'ok' : valid ? 'warn' : 'fail';
      setReportItem('hardware-profile', status,
        normalized ? 'Common hardware profile' : valid ? 'Raw hardware profile' : 'Invalid hardware values',
        `CPU cores: ${cores || 'unavailable'}, memory: ${memory || 'unavailable'} GB`);
      setCard('hardware-card', status,
        normalized ? 'Common profile' : valid ? 'Raw profile' : 'Invalid values', [
          ['hardware concurrency', cores ? String(cores) : 'unavailable'],
          ['device memory', memory ? `${memory} GB` : 'unavailable'],
          ['common profile', String(normalized)],
          ['browser profile', profile ? JSON.stringify(profile) : 'none']
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
      const uniqueAttempts = new Set(hashes).size;
      const previous = localStorage.getItem(storageKeys.canvas);
      localStorage.setItem(storageKeys.canvas, hash);
      const changed = previous && previous !== hash;
      const changedAcrossAttempts = uniqueAttempts > 1;
      const canvasStatus = changedAcrossAttempts || changed ? 'ok' : previous ? 'fail' : 'warn';
      setReportItem('canvas-fingerprint', canvasStatus,
        changedAcrossAttempts
          ? 'Changes across repeated attempts'
          : changed
            ? 'Changed from previous reload'
            : previous
              ? 'Not changed across attempts or reload'
              : 'First run; reload once to verify',
        `Unique attempt hashes: ${uniqueAttempts}/3`);
      setCard('canvas-card', canvasStatus,
        changedAcrossAttempts ? 'Changed across attempts' : previous ? (changed ? 'Changed after reload' : 'Same as previous reload') : 'First run', [
          ['current hash', hash],
          ['previous hash', previous || 'none'],
          ['attempt hashes', hashes.join(', ')],
          ['unique attempts', `${uniqueAttempts}/3`],
          ['reload changed', previous ? String(changed) : 'reload once to verify'],
          ['data URL prefix', short(dataUrl)]
        ]);
    };

    const runWebGLTest = async () => {
      const canvas = document.getElementById('webgl-canvas');
      if (!canvas) return;
      const gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
      if (!gl) {
        setReportItem('webgl-profile', 'warn', 'WebGL unavailable', 'No WebGL context was created.');
        setReportItem('webgl-debug', 'warn', 'WebGL unavailable', 'Cannot verify debug renderer exposure without WebGL.');
        setCard('webgl-card', 'warn', 'WebGL unavailable', [['result', 'No WebGL context']]);
        return;
      }
      gl.clearColor(0.18, 0.52, 0.38, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
      const pixels = new Uint8Array(64 * 64 * 4);
      gl.readPixels(0, 0, 64, 64, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      const hash = await hashText([...pixels].join(','));
      const previous = localStorage.getItem(storageKeys.webgl);
      localStorage.setItem(storageKeys.webgl, hash);
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
          ['readPixels hash', hash],
          ['previous hash', previous || 'none']
        ]);
    };

    const runWebGPUTest = async () => {
      const log = document.getElementById('webgpu-log');
      if (!log) return;
      if (!navigator.gpu) {
        log.textContent = 'navigator.gpu is unavailable in this runtime.';
        setReportItem('webgpu-compute', 'ok', 'WebGPU unavailable', 'Compute pipeline surface is not exposed in this runtime.');
        setCard('webgpu-card', 'ok', 'WebGPU unavailable', [['navigator.gpu', 'false']]);
        return;
      }
      try {
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
          log.textContent = 'requestAdapter returned null.';
          setReportItem('webgpu-compute', 'ok', 'No WebGPU adapter', 'Compute pipeline surface is not available without an adapter.');
          setCard('webgpu-card', 'ok', 'No adapter', [['adapter', 'null']]);
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
          ['error', 'none']
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
            ['error message', error.message]
          ]);
      }
    };

    runInjectionTest();
    runScreenTest();
    runHardwareTest();
    runCanvasTest();
    runWebGLTest();
    runWebGPUTest();
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

          @media (max-width: 760px) {
            #fingerprints-container .grid {
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
          <h1>Browser Fingerprint Test</h1>
          <p>Run this page in any browser to compare fingerprinting exposure, protection behavior, and high-risk API surfaces.</p>
          <button onClick={() => window.location.reload()}>Reload page</button>
        </header>

        <section className="card wide report-card">
          <h2>Browser Privacy Score</h2>
          <div className="score-panel">
            <div className="score-number">
              <span id="fingerprint-score-value">0</span>
              <span>/100</span>
            </div>
            <div className="score-meta">
              <div className="score-title">Fingerprint resistance score</div>
              <div className="score-track"><div className="score-fill" id="fingerprint-score-fill"></div></div>
              <div className="score-label" id="fingerprint-score-label">0 tests complete</div>
            </div>
          </div>
          <div className="report">
            {[
              ['screen-dimensions', 'Screen dimensions'],
              ['hardware-profile', 'CPU and memory'],
              ['canvas-fingerprint', 'Canvas fingerprint'],
              ['webgl-profile', 'WebGL identity'],
              ['webgl-debug', 'WebGL debug renderer'],
              ['webgpu-compute', 'WebGPU compute pipeline'],
              ['worker-surface', 'Worker execution surface'],
            ].map(([id, label]) => (
              <div className="report-row" data-report-item={id} data-status="checking" key={id}>
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
          </div>
        </section>

        <section className="grid">
          <article className="card" id="screen-card">
            <h2>Screen</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="hardware-card">
            <h2>CPU & Memory</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="policy-card">
            <h2>Runtime Surface</h2>
            <span className="status warn">Running</span>
            <dl></dl>
          </article>

          <article className="card" id="canvas-card">
            <h2>Canvas</h2>
            <span className="status warn">Running</span>
            <canvas id="canvas" width="420" height="140"></canvas>
            <dl></dl>
          </article>

          <article className="card" id="webgl-card">
            <h2>WebGL</h2>
            <span className="status warn">Running</span>
            <canvas id="webgl-canvas" width="64" height="64" style="display:none;"></canvas>
            <dl></dl>
          </article>

          <article className="card" id="webgpu-card">
            <h2>WebGPU Compute</h2>
            <span className="status warn">Running</span>
            <dl></dl>
            <pre id="webgpu-log"></pre>
          </article>
        </section>
      </main>
    </div>
  );
}
