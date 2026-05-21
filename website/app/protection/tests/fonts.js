export default {
  module: 'fonts',
  category: 'privacy',
  produces: [{
    id: 'font-surface',
    label: 'Font surface',
    entropy: 'high',
    description: 'Installed-font enumeration via document.fonts, canvas measureText, and DOM metrics.',
  }],
  async run(ctx) {
    const allowedFonts = ['Arial', 'Helvetica', 'Times New Roman', 'Courier New'];
    const probeFonts = ['Calibri', 'Segoe UI', 'Roboto', 'Ubuntu', 'Noto Sans', 'DejaVu Sans', 'SF Pro Display'];
    const profile = globalThis.__otfFontProfile;
    const fontSet = document.fonts;
    const fontCheckAvailable = !!(fontSet && typeof fontSet.check === 'function');
    const checkFont = (name) => {
      try { return fontCheckAvailable && fontSet.check(`12px "${name}"`); }
      catch { return false; }
    };
    const allowedDetected = allowedFonts.filter(checkFont);
    const extraDetected = probeFonts.filter(checkFont);
    const fontSetSize = fontSet && typeof fontSet.size === 'number' ? fontSet.size : 'unavailable';
    let enumeratedFonts = [];
    try {
      if (fontSet && typeof fontSet.forEach === 'function') {
        fontSet.forEach((fontFace) => {
          if (fontFace && fontFace.family) enumeratedFonts.push(fontFace.family);
        });
      }
    } catch {}
    enumeratedFonts = [...new Set(enumeratedFonts)];

    const canvas = document.createElement('canvas');
    const c2d = canvas.getContext('2d');
    let canvasNormalized = false;
    let arialWidth = 'unavailable';
    let rareWidth = 'unavailable';
    if (c2d && typeof c2d.measureText === 'function') {
      c2d.font = '72px Arial';
      arialWidth = c2d.measureText('mmmmmmmmmm').width;
      c2d.font = '72px "Calibri"';
      rareWidth = c2d.measureText('mmmmmmmmmm').width;
      canvasNormalized = Math.abs(arialWidth - rareWidth) < 0.01;
    }

    const metricText = 'mmmMMMmmmlllmmmLLL₹▁₺₸ẞॿmmmiiimmmIIImmmwwwmmmWWW';
    const measureDomFont = (fontFamily) => {
      const node = document.createElement('span');
      node.textContent = metricText;
      node.style.cssText = [
        'position:absolute', 'left:-10000px', 'top:-10000px',
        'white-space:nowrap', 'font-size:72px',
        `font-family:${fontFamily}, monospace`,
      ].join(';');
      document.body.appendChild(node);
      const rect = node.getBoundingClientRect();
      const result = {
        width: rect.width,
        height: rect.height,
        display: `${Math.round(rect.width * 100) / 100}x${Math.round(rect.height * 100) / 100}`,
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
      : limitedApi && (canvasNormalized || domMetricsNormalized) ? 'warn' : 'fail';

    ctx.set('font-surface', status,
      status === 'ok' ? 'Font probing limited'
        : status === 'warn' ? 'Font API partially limited' : 'Additional fonts exposed',
      `Allowed detected: ${allowedDetected.length}, extra detected: ${extraDetected.length}, DOM range: ${domWidthRange.toFixed(2)}x${domHeightRange.toFixed(2)}`,
      [
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
        ['DOM font metrics probes', domMetrics.map(([f, m]) => `${f}: ${m.display}`).join(' | ')],
        ['browser profile', profile ? JSON.stringify(profile) : 'none'],
      ]);
  },
};
