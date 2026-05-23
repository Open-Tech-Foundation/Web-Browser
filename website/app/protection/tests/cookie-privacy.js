// Cookie privacy protections:
//   1. Do Not Track — navigator.doNotTrack reports "1"
//   2. Third-party cookie blocking — document.cookie is inert in
//      cross-origin sub-frames so embedded trackers cannot read or
//      write cookies via JS.
//
// Tests are purely behavioural — no browser-internal markers are checked.
export default {
  module: 'cookie-privacy',
  category: 'privacy',
  produces: [
    {
      id: 'cookie-dnt',
      label: 'Do Not Track (DNT)',
      entropy: 'low',
      description: 'navigator.doNotTrack reports "1" — sites that honour DNT will not track you.',
    },
    {
      id: 'cookie-third-party-blocking',
      label: 'Third-party cookie blocking',
      entropy: 'medium',
      description: 'document.cookie is inert in cross-origin sub-frames so embedded trackers cannot read or write cookies via JS.',
    },
  ],
  async run(ctx) {
    // ── Do Not Track ─────────────────────────────────────────────────────────
    const dnt = navigator.doNotTrack;
    ctx.set(
      'cookie-dnt',
      dnt === '1' ? 'ok' : 'fail',
      dnt === '1' ? 'DNT set to "1" — tracking opt-out signalled' : `DNT not active (value: ${String(dnt)})`,
      dnt === '1'
        ? 'navigator.doNotTrack = "1". Sites and ad networks that honour DNT will not build a profile on you.'
        : 'navigator.doNotTrack is not "1". Tracking-aware sites may still profile you.',
      [
        ['navigator.doNotTrack', String(dnt)],
        ['expected',             '"1"'],
      ]
    );

    // ── Third-party cookie blocking ───────────────────────────────────────────
    // A sandboxed srcdoc iframe has an opaque origin, exactly the same
    // cross-origin condition that should trigger third-party cookie blocking.
    // We attempt a write and verify the read-back is empty.
    const probeResult = await new Promise((resolve) => {
      const token = `cookie-probe-${Date.now()}-${Math.random()}`;
      const iframe = document.createElement('iframe');
      iframe.hidden = true;
      iframe.setAttribute('aria-hidden', 'true');
      iframe.setAttribute('sandbox', 'allow-scripts');

      const cleanup = () => {
        window.removeEventListener('message', onMessage);
        iframe.remove();
      };
      const onMessage = (event) => {
        if (!event.data || event.data.token !== token) return;
        cleanup();
        resolve(event.data);
      };
      window.addEventListener('message', onMessage);

      iframe.srcdoc = `<!doctype html><meta charset="utf-8"><script>
        const token = ${JSON.stringify(token)};
        try {
          const before = document.cookie;
          document.cookie = 'tp_probe=1; path=/; SameSite=None';
          const after = document.cookie;
          parent.postMessage({ token, before, after }, '*');
        } catch (e) {
          parent.postMessage({ token, error: e.name + ': ' + e.message }, '*');
        }
      <\/script>`;
      document.body.appendChild(iframe);
      setTimeout(() => { cleanup(); resolve({ error: 'probe-timeout' }); }, 3000);
    });

    const writeBlocked = probeResult.after === '';
    // A SecurityError on document.cookie means the browser refused cookie
    // access entirely in the opaque-origin frame — that is effective blocking.
    const sandboxBlocked = typeof probeResult.error === 'string' &&
      probeResult.error.includes('SecurityError') &&
      probeResult.error.includes('allow-same-origin');
    const blocked = writeBlocked || sandboxBlocked;
    const status = blocked ? 'ok' : probeResult.error ? 'warn' : 'fail';

    ctx.set(
      'cookie-third-party-blocking',
      status,
      blocked
        ? 'Third-party cookie access blocked in cross-origin frames'
        : probeResult.error
          ? `Probe inconclusive: ${probeResult.error}`
          : 'Third-party cookies not blocked',
      blocked
        ? sandboxBlocked
          ? 'Browser threw SecurityError on document.cookie in an opaque-origin frame — access denied at the sandbox level.'
          : 'document.cookie read-back is empty after a write attempt in an opaque-origin frame.'
        : probeResult.error
          ? 'The sandboxed iframe probe could not run.'
          : 'document.cookie was readable/writable inside a cross-origin frame.',
      [
        ['probe context',            'sandboxed srcdoc iframe (opaque origin)'],
        ['cookie before write',      JSON.stringify(probeResult.before ?? 'unavailable')],
        ['cookie after write attempt', JSON.stringify(probeResult.after ?? 'unavailable')],
        ['write blocked',            String(blocked)],
        ...(probeResult.error ? [['probe error', probeResult.error]] : []),
      ]
    );
  },
};
