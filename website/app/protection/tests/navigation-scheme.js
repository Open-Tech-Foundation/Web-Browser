export default {
  module: 'navigation-scheme',
  category: 'security',
  produces: [{
    id: 'navigation-scheme',
    label: 'Dangerous navigation scheme blocking',
    entropy: 'security',
    description: 'javascript:, data:, file: — whether window.open, iframe src, and <a> click are blocked.',
  }],
  async run(ctx) {
    let jsOpenBlocked = true;
    try {
      const w = window.open('javascript:void(0)');
      if (w) { jsOpenBlocked = false; w.close(); }
    } catch (_) { jsOpenBlocked = true; }

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

    const dataToken = 'nav-data-' + Date.now() + '-' + Math.random();
    const jsToken = 'nav-js-' + Date.now() + '-' + Math.random();
    const fileToken = 'nav-file-' + Date.now() + '-' + Math.random();

    const [dataIframe, jsIframe, fileIframe] = await Promise.all([
      runIframeProbe(`data:text/html,<script>parent.postMessage({token:"${dataToken}",result:"loaded"},"*")<\/script>`, dataToken),
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

    const allBlocked = jsOpenBlocked && dataIframe !== 'allowed' && jsIframe !== 'allowed' &&
      fileIframe !== 'allowed' && anchorJsBlocked;
    const allowed = [];
    if (!jsOpenBlocked) allowed.push('window.open javascript');
    if (dataIframe === 'allowed') allowed.push('iframe data:');
    if (jsIframe === 'allowed') allowed.push('iframe javascript:');
    if (fileIframe === 'allowed') allowed.push('iframe file:');
    if (!anchorJsBlocked) allowed.push('anchor javascript:');
    const status = allBlocked ? 'ok' : allowed.length <= 2 ? 'warn' : 'fail';

    ctx.set('navigation-scheme', status,
      allBlocked ? 'All dangerous navigation schemes blocked' : `${allowed.length} scheme(s) allowed: ${allowed.join(', ')}`,
      `javascript window.open: ${jsOpenBlocked ? 'blocked' : 'allowed'} | iframe data:: ${dataIframe} | iframe javascript:: ${jsIframe} | iframe file:: ${fileIframe} | javascript anchor: ${anchorJsBlocked ? 'blocked' : 'allowed'}`,
      [
        ['window.open javascript:', jsOpenBlocked ? 'blocked' : 'allowed'],
        ['iframe data:', dataIframe],
        ['iframe javascript:', jsIframe],
        ['iframe file:', fileIframe],
        ['<a> javascript: click', anchorJsBlocked ? 'blocked' : 'allowed'],
      ]);
  },
};
