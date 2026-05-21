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
  for (const mod of reloadModules) {
    const prev = reloadStateFor(mod.module).get();
    if (prev === null) {
      // Phase 1: capture and signal that a reload is required.
      runState.value = 'awaiting-reload';
      markRunning(mod);
      let captured = null;
      try { captured = await mod.capture(ctx); } catch (_) { captured = null; }
      if (captured === null) {
        await runOne(mod, ctx);    // module decides how to fail gracefully
        continue;
      }
      reloadStateFor(mod.module).set({ value: captured });
      markAutoResume();
      if (onReloadNeeded) {
        const proceed = await onReloadNeeded(mod);
        if (proceed === false) {
          reloadStateFor(mod.module).set(null);
          consumeAutoResume();
          await runOne(mod, ctx);  // fall back to single-shot run
          continue;
        }
      }
      location.reload();
      return;
    }
    // Phase 2: we have a prior value; let the module finish.
    await runOne(mod, ctx);
  }

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
