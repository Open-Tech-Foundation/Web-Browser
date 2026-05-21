// Advanced font-surface probes (Range rectangles, SVG getBBox,
// SVG getComputedTextLength, FontFace local()). Each probe is treated as a
// case-row in the detail view.
export default {
  module: 'font-advanced',
  category: 'privacy',
  produces: [
    {
      id: 'font-advanced',
      label: 'Advanced font surfaces',
      entropy: 'high',
      description: 'Range rectangles, SVG getBBox, computed text length — bypass paths around document.fonts.',
    },
    {
      id: 'fontface-local',
      label: 'FontFace local() probe',
      entropy: 'high',
      description: 'Whether `new FontFace(_, local("…"))` reveals which fonts are installed.',
    },
  ],
  async run(ctx) {
    const probeFonts = ['Calibri', 'Segoe UI', 'Roboto', 'Ubuntu', 'Noto Sans', 'DejaVu Sans', 'Courier New'];
    const probeText = 'mmmMMMmmmlllmmmLLL₹▁₺₸ẞॿmmmiiimmmIIImmmwwwmmmWWW';
    const metric = (width, height) => `${Number(width).toFixed(2)}x${Number(height).toFixed(2)}`;

    const measureRange = (fontFamily) => {
      const node = document.createElement('span');
      node.textContent = probeText;
      node.style.cssText = [
        'position:absolute', 'left:-10000px', 'top:-10000px',
        'white-space:nowrap', 'font-size:72px',
        `font-family:${fontFamily}, monospace`,
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
        clientRects: clientRects.join(', ') || 'none',
      };
    };

    const measureSvg = (fontFamily) => {
      const ns = 'http://www.w3.org/2000/svg';
      const svg = document.createElementNS(ns, 'svg');
      const text = document.createElementNS(ns, 'text');
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
      try { const box = text.getBBox(); bbox = metric(box.width, box.height); }
      catch (error) { bbox = `${error.name}: ${error.message}`; }
      try { computedLength = Number(text.getComputedTextLength()).toFixed(2); }
      catch (error) { computedLength = `${error.name}: ${error.message}`; }
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
          new Promise((_, reject) => setTimeout(() => reject(new Error('timeout')), 2500)),
        ]);
        return loaded && loaded.status ? loaded.status : 'loaded';
      } catch (error) {
        return `${error.name}: ${error.message}`;
      }
    };

    const rangeMetrics = probeFonts.map((font) => [font, measureRange(`"${font}"`)]);
    const svgMetrics = probeFonts.map((font) => [font, measureSvg(`"${font}"`)]);
    const fontFaceResults = await Promise.all(probeFonts.slice(0, 4).map(async (font) => [
      font, await testFontFaceLocal(font),
    ]));

    const uniqueRangeRects = new Set(rangeMetrics.map(([, v]) => v.rect)).size;
    const uniqueRangeClientRects = new Set(rangeMetrics.map(([, v]) => v.clientRects)).size;
    const uniqueSvgBBoxes = new Set(svgMetrics.map(([, v]) => v.bbox)).size;
    const uniqueSvgLengths = new Set(svgMetrics.map(([, v]) => v.computedLength)).size;
    const fontFaceLoaded = fontFaceResults.filter(([, v]) => v === 'loaded').length;
    const leaks = [
      uniqueRangeRects > 1,
      uniqueRangeClientRects > 1,
      uniqueSvgBBoxes > 1,
      uniqueSvgLengths > 1,
      fontFaceLoaded > 0,
    ].filter(Boolean).length;
    const status = leaks === 0 ? 'ok' : leaks <= 2 ? 'warn' : 'fail';

    const caseRows = [
      { title: 'Range rectangles',
        ok: uniqueRangeRects <= 1 && uniqueRangeClientRects <= 1,
        current: `${uniqueRangeRects} bounding values, ${uniqueRangeClientRects} client-rect values.` },
      { title: 'SVG text box',
        ok: uniqueSvgBBoxes <= 1,
        current: `${uniqueSvgBBoxes} unique box values.` },
      { title: 'SVG text length',
        ok: uniqueSvgLengths <= 1,
        current: `${uniqueSvgLengths} unique text-length values.` },
      { title: 'FontFace local()',
        ok: fontFaceLoaded === 0,
        current: `${fontFaceLoaded} local font loads succeeded.` },
    ];
    const failed = caseRows.filter((r) => !r.ok).map((r) => r.title);
    const fontFaceLocalStatus = fontFaceLoaded === 0 ? 'ok' : 'fail';

    ctx.set('fontface-local',
      fontFaceLocalStatus,
      fontFaceLocalStatus === 'ok' ? 'FontFace local() blocked or timed out'
        : `${fontFaceLoaded} local font load(s) succeeded`,
      'FontFace local() can probe whether a specific font is installed without any metric APIs.',
      [
        ['loaded count', String(fontFaceLoaded)],
        ['per-font results', fontFaceResults.map(([f, v]) => `${f}: ${v}`).join(' | ')],
      ]);

    ctx.set('font-advanced', status,
      failed.length === 0
        ? 'All advanced font probes matched expected behavior'
        : `${failed.length} advanced font probe${failed.length === 1 ? '' : 's'} failed`,
      failed.length ? `Failed: ${failed.join(', ')}` : 'No advanced font leaks detected.',
      [
        ['cases', caseRows.map((r) => `${r.title}: ${r.ok ? 'PASS' : 'FAIL'} (${r.current})`).join(' || ')],
        ['Range unique rects', String(uniqueRangeRects)],
        ['Range unique client rects', String(uniqueRangeClientRects)],
        ['Range probes', rangeMetrics.map(([font, v]) => `${font}: ${v.rect} | ${v.clientRects}`).join(' || ')],
        ['SVG unique bboxes', String(uniqueSvgBBoxes)],
        ['SVG unique text lengths', String(uniqueSvgLengths)],
        ['SVG probes', svgMetrics.map(([font, v]) => `${font}: bbox ${v.bbox}, len ${v.computedLength}`).join(' || ')],
        ['FontFace local() results', fontFaceResults.map(([font, v]) => `${font}: ${v}`).join(' | ')],
      ]);
  },
};
