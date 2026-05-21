// Local Font Access API is gated behind a permission prompt and requires
// a transient user activation. The runner schedules this module's run()
// inside the user-click handler so queryLocalFonts() may show its prompt.
// We don't await — the prompt resolves in parallel with the rest of the
// suite, and the row updates whenever the user responds (or the timeout
// fires).
const TIMEOUT_MS = 30000;

export default {
  module: 'local-font-api',
  category: 'privacy',
  needsGesture: true,
  produces: [{
    id: 'local-font-api',
    label: 'Local Font Access API',
    entropy: 'high',
    description: 'queryLocalFonts() can enumerate installed fonts post-permission — major fingerprint vector.',
  }],
  run(ctx) {
    if (typeof globalThis.queryLocalFonts !== 'function') {
      ctx.set('local-font-api', 'ok',
        'Local Font Access API not exposed',
        'globalThis.queryLocalFonts is undefined in this runtime.');
      return;
    }
    ctx.set('local-font-api', 'pending-gesture',
      'Awaiting Local Font Access permission prompt',
      'The browser is showing a permission prompt. Allow or block — we report what it does after a real grant.');
    Promise.race([
      globalThis.queryLocalFonts(),
      new Promise((_, reject) => setTimeout(() => reject(new Error('user-no-response')), TIMEOUT_MS)),
    ]).then((fonts) => {
      const count = Array.isArray(fonts) ? fonts.length : 0;
      if (count > 0) {
        const sample = fonts.slice(0, 3)
          .map((f) => f.fullName || f.postscriptName || f.family || '?').join(', ');
        ctx.set('local-font-api', 'fail',
          `queryLocalFonts exposed ${count} fonts after permission grant`,
          `Sample: ${sample}`,
          [['count', String(count)], ['sample', sample]]);
      } else {
        ctx.set('local-font-api', 'ok',
          'queryLocalFonts returned empty list',
          'Browser refuses to enumerate fonts even when the user granted permission.');
      }
    }).catch((error) => {
      if (error && error.message === 'user-no-response') {
        ctx.set('local-font-api', 'warn',
          'Local Font Access prompt unanswered',
          'User did not respond to the permission prompt within 30s.');
      } else {
        ctx.set('local-font-api', 'ok',
          'queryLocalFonts denied or threw',
          `${error.name}: ${error.message}`);
      }
    });
  },
};
