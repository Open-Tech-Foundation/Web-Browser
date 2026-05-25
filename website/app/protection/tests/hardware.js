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
    const coresOk = cores === 6;
    const memoryOk = memory === 16;
    const fixed = coresOk && memoryOk;
    const status = fixed ? 'ok' : coresOk || memoryOk ? 'warn' : 'fail';
    ctx.set('hardware-profile', status,
      fixed ? 'Fixed hardware profile' : coresOk || memoryOk ? 'Partially fixed' : 'Raw hardware values',
      `CPU cores: ${cores || 'unavailable'}, memory: ${memory || 'unavailable'} GB`,
      [
        ['hardware concurrency', cores ? String(cores) : 'unavailable'],
        ['device memory', memory ? `${memory} GB` : 'unavailable'],
        ['cores fixed', String(coresOk)],
        ['memory fixed', String(memoryOk)],
        ['browser profile', profile ? JSON.stringify(profile) : 'none'],
      ]);
  },
};
