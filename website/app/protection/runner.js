import { registerRow, setStatus, setExtra, setRow, runState } from "./store.js";
import modules from "./tests/index.js";

// ── Test context handed to each module's run(ctx) ─────────────────────────
// Modules call:
//   ctx.set(id, status, summary, detail, rows?)  — write a row's result
//   ctx.setRows(id, rows)                        — update only the detail rows
//   ctx.setExtra(id, anything)                   — attach renderer-specific
//                                                  payload (e.g. canvas hooks)
const makeCtx = () => ({
  set: setStatus,
  setRows: (id, rows) => setRow(id, { rows }),
  setExtra,
});

// Reload protocol — used by tests that need to compare a value across two
// fresh page loads (currently the audio test). A module marks itself with
// `needsReload: true` and uses ctx.reloadState to drive the dance.
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
// Called once at mount. Walks each module and registers every produced row
// in the store so the UI can render them with idle status.
export const registerAll = () => {
  for (const mod of modules) {
    for (const row of mod.produces) {
      registerRow(row.id, {
        label: row.label,
        entropy: row.entropy,           // 'high' | 'medium' | 'low' | 'security'
        category: mod.category,         // 'privacy' | 'security'
        module: mod.module,
        description: row.description || '',
      });
    }
  }
};

// ── Orchestration ─────────────────────────────────────────────────────────
// Tests split into three buckets:
//   gesture — must run synchronously inside a click handler so transient
//             user activation is still valid (e.g. queryLocalFonts).
//   reload  — needs a two-phase capture across a page reload.
//   normal  — async, runs in parallel (Promise.allSettled).
const markRunning = (mod) => {
  for (const row of mod.produces) setStatus(row.id, 'running', 'Running…', '');
};

const runGesture = (ctx) => {
  // MUST be called synchronously from the click handler.
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

export const startSuite = async ({ onReloadNeeded } = {}) => {
  const ctx = makeCtx();
  runState.value = 'running';
  runGesture(ctx);
  const normal = modules.filter((m) => !m.needsGesture && !m.needsReload);
  await Promise.allSettled(normal.map((m) => runOne(m, ctx)));

  const reloadModules = modules.filter((m) => m.needsReload);

  // Phase 1: capture ALL reload modules that don't have a saved value yet,
  // then do a single page reload for all of them at once.
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
          runState.value = 'done';
          return;
        }
      }
      location.reload();
      return;
    }

    // All captures failed: fall back to single-shot for these modules.
    for (const mod of toCaptureNow) await runOne(mod, ctx);
  }

  // Phase 2: run all reload modules — those with a saved value compare across
  // loads; those without (failed capture) fall back in their own run().
  for (const mod of reloadModules) await runOne(mod, ctx);

  runState.value = 'done';
};

// Hook called after registerAll on every mount. If the previous session
// scheduled a reload, kick the suite back off so the user doesn't have to
// click Start again.
export const maybeAutoResume = async (callbacks) => {
  if (!consumeAutoResume()) return false;
  await startSuite(callbacks);
  return true;
};
