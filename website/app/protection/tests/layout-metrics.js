import { hashText, trackSessionHistory, storageKeys } from "./helpers.js";

export default {
  module: 'layout-metrics',
  category: 'privacy',
  produces: [{
    id: 'layout-metrics',
    label: 'Layout metrics',
    entropy: 'high',
    description: 'getBoundingClientRect noise — verifies layout values rotate across sessions.',
  }],
  async run(ctx) {
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
        left: Number(rect.left).toFixed(4),
        top: Number(rect.top).toFixed(4),
        right: Number(rect.right).toFixed(4),
        bottom: Number(rect.bottom).toFixed(4),
      });
    }
    node.remove();

    const widthValues = rects.map((r) => r.width);
    const heightValues = rects.map((r) => r.height);
    const uniqueWidths = new Set(widthValues).size;
    const uniqueHeights = new Set(heightValues).size;
    const uniqueRects = new Set(rects.map((r) => JSON.stringify(r))).size;
    const hash = await hashText(JSON.stringify(rects));
    const history = trackSessionHistory(storageKeys.layout, hash);
    const uniqueSessionHashes = new Set(history).size;
    const sessionCount = history.length;
    const allUnique = uniqueSessionHashes === sessionCount;
    const status = sessionCount < 2 ? 'warn' : allUnique ? 'ok' : 'fail';

    ctx.set('layout-metrics', status,
      sessionCount < 2
        ? 'Awaiting second session for baseline'
        : allUnique
          ? `All ${sessionCount} sessions produced different layout hashes`
          : 'Duplicate layout hash found across sessions — noise may be stable',
      `Unique rects this run: ${uniqueRects}/8, sessions recorded: ${sessionCount}, unique session hashes: ${uniqueSessionHashes}/${sessionCount}`,
      [
        ['session history (newest last)', history.join(', ')],
        ['sessions recorded', String(sessionCount)],
        ['unique session hashes', `${uniqueSessionHashes}/${sessionCount}`],
        ['all sessions unique', String(allUnique)],
        ['unique rects this run', `${uniqueRects}/8`],
        ['unique widths', String(uniqueWidths)],
        ['unique heights', String(uniqueHeights)],
        ['width samples', widthValues.join(', ')],
        ['height samples', heightValues.join(', ')],
        ['first rect', JSON.stringify(rects[0])],
        ['last rect', JSON.stringify(rects[rects.length - 1])],
      ]);
  },
};
