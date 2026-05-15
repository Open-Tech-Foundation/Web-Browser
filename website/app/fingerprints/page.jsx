import { onMount } from "@opentf/web";

export default function FingerprintsPage() {
  onMount(() => {
    const storageKeys = {
      canvas: 'otfFingerprintTest:lastCanvas',
      webgl: 'otfFingerprintTest:lastWebgl'
    };

    const short = (value) => String(value).slice(0, 24);

    const setReportItem = (id, status, behavior, detail) => {
      const item = document.querySelector(`[data-report-item="${id}"]`);
      if (!item) return;
      const icon = item.querySelector('.report-icon');
      const behaviorNode = item.querySelector('.report-behavior');
      const detailNode = item.querySelector('.report-detail');
      icon.className = 'report-icon ' + status;
      icon.textContent = status === 'ok' ? '✓' : status === 'fail' ? '×' : '!';
      behaviorNode.textContent = behavior;
      detailNode.textContent = detail || '';
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
      setReportItem('policy', injected ? 'ok' : 'fail',
        injected ? 'Policy applied' : 'Policy missing',
        injected ? 'Page-level policy marker is present.' : 'This page is running without the browser policy marker.');
      setReportItem('canvas-hooks', canvasWrapped && toDataUrlWrapped ? 'ok' : 'fail',
        canvasWrapped && toDataUrlWrapped ? 'Canvas hooks active' : 'Canvas hooks missing',
        `getImageData: ${canvasWrapped}, toDataURL: ${toDataUrlWrapped}`);
      setReportItem('worker', workerWrapped ? 'ok' : 'warn',
        workerWrapped ? 'Worker constructor wrapped' : 'Worker constructor appears native',
        'Worker coverage should also be checked in dedicated worker tests.');
      setCard('policy-card', injected ? 'ok' : 'fail',
        injected ? 'Policy injected' : 'Policy missing', [
          ['user agent', navigator.userAgent],
          ['global marker', String(injected)],
          ['canvas getImageData wrapped', String(canvasWrapped)],
          ['canvas toDataURL wrapped', String(toDataUrlWrapped)],
          ['webgl getParameter wrapped', String(webglWrapped)],
          ['worker constructor wrapped', String(workerWrapped)]
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
      ctx.fillText('OTF canvas fingerprint', 24, 48);
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
      const vendor = gl.getParameter(37445);
      const renderer = gl.getParameter(37446);
      const extensions = gl.getSupportedExtensions() || [];
      const patched = debugExtension === null && vendor === 'OTF Browser' && renderer === 'OTF WebGL';
      const sensitiveDebugHidden = debugExtension === null && !extensions.includes('WEBGL_debug_renderer_info');
      setReportItem('webgl-profile', patched ? 'ok' : 'fail',
        patched ? 'Normalized WebGL identity' : 'Raw WebGL identity exposed',
        `Vendor: ${String(vendor)}, renderer: ${String(renderer)}`);
      setReportItem('webgl-debug', sensitiveDebugHidden ? 'ok' : 'fail',
        sensitiveDebugHidden ? 'Debug renderer hidden' : 'Debug renderer available',
        `WEBGL_debug_renderer_info listed: ${extensions.includes('WEBGL_debug_renderer_info')}`);
      setCard('webgl-card', patched ? 'ok' : 'fail',
        patched ? 'Policy applied' : 'Policy missing', [
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
        setReportItem('webgpu-compute', 'warn', 'WebGPU unavailable', 'Cannot verify compute pipeline behavior in this runtime.');
        setCard('webgpu-card', 'warn', 'WebGPU unavailable', [['navigator.gpu', 'false']]);
        return;
      }
      try {
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
          log.textContent = 'requestAdapter returned null.';
          setReportItem('webgpu-compute', 'warn', 'No WebGPU adapter', 'Cannot verify compute pipeline behavior without an adapter.');
          setCard('webgpu-card', 'warn', 'No adapter', [['adapter', 'null']]);
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
          }
        `}
      </style>
      <main>
        <header>
          <h1>Fingerprint Protection Proof</h1>
          <p>This diagnostic tool verifies the effectiveness of the OTF Browser's anti-fingerprinting policies.</p>
          <button onClick={() => window.location.reload()}>Reload page</button>
        </header>

        <section className="card wide report-card">
          <h2>Browser Behavior Report</h2>
          <div className="report">
            {[
              ['policy', 'Page policy injection'],
              ['canvas-hooks', 'Canvas API hooks'],
              ['canvas-fingerprint', 'Canvas fingerprint'],
              ['webgl-profile', 'WebGL identity'],
              ['webgl-debug', 'WebGL debug renderer'],
              ['webgpu-compute', 'WebGPU compute pipeline'],
              ['worker', 'Worker constructor'],
            ].map(([id, label]) => (
              <div className="report-row" data-report-item={id} key={id}>
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
          <article className="card" id="policy-card">
            <h2>Injection</h2>
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
