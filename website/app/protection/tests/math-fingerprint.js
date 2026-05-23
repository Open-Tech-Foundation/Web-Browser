import { reloadStateFor } from "../runner.js";

// Representative probe set used by real fingerprinting scripts.
const PROBES = [
  ['sin',   () => Math.sin(1)],
  ['cos',   () => Math.cos(1)],
  ['tan',   () => Math.tan(1)],
  ['log',   () => Math.log(2)],
  ['exp',   () => Math.exp(1)],
  ['sqrt',  () => Math.sqrt(2)],
  ['atan2', () => Math.atan2(1, -1)],
];

const captureProbes = () => PROBES.map(([, fn]) => fn().toString()).join('|');

export default {
  module: 'math-fingerprint',
  category: 'privacy',
  needsReload: true,
  produces: [{
    id: 'math-fingerprint',
    label: 'Math fingerprinting',
    entropy: 'medium',
    description: 'Checks that trig/transcendental Math results rotate across page loads.',
  }],
  async capture(_ctx) {
    return captureProbes();
  },
  async run(ctx) {
    const state = reloadStateFor('math-fingerprint');
    const prev = state.get()?.value ?? null;
    const curr = captureProbes();

    if (prev === null) {
      ctx.set('math-fingerprint', 'warn',
        'Awaiting second load for cross-session check',
        'Reload the page to confirm Math values rotate.',
        [['snapshot', curr]]);
      return;
    }

    state.set(null);
    const rotated = prev !== curr;
    const rows = PROBES.map(([name, fn], i) => {
      const prevVal = prev.split('|')[i];
      const currVal = fn().toString();
      return [name, prevVal === currVal ? `stable: ${currVal}` : `rotated (prev ${prevVal} → ${currVal})`];
    });

    ctx.set('math-fingerprint', rotated ? 'ok' : 'fail',
      rotated
        ? 'Math values rotated across loads — per-session noise confirmed'
        : 'Math values are stable across loads — fingerprinting risk',
      rotated ? 'Per-session perturbation active' : 'No perturbation detected',
      rows);
  },
};
