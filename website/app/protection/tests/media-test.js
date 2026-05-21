// Speaker-test helper for the fp-enumerate-devices row. Not a scored test
// — it's a manual proof-of-life button the user can press to verify audio
// output still works while the MediaDevices identity surface is spoofed.

let lastContext = null;
const subscribers = new Set();

// Reactive state for the in-row affordance. The page subscribes via
// onSpeakerStateChange and re-renders the button label / detail accordingly.
const state = {
  status: 'idle',         // 'idle' | 'playing' | 'ok' | 'fail'
  detail: '',
  rows: [],
};

const notify = () => { for (const fn of subscribers) fn(state); };

export const onSpeakerStateChange = (fn) => {
  subscribers.add(fn);
  return () => subscribers.delete(fn);
};
export const getSpeakerState = () => state;

export const playTestTone = async () => {
  state.status = 'playing';
  state.detail = 'Playing 440 Hz tone…';
  state.rows = [];
  notify();
  try {
    const AudioCtx = window.AudioContext || window.webkitAudioContext;
    if (!AudioCtx) throw new Error('AudioContext unavailable');
    if (lastContext && lastContext.state !== 'closed') {
      try { await lastContext.close(); } catch (_) {}
    }
    const audioCtx = new AudioCtx();
    lastContext = audioCtx;
    if (audioCtx.state === 'suspended') await audioCtx.resume();

    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.type = 'sine';
    osc.frequency.value = 440;
    // Soft envelope so the test doesn't startle anyone wearing headphones.
    gain.gain.setValueAtTime(0.0001, audioCtx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.06, audioCtx.currentTime + 0.05);
    gain.gain.setValueAtTime(0.06, audioCtx.currentTime + 0.6);
    gain.gain.exponentialRampToValueAtTime(0.0001, audioCtx.currentTime + 0.7);
    osc.connect(gain);
    gain.connect(audioCtx.destination);
    osc.start();
    osc.stop(audioCtx.currentTime + 0.72);
    await new Promise((r) => setTimeout(r, 750));

    state.status = 'ok';
    state.detail = '440 Hz tone played through default output. If you didn\'t hear anything, the device may be muted or routed elsewhere.';
    state.rows = [
      ['AudioContext state', audioCtx.state],
      ['sample rate', `${audioCtx.sampleRate} Hz`],
      ['base latency', typeof audioCtx.baseLatency === 'number' ? `${audioCtx.baseLatency.toFixed(4)} s` : 'unavailable'],
      ['output latency', typeof audioCtx.outputLatency === 'number' ? `${audioCtx.outputLatency.toFixed(4)} s` : 'unavailable'],
      ['destination channels', String(audioCtx.destination.channelCount)],
      ['destination max channels', String(audioCtx.destination.maxChannelCount)],
    ];
  } catch (error) {
    state.status = 'fail';
    state.detail = `${error.name}: ${error.message}`;
    state.rows = [];
  }
  notify();
};
