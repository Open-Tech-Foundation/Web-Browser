import { reloadStateFor } from "../runner.js";

const AUDIO_TIMEOUT = 10000;

const audioHashWithTimeout = () => {
  const Ctor = globalThis.OfflineAudioContext || globalThis.webkitOfflineAudioContext;
  if (!Ctor) return Promise.resolve(null);
  return new Promise((resolve) => {
    const timer = setTimeout(() => resolve(null), AUDIO_TIMEOUT);
    try {
      const offline = new Ctor(1, 44100, 44100);
      const osc = offline.createOscillator();
      osc.type = 'sawtooth';
      osc.frequency.value = 440;
      const dst = offline.createGain();
      dst.gain.value = 0.5;
      osc.connect(dst);
      dst.connect(offline.destination);
      osc.start();
      const done = (h) => { clearTimeout(timer); resolve(h); };
      offline.oncomplete = (e) => {
        const data = e.renderedBuffer.getChannelData(0);
        let h = 0;
        for (let i = 0; i < Math.min(data.length, 4410); i += 1) {
          h = ((h << 5) - h + ((data[i] * 100000) | 0)) | 0;
        }
        done(h);
      };
      offline.onerror = () => done(null);
      offline.startRendering();
    } catch (_) { clearTimeout(timer); resolve(null); }
  });
};

// Audio fingerprint test compares the hash across two page loads. The
// runner drives the protocol: it calls capture() the first time, stores
// the value, reloads, then on the next mount calls run() which finds the
// stored value and decides PASS/FAIL.
export default {
  module: 'audio',
  category: 'privacy',
  needsReload: true,
  produces: [{
    id: 'fp-audio',
    label: 'Audio fingerprint',
    entropy: 'high',
    description: 'OfflineAudioContext rendering — verifies the audio hash rotates between loads.',
  }],
  async capture(_ctx) {
    const hash = await audioHashWithTimeout();
    return hash === null ? null : String(hash);
  },
  async run(ctx) {
    const state = reloadStateFor('audio');
    const prev = state.get()?.value ?? null;
    const hash = await audioHashWithTimeout();
    if (hash === null) {
      state.set(null);
      ctx.set('fp-audio', 'ok', 'Audio fingerprint unavailable', 'OfflineAudioContext not exposed.');
      return;
    }
    if (prev === null) {
      // Fallback when reload was skipped — single-shot, can't verify rotation.
      ctx.set('fp-audio', 'warn',
        'Skipped cross-session check',
        `Single-shot hash: ${hash}. Re-run to verify rotation.`,
        [['this-load hash', String(hash)]]);
      return;
    }
    state.set(null);
    const rotated = prev !== String(hash);
    ctx.set('fp-audio', rotated ? 'ok' : 'fail',
      rotated
        ? `Audio rotated across loads (was ${prev}, now ${hash})`
        : 'Audio did NOT rotate — unexpected',
      `hash: ${hash}, prev: ${prev}`,
      [['this-load hash', String(hash)], ['previous-load hash', String(prev)]]);
  },
};
