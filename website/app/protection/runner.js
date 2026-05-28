import { registerRow, setStatus, setExtra, setRow, runState, rowsState } from "./store.js";
import modules from "./tests/index.js";
import { getRestartState, clearRestartHistory } from "./tests/helpers.js";

// ── Test context handed to each module's run(ctx) ─────────────────────────
const makeCtx = () => ({
  set: setStatus,
  setRows: (id, rows) => setRow(id, { rows }),
  setExtra,
});

// Reload protocol — used by tests that need to compare a value across two
// fresh page loads (audio, math, layout). A module marks itself with
// `needsReload: true` and implements capture() + run().
const RELOAD_KEY = 'otfProtectionReload';

export const reloadStateFor = (moduleName) => {
  const get = () => {
    try { return JSON.parse(sessionStorage.getItem(RELOAD_KEY + ':' + moduleName) || 'null'); }
    catch (_) { return null; }
  };
  const set = (value) => {
    if (value === null) sessionStorage.removeItem(RELOAD_KEY + ':' + moduleName);
    else sessionStorage.setItem(RELOAD_KEY + ':' + moduleName, JSON.stringify(value));
  };
  return { get, set };
};

const AUTO_RESUME_KEY = 'otfProtectionAutoResume';
export const markAutoResume = () => sessionStorage.setItem(AUTO_RESUME_KEY, '1');
export const consumeAutoResume = () => {
  const has = sessionStorage.getItem(AUTO_RESUME_KEY) !== null;
  sessionStorage.removeItem(AUTO_RESUME_KEY);
  return has;
};

// ── Registration ──────────────────────────────────────────────────────────
export const registerAll = () => {
  for (const mod of modules) {
    for (const row of mod.produces) {
      registerRow(row.id, {
        label: row.label,
        entropy: row.entropy,
        category: mod.category,
        module: mod.module,
        description: row.description || '',
      });
    }
  }
};

// ── Orchestration ─────────────────────────────────────────────────────────
const markRunning = (mod) => {
  for (const row of mod.produces) setStatus(row.id, 'running', 'Running…', '');
};

const runGesture = (ctx) => {
  for (const mod of modules.filter((m) => m.needsGesture)) {
    markRunning(mod);
    try { mod.run(ctx); } catch (error) {
      for (const row of mod.produces) {
        setStatus(row.id, 'warn', 'Test threw', `${error.name}: ${error.message}`);
      }
    }
  }
};

const runOne = async (mod, ctx) => {
  markRunning(mod);
  try {
    await mod.run(ctx);
  } catch (error) {
    for (const row of mod.produces) {
      setStatus(row.id, 'warn', 'Test threw', `${error.name}: ${error.message}`);
    }
  }
};

// isAutoResume: true when called from maybeAutoResume after a mid-suite page
// reload (for audio/math/layout tests). In that case we must NOT clear restart
// history — the canvas/webgl baseline from earlier in this session is still needed.
export const startSuite = async ({ onReloadNeeded, isAutoResume = false } = {}) => {
  const ctx = makeCtx();

  // Clear restart history only when starting a genuinely new cycle:
  //   - not an auto-resume (mid-suite page reload for reload tests)
  //   - not a comparison run (browser was restarted, previous session data is the baseline)
  if (!isAutoResume && getRestartState() !== 'ready-to-compare') {
    clearRestartHistory();
  }

  runState.value = 'running';
  runGesture(ctx);

  // Normal tests (canvas, webgl, etc.) and reload tests run independently.
  // Restart-pending rows from canvas/webgl do NOT block the reload tests.
  const normal = modules.filter((m) => !m.needsGesture && !m.needsReload);
  await Promise.allSettled(normal.map((m) => runOne(m, ctx)));

  const reloadModules = modules.filter((m) => m.needsReload);
  const toCaptureNow = reloadModules.filter((m) => reloadStateFor(m.module).get() === null);

  if (toCaptureNow.length > 0) {
    runState.value = 'awaiting-reload';
    let capturedCount = 0;
    for (const mod of toCaptureNow) {
      markRunning(mod);
      try {
        const captured = await mod.capture(ctx);
        if (captured !== null && captured !== undefined) {
          reloadStateFor(mod.module).set({ value: captured });
          capturedCount++;
        }
      } catch (_) {}
    }

    if (capturedCount > 0) {
      markAutoResume();
      if (onReloadNeeded) {
        const proceed = await onReloadNeeded(toCaptureNow);
        if (proceed === false) {
          for (const mod of toCaptureNow) reloadStateFor(mod.module).set(null);
          consumeAutoResume();
          for (const mod of reloadModules) await runOne(mod, ctx);
          runState.value = _finalState();
          return;
        }
      }
      location.reload();
      return;
    }

    for (const mod of toCaptureNow) await runOne(mod, ctx);
  }

  for (const mod of reloadModules) await runOne(mod, ctx);

  runState.value = _finalState();
};

// After all tests complete, determine the final runState:
// if any row is pending-restart the user still needs to restart the browser.
const _finalState = () => {
  const restartPending = Object.values(rowsState.value)
    .some((s) => s.status === 'pending-restart');
  return restartPending ? 'awaiting-restart' : 'done';
};

// Called on every mount. If the runner triggered a page reload mid-suite,
// auto-resume so the user doesn't have to click Start again.
export const maybeAutoResume = async (callbacks) => {
  if (!consumeAutoResume()) return false;
  await startSuite({ ...callbacks, isAutoResume: true });
  return true;
};
