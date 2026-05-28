export default {
  module: 'navigation-scheme',
  category: 'security',
  produces: [{
    id: 'navigation-scheme',
    label: 'Dangerous navigation scheme blocking',
    entropy: 'security',
    description: 'javascript:, file: — whether iframe src and <a> click are blocked. data:/blob: iframes are intentionally allowed so antifingerprint policy reaches those realms (top-level data:/blob: navigation is still blocked).',
  }],
  async run(ctx) {
    const runIframeProbe = (url, token, timeoutMs = 2000) => new Promise((resolve) => {
      const iframe = document.createElement('iframe');
      iframe.hidden = true;
      iframe.setAttribute('aria-hidden', 'true');
      const cleanup = () => {
        window.removeEventListener('message', onMessage);
        iframe.remove();
      };
      const onMessage = (event) => {
        if (!event.data || event.data.token !== token) return;
        cleanup();
        resolve('allowed');
      };
      window.addEventListener('message', onMessage);
      iframe.src = url;
      document.body.appendChild(iframe);
      setTimeout(() => { cleanup(); resolve('blocked'); }, timeoutMs);
    });

    const jsToken = 'nav-js-' + Date.now() + '-' + Math.random();
    const fileToken = 'nav-file-' + Date.now() + '-' + Math.random();

    const [jsIframe, fileIframe] = await Promise.all([
      runIframeProbe(`javascript:void(parent.postMessage({token:"${jsToken}",result:"executed"},"*"))`, jsToken),
      runIframeProbe('file:///etc/passwd', fileToken),
    ]);

    let anchorJsBlocked = true;
    const canary = '__otf_scheme_anchor_' + Date.now();
    window[canary] = 'secure';
    try {
      const a = document.createElement('a');
      a.href = "javascript:void(window['" + canary + "']='pwned')";
      a.click();
      if (window[canary] === 'pwned') anchorJsBlocked = false;
    } catch (_) {}

    const allBlocked = jsIframe !== 'allowed' && fileIframe !== 'allowed' && anchorJsBlocked;
    const allowed = [];
    if (jsIframe === 'allowed') allowed.push('iframe javascript:');
    if (fileIframe === 'allowed') allowed.push('iframe file:');
    if (!anchorJsBlocked) allowed.push('anchor javascript:');
    const status = allBlocked ? 'ok' : allowed.length <= 1 ? 'warn' : 'fail';

    ctx.set('navigation-scheme', status,
      allBlocked ? 'All dangerous navigation schemes blocked' : `${allowed.length} scheme(s) allowed: ${allowed.join(', ')}`,
      `iframe javascript:: ${jsIframe} | iframe file:: ${fileIframe} | javascript anchor: ${anchorJsBlocked ? 'blocked' : 'allowed'}`,
      [
        ['iframe javascript:', jsIframe],
        ['iframe file:', fileIframe],
        ['<a> javascript: click', anchorJsBlocked ? 'blocked' : 'allowed'],
      ]);
  },
};
