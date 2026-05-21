// Bundle of small probes against the navigator/screen/css surfaces that
// each leak a few bits on their own but collectively form a stable
// fingerprint. Each is registered as its own row so users can spot which
// surface still leaks.
//
// `set(id, exposed, behavior, detail)` matches the original convention:
//   exposed=true  → 'fail' (info leaks)
//   exposed=false → 'ok'   (protected/normalized)
const setOK = (ctx, id, exposed, behavior, detail) =>
  ctx.set(id, exposed ? 'fail' : 'ok', behavior, detail);

export default {
  module: 'fingerprint-surfaces',
  category: 'privacy',
  produces: [
    { id: 'fp-disk-space',         label: 'Storage quota',          entropy: 'medium' },
    { id: 'fp-battery',            label: 'Battery API',            entropy: 'low' },
    { id: 'fp-platform',           label: 'navigator.platform',     entropy: 'medium' },
    { id: 'fp-plugins',            label: 'navigator.plugins',      entropy: 'medium' },
    { id: 'fp-mime-types',         label: 'navigator.mimeTypes',    entropy: 'medium' },
    { id: 'fp-connection',         label: 'Network Connection API', entropy: 'medium' },
    { id: 'fp-max-touch-points',   label: 'maxTouchPoints',         entropy: 'medium' },
    { id: 'fp-color-depth',        label: 'screen.colorDepth',      entropy: 'medium' },
    { id: 'fp-color-gamut',        label: 'CSS color-gamut',        entropy: 'medium' },
    { id: 'fp-keyboard',           label: 'Keyboard API',           entropy: 'medium' },
    { id: 'fp-pdf-viewer',         label: 'pdfViewerEnabled',       entropy: 'medium' },
    { id: 'fp-pointer',            label: 'CSS pointer',            entropy: 'medium' },
    { id: 'fp-hover',              label: 'CSS hover',              entropy: 'medium' },
    { id: 'fp-forced-colors',      label: 'CSS forced-colors',      entropy: 'low' },
    { id: 'fp-dynamic-range',      label: 'CSS dynamic-range',      entropy: 'medium' },
    { id: 'fp-reduced-motion',     label: 'prefers-reduced-motion', entropy: 'low' },
    { id: 'fp-speech-voices',      label: 'speechSynthesis voices', entropy: 'high' },
    { id: 'fp-enumerate-devices',  label: 'media devices',          entropy: 'high' },
    { id: 'fp-gamepads',           label: 'getGamepads',            entropy: 'low' },
    { id: 'fp-connection-rtt',     label: 'connection RTT',         entropy: 'medium' },
  ],
  async run(ctx) {
    const exists = typeof globalThis !== 'undefined' ? globalThis : window;

    // Disk space — navigator.storage.estimate() reveals quota/usage.
    if (exists.navigator?.storage?.estimate) {
      exists.navigator.storage.estimate().then((info) => {
        const bytes = info?.quota || 0;
        const expectedQuota = 193273528320;
        setOK(ctx, 'fp-disk-space', bytes > 0 && bytes !== expectedQuota,
          bytes > 0 ? `Storage quota exposed: ${(bytes / 1073741824).toFixed(1)} GB` : 'Storage API blocked',
          `quota: ${bytes}, usage: ${info?.usage || 0}`);
      }).catch(() => setOK(ctx, 'fp-disk-space', false, 'Storage estimate threw', ''));
    } else setOK(ctx, 'fp-disk-space', false, 'Storage estimate unavailable', '');

    if (typeof exists.navigator?.getBattery === 'function') {
      exists.navigator.getBattery().then((battery) => {
        const real = battery && (typeof battery.level === 'number' || typeof battery.charging === 'boolean');
        setOK(ctx, 'fp-battery', !!real,
          real ? 'Battery API exposed' : 'Battery API normalized',
          real ? `level: ${battery.level}, charging: ${battery.charging}` : '');
      }).catch(() => setOK(ctx, 'fp-battery', false, 'getBattery threw', ''));
    } else setOK(ctx, 'fp-battery', false, 'getBattery unavailable', '');

    const platform = exists.navigator?.platform || '';
    const uaPlatform = exists.navigator?.userAgentData?.platform || '(not exposed)';
    const platformNormalized = !platform ||
      /^(Win32|Linux(\s+x86_64|\s+i\d+)?|MacIntel|Mac ARM|iPhone|iPad|iPod|Android|WebOS|OpenBSD|FreeBSD)$/i.test(platform);
    setOK(ctx, 'fp-platform', !platformNormalized,
      platformNormalized ? `Platform: ${platform || 'unavailable'}` : `Raw platform exposed: ${platform}`,
      `navigator.platform: ${platform} | userAgentData.platform: ${uaPlatform}`);

    const pluginsLen = exists.navigator?.plugins?.length || 0;
    const mimeLen = exists.navigator?.mimeTypes?.length || 0;
    setOK(ctx, 'fp-plugins',  false, `plugins: ${pluginsLen}`,    `plugins: ${pluginsLen}`);
    setOK(ctx, 'fp-mime-types', false, `mimeTypes: ${mimeLen}`, `mimeTypes: ${mimeLen}`);

    const conn = exists.navigator?.connection || exists.navigator?.mozConnection || exists.navigator?.webkitConnection;
    setOK(ctx, 'fp-connection', !!conn,
      conn ? 'Network Connection API exposed' : 'Connection API blocked',
      conn ? `type: ${conn.effectiveType || '?'}, downlink: ${conn.downlink || '?'}` : '');

    const mtp = exists.navigator?.maxTouchPoints;
    setOK(ctx, 'fp-max-touch-points', mtp > 0,
      mtp > 0 ? `maxTouchPoints: ${mtp}` : 'Touch points hidden',
      `maxTouchPoints: ${mtp}`);

    const colorDepth = exists.screen?.colorDepth;
    setOK(ctx, 'fp-color-depth', colorDepth !== undefined && colorDepth !== 24,
      colorDepth === 24 ? 'colorDepth normalized (24)' : `colorDepth: ${colorDepth}`,
      `colorDepth: ${colorDepth}, pixelDepth: ${exists.screen?.pixelDepth}`);

    const cg = matchMedia('(color-gamut: srgb)').matches ? 'srgb'
      : matchMedia('(color-gamut: p3)').matches ? 'p3'
      : matchMedia('(color-gamut: rec2020)').matches ? 'rec2020' : 'unknown';
    setOK(ctx, 'fp-color-gamut', cg !== 'srgb',
      cg === 'srgb' ? 'color-gamut: srgb (normalized)' : `color-gamut: ${cg}`, cg);

    const hasKeyboard = !!(exists.navigator?.keyboard);
    setOK(ctx, 'fp-keyboard', hasKeyboard,
      hasKeyboard ? 'Keyboard API exposed' : 'Keyboard API blocked', '');

    const pdf = exists.navigator?.pdfViewerEnabled;
    // Accept the standardized `true` value as expected. Anything else
    // (false, undefined) means the surface diverges from the baseline.
    setOK(ctx, 'fp-pdf-viewer', pdf !== true,
      pdf === true ? 'pdfViewerEnabled: true (standardized)'
        : pdf === false ? 'pdfViewerEnabled: false (non-standard)'
        : 'pdfViewerEnabled: unavailable',
      `pdfViewerEnabled: ${pdf}`);

    // Pointer: standardized to `fine` + any-pointer `fine`, coarse=false.
    const pointerFine = matchMedia('(pointer: fine)').matches;
    const pointerCoarse = matchMedia('(pointer: coarse)').matches;
    const anyPointerFine = matchMedia('(any-pointer: fine)').matches;
    const pointerStd = pointerFine && !pointerCoarse && anyPointerFine;
    setOK(ctx, 'fp-pointer', !pointerStd,
      pointerStd ? 'Pointer: fine (standardized)' : 'Pointer: non-standard',
      `pointer:fine=${pointerFine}, pointer:coarse=${pointerCoarse}, any-pointer:fine=${anyPointerFine}`);

    // Hover: standardized to `hover` + any-hover `hover`.
    const hoverHover = matchMedia('(hover: hover)').matches;
    const anyHoverHover = matchMedia('(any-hover: hover)').matches;
    const hoverStd = hoverHover && anyHoverHover;
    setOK(ctx, 'fp-hover', !hoverStd,
      hoverStd ? 'Hover: hover (standardized)' : 'Hover: non-standard',
      `hover:hover=${hoverHover}, any-hover:hover=${anyHoverHover}`);

    const forced = matchMedia('(forced-colors: active)').matches;
    setOK(ctx, 'fp-forced-colors', false,
      forced ? 'Forced colors active' : 'Forced colors not active', '');

    const dr = matchMedia('(dynamic-range: high)').matches ? 'high' : 'standard';
    setOK(ctx, 'fp-dynamic-range', dr === 'high',
      dr === 'high' ? 'Dynamic range: high' : 'Dynamic range: standard', dr);

    const rm = matchMedia('(prefers-reduced-motion: reduce)').matches;
    setOK(ctx, 'fp-reduced-motion', rm,
      rm ? 'prefers-reduced-motion: reduce' : 'prefers-reduced-motion: no-preference', '');

    if (exists.speechSynthesis?.getVoices) {
      const voices = exists.speechSynthesis.getVoices();
      const emit = (v) => setOK(ctx, 'fp-speech-voices', v.length > 0,
        v.length > 0 ? `${v.length} voice(s) exposed` : 'No voices exposed',
        v.map((x) => x.name).join(', '));
      if (voices.length === 0) {
        exists.speechSynthesis.addEventListener('voiceschanged',
          () => emit(exists.speechSynthesis.getVoices()), { once: true });
        setTimeout(() => emit(exists.speechSynthesis.getVoices()), 2000);
      } else emit(voices);
    } else setOK(ctx, 'fp-speech-voices', false, 'speechSynthesis unavailable', '');

    if (exists.navigator?.mediaDevices?.enumerateDevices) {
      exists.navigator.mediaDevices.enumerateDevices().then((devices) => {
        const list = devices || [];
        // Policy contract: enumerateDevices ALWAYS returns these three
        // fixed entries, regardless of real hardware. Labels are empty
        // until microphone permission is granted, then become
        // "Standard <kind>". Anything else means the policy isn't active.
        const EXPECTED = [
          { deviceId: 'fixed-audioinput',  kind: 'audioinput',  groupId: 'fixed-group-a' },
          { deviceId: 'fixed-audiooutput', kind: 'audiooutput', groupId: 'fixed-group-a' },
          { deviceId: 'fixed-videoinput',  kind: 'videoinput',  groupId: 'fixed-group-v' },
        ];
        const shapeMatches = list.length === EXPECTED.length && EXPECTED.every((want, i) => {
          const got = list[i];
          return got
            && got.deviceId === want.deviceId
            && got.kind === want.kind
            && got.groupId === want.groupId
            && (got.label === '' || got.label === `Standard ${want.kind}`);
        });
        const rows = [['device count', String(list.length)]];
        list.forEach((d, i) => {
          rows.push([`#${i + 1} ${d.kind}`, '']);
          rows.push(['    deviceId', d.deviceId]);
          rows.push(['    groupId', d.groupId]);
          rows.push(['    label', d.label ? `"${d.label}"` : '(empty)']);
        });
        ctx.set('fp-enumerate-devices',
          shapeMatches ? 'ok' : 'fail',
          shapeMatches
            ? '3 standardized device entries'
            : `${list.length} device(s) — does not match fixed shape`,
          shapeMatches
            ? 'audioinput + audiooutput + videoinput, all with fixed-* identifiers.'
            : 'Expected exactly 3 entries with fixed-audioinput / fixed-audiooutput / fixed-videoinput IDs.',
          rows);
      }).catch(() => setOK(ctx, 'fp-enumerate-devices', false, 'enumerateDevices threw', ''));
    } else setOK(ctx, 'fp-enumerate-devices', false, 'enumerateDevices unavailable', '');

    if (typeof exists.navigator?.getGamepads === 'function') {
      try {
        const pads = exists.navigator.getGamepads();
        const count = pads ? pads.filter(Boolean).length : 0;
        setOK(ctx, 'fp-gamepads', count > 0,
          count > 0 ? `${count} gamepad(s) exposed` : 'No gamepads exposed',
          `gamepads: ${count}`);
      } catch (_) { setOK(ctx, 'fp-gamepads', false, 'getGamepads threw', ''); }
    } else setOK(ctx, 'fp-gamepads', false, 'getGamepads unavailable', '');

    setOK(ctx, 'fp-connection-rtt', conn && typeof conn.rtt === 'number',
      conn && typeof conn.rtt === 'number' ? `RTT: ${conn.rtt}ms` : 'RTT not exposed',
      conn ? `rtt: ${conn.rtt}` : 'connection unavailable');
  },
};
