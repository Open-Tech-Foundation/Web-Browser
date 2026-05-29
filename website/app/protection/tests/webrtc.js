// WebRTC leak probe.
//
// RTCPeerConnection is a high-entropy / deanonymizing surface, independent of
// any iframe realm:
//   1. ICE host candidates expose the machine's *local* network IPs (and the
//      number of network interfaces) without any user gesture or network call.
//   2. With a STUN server, a server-reflexive (srflx) candidate exposes the
//      *public* IP — bypassing VPNs/proxies that only tunnel TCP.
//   3. RTCRtpReceiver.getCapabilities() returns the full codec/header-extension
//      matrix (H.264/AAC/HEVC availability, hardware decoders) — a stable
//      cross-site fingerprint even when IPs are obfuscated.
//
// Chromium's default mDNS obfuscation replaces raw host IPs with a per-session
// "<uuid>.local" hostname; that hides the raw IP but the candidate gathering,
// the interface count, and the codec matrix all still leak. For an
// antifingerprint browser the bar is "RTCPeerConnection reveals nothing", so
// any of these signals counts against us.

const RTC =
  globalThis.RTCPeerConnection ||
  globalThis.webkitRTCPeerConnection ||
  globalThis.mozRTCPeerConnection ||
  null;

// IPv4 dotted-quad or IPv6 hextet group from an ICE candidate line.
const IPV4_RE = /\b(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\b/;
const IPV6_RE = /\b((?:[0-9a-f]{1,4}:){2,}[0-9a-f]{0,4})\b/i;

const isPrivateV4 = (ip) => {
  const p = ip.split('.').map(Number);
  if (p.length !== 4 || p.some((n) => Number.isNaN(n) || n > 255)) return false;
  return (
    p[0] === 10 ||
    p[0] === 127 ||
    (p[0] === 192 && p[1] === 168) ||
    (p[0] === 172 && p[1] >= 16 && p[1] <= 31) ||
    (p[0] === 169 && p[1] === 254) // link-local
  );
};

// Gather ICE candidates from a throwaway peer connection. Resolves with the
// raw candidate strings once gathering completes or a short timeout elapses.
const gatherCandidates = (timeoutMs = 2500) =>
  new Promise((resolve) => {
    if (!RTC) {
      resolve({ supported: false, candidates: [], error: null });
      return;
    }
    let pc;
    const out = [];
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      try { pc && pc.close(); } catch (_) {}
      resolve({ supported: true, candidates: out, error: null });
    };
    try {
      pc = new RTC({
        iceServers: [{ urls: 'stun:stun.l.google.com:19302' }],
      });
      pc.onicecandidate = (e) => {
        if (e.candidate && e.candidate.candidate) {
          out.push(e.candidate.candidate);
        } else if (!e.candidate) {
          finish(); // null candidate = end of gathering
        }
      };
      pc.onicegatheringstatechange = () => {
        if (pc.iceGatheringState === 'complete') finish();
      };
      // A data channel forces candidate gathering without media permissions.
      pc.createDataChannel('otf-probe');
      pc.createOffer()
        .then((offer) => pc.setLocalDescription(offer))
        .catch(() => finish());
    } catch (e) {
      resolve({ supported: true, candidates: [], error: String(e) });
      return;
    }
    setTimeout(finish, timeoutMs);
  });

const codecCapabilities = () => {
  const recv = globalThis.RTCRtpReceiver;
  if (!recv || typeof recv.getCapabilities !== 'function') return null;
  const safe = (kind) => {
    try { return recv.getCapabilities(kind); } catch (_) { return null; }
  };
  const audio = safe('audio');
  const video = safe('video');
  const count =
    (audio && audio.codecs ? audio.codecs.length : 0) +
    (video && video.codecs ? video.codecs.length : 0);
  return { count, audio, video };
};

export default {
  module: 'webrtc',
  category: 'privacy',
  produces: [{
    id: 'webrtc-leak',
    label: 'WebRTC IP & codec leak',
    entropy: 'high',
    description:
      'RTCPeerConnection ICE candidates expose local (and via STUN, public) IPs ' +
      'with no user gesture, and getCapabilities() leaks the codec matrix. ' +
      'Strong deanonymization + fingerprinting vector.',
  }],
  async run(ctx) {
    const caps = codecCapabilities();
    const { supported, candidates, error } = await gatherCandidates();

    const privateIps = new Set();
    const publicIps = new Set();
    const ipv6s = new Set();
    let mdnsCount = 0;

    for (const c of candidates) {
      if (/\.local\b/i.test(c)) { mdnsCount++; continue; }
      const v4 = c.match(IPV4_RE);
      if (v4) {
        (isPrivateV4(v4[1]) ? privateIps : publicIps).add(v4[1]);
        continue;
      }
      const v6 = c.match(IPV6_RE);
      if (v6) ipv6s.add(v6[1]);
    }

    const rawIpLeak = privateIps.size > 0 || publicIps.size > 0 || ipv6s.size > 0;
    const codecCount = caps ? caps.count : 0;

    // Pass only when RTCPeerConnection reveals nothing: not exposed (or inert
    // — no candidates, no codec matrix). A raw IP is the worst case; an exposed
    // RTC stack that still gathers candidates or hands out the codec matrix is a
    // fail for our antifingerprint bar even under Chromium's mDNS obfuscation.
    let status;
    if (!supported) {
      status = 'ok';
    } else if (rawIpLeak) {
      status = 'fail';
    } else if (candidates.length > 0 || codecCount > 0) {
      status = 'fail';
    } else {
      status = 'warn';
    }

    const summary =
      status === 'ok'
        ? 'RTCPeerConnection not exposed'
        : rawIpLeak
          ? 'Raw IP address leaked via WebRTC'
          : status === 'fail'
            ? 'WebRTC active — candidates / codec matrix exposed'
            : 'WebRTC exposed but inert';

    const detail =
      `supported: ${supported} | candidates: ${candidates.length} | ` +
      `private IPs: ${privateIps.size} | public IPs: ${publicIps.size} | ` +
      `IPv6: ${ipv6s.size} | mDNS: ${mdnsCount} | codecs: ${codecCount}` +
      (error ? ` | error: ${error}` : '');

    ctx.set('webrtc-leak', status, summary, detail, [
      ['RTCPeerConnection exposed', supported ? 'yes' : 'no'],
      ['ICE candidates gathered', String(candidates.length)],
      ['local/private IP leaked', privateIps.size ? [...privateIps].join(', ') : 'none'],
      ['public IP leaked', publicIps.size ? [...publicIps].join(', ') : 'none'],
      ['IPv6 leaked', ipv6s.size ? [...ipv6s].join(', ') : 'none'],
      ['mDNS (.local) candidates', String(mdnsCount)],
      ['codec capabilities exposed', caps ? `${codecCount} codecs` : 'getCapabilities absent'],
    ]);
  },
};
