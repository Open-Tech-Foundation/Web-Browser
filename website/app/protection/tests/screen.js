export default {
  module: 'screen',
  category: 'privacy',
  produces: [{
    id: 'screen-dimensions',
    label: 'Screen dimensions',
    entropy: 'high',
    description: 'Width, height, DPR and color depth — a stable per-device tuple.',
  }],
  async run(ctx) {
    const width = Number(screen.width);
    const height = Number(screen.height);
    const availWidth = Number(screen.availWidth);
    const availHeight = Number(screen.availHeight);
    const dpr = Number(globalThis.devicePixelRatio || 1);
    const colorDepth = Number(screen.colorDepth);
    const pixelDepth = Number(screen.pixelDepth);
    const profile = globalThis.__otfScreenProfile;
    const knownProfiles = new Set([
      '1280x720@1', '1280x720@2',
      '1280x800@1', '1280x800@2',
      '1280x1024@1',
      '1360x768@1',
      '1366x768@1',
      '1440x900@1', '1440x900@2',
      '1600x900@1', '1600x900@2',
      '1680x1050@1',
      '1920x1080@1', '1920x1080@2',
      '1920x1200@1',
      '2560x1080@1',
      '2560x1440@1', '2560x1440@2',
      '2880x1800@2',
      '3440x1440@1',
      '3840x2160@1', '3840x2160@2',
    ]);
    const signature = `${width}x${height}@${dpr}`;
    const commonProfile = knownProfiles.has(signature);
    const validGeometry =
      width > 0 && height > 0 && availWidth > 0 && availHeight > 0 &&
      availWidth <= width && availHeight <= height && dpr > 0;
    const validDepth = colorDepth > 0 && pixelDepth > 0 && colorDepth === pixelDepth;
    const internallyConsistent = validGeometry && validDepth;
    const status = profile || (commonProfile && internallyConsistent)
      ? 'ok'
      : internallyConsistent ? 'warn' : 'fail';
    ctx.set('screen-dimensions', status,
      status === 'ok' ? 'Common screen profile' : status === 'warn' ? 'Raw or uncommon profile' : 'Invalid screen values',
      `${width} × ${height}, available ${availWidth} × ${availHeight}, DPR ${dpr}x`,
      [
        ['resolution', `${width} × ${height}`],
        ['available area', `${availWidth} × ${availHeight}`],
        ['device pixel ratio', `${dpr}x`],
        ['color depth', `${colorDepth} bits`],
        ['pixel depth', `${pixelDepth} bits`],
        ['known bucket', String(commonProfile)],
        ['valid geometry', String(validGeometry)],
        ['valid color depth', String(validDepth)],
        ['browser profile', profile ? JSON.stringify(profile) : 'none'],
      ]);
  },
};
