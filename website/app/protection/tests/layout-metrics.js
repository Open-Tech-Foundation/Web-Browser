import { reloadStateFor } from "../runner.js";
import { hashText } from "./helpers.js";

const measureLayout = async () => {
  const node = document.createElement('span');
  node.textContent = 'mmmMMMmmmlllmmmLLL₹▁₺₸ẞॿmmmiiimmmIIImmmwwwmmmWWW';
  node.style.cssText = [
    'position:absolute', 'left:-10000px', 'top:-10000px',
    'white-space:nowrap', 'font-size:72px', 'font-family:"Calibri", monospace',
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
    });
  }
  node.remove();
  const hash = await hashText(JSON.stringify(rects));
  return { rects, hash };
};

export default {
  module: 'layout-metrics',
  category: 'privacy',
  needsReload: true,
  produces: [{
    id: 'layout-metrics',
    label: 'Layout metrics',
    entropy: 'high',
    description: 'getBoundingClientRect noise — verifies layout values rotate across page loads.',
  }],
  async capture(_ctx) {
    const { hash } = await measureLayout();
    return hash;
  },
  async run(ctx) {
    const state = reloadStateFor('layout-metrics');
    const prev = state.get()?.value ?? null;
    const { rects, hash } = await measureLayout();

    const widthValues = rects.map((r) => r.width);
    const heightValues = rects.map((r) => r.height);
    const uniqueRects = new Set(rects.map((r) => JSON.stringify(r))).size;

    if (prev === null) {
      ctx.set('layout-metrics', 'warn',
        'Skipped cross-load check',
        `Single-shot hash: ${hash}. Re-run to verify rotation.`,
        [['this-load hash', hash], ['unique rects this run', `${uniqueRects}/8`]]);
      return;
    }

    state.set(null);
    const rotated = prev !== hash;
    ctx.set('layout-metrics', rotated ? 'ok' : 'fail',
      rotated
        ? 'Layout hash rotated across loads — per-session noise confirmed'
        : 'Layout hash did NOT rotate — noise may be stable or absent',
      `hash: ${hash}, prev: ${prev}`,
      [
        ['this-load hash', hash],
        ['previous-load hash', prev],
        ['hashes differ', String(rotated)],
        ['unique rects this run', `${uniqueRects}/8`],
        ['unique widths', String(new Set(widthValues).size)],
        ['unique heights', String(new Set(heightValues).size)],
        ['width samples', widthValues.join(', ')],
        ['height samples', heightValues.join(', ')],
      ]);
  },
};
