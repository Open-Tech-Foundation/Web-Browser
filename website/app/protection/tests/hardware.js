export default {
  module: 'hardware',
  category: 'privacy',
  produces: [{
    id: 'hardware-profile',
    label: 'CPU & memory',
    entropy: 'high',
    description: 'navigator.hardwareConcurrency + deviceMemory — discloses machine class.',
  }],
  async run(ctx) {
    const cores = Number(navigator.hardwareConcurrency || 0);
    const memory = Number(navigator.deviceMemory || 0);
    const profile = globalThis.__otfHardwareProfile;
    const normalizedCores = [2, 4, 8].includes(cores);
    const normalizedMemory = [0.5, 1, 2, 4, 8].includes(memory);
    const normalized = normalizedCores && normalizedMemory;
    const valid = Number.isFinite(cores) && Number.isFinite(memory) && cores > 0 && memory > 0;
    const status = normalized ? 'ok' : valid ? 'warn' : 'fail';
    ctx.set('hardware-profile', status,
      normalized ? 'Normalized hardware profile' : valid ? 'Raw hardware profile' : 'Invalid hardware values',
      `CPU cores: ${cores || 'unavailable'}, memory: ${memory || 'unavailable'} GB`,
      [
        ['hardware concurrency', cores ? String(cores) : 'unavailable'],
        ['device memory', memory ? `${memory} GB` : 'unavailable'],
        ['cores normalized', String(normalizedCores)],
        ['memory normalized', String(normalizedMemory)],
        ['browser profile', profile ? JSON.stringify(profile) : 'none'],
      ]);
  },
};
