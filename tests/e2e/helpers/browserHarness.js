import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';

export const repoRoot = path.resolve(new URL('../../..', import.meta.url).pathname);
export const browserBin = process.env.OTF_BROWSER_BIN ||
  path.join(repoRoot, 'build', 'Release', 'otf-browser');
export const devPort = Number(process.env.OTF_E2E_DEV_PORT || 3000);
export const cdpPort = Number(process.env.OTF_E2E_CDP_PORT || 9222);
export const devUrl = process.env.OTF_E2E_DEV_URL || `http://127.0.0.1:${devPort}`;
export const timeoutMs = Number(process.env.OTF_E2E_TIMEOUT_MS || 45000);

const debug = process.env.OTF_E2E_DEBUG === '1';

export function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function requestText(url) {
  return new Promise((resolve, reject) => {
    const req = http.get(url, (res) => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', (chunk) => {
        body += chunk;
      });
      res.on('end', () => {
        resolve({ statusCode: res.statusCode || 0, body });
      });
    });
    req.on('error', reject);
    req.setTimeout(3000, () => {
      req.destroy(new Error(`timeout requesting ${url}`));
    });
  });
}

export async function waitForHttp(url, deadlineMs = timeoutMs) {
  const deadline = Date.now() + deadlineMs;
  let lastError;
  while (Date.now() < deadline) {
    try {
      const response = await requestText(url);
      if (response.statusCode >= 200 && response.statusCode < 500) {
        return response;
      }
      lastError = new Error(`HTTP ${response.statusCode}`);
    } catch (error) {
      lastError = error;
    }
    await sleep(250);
  }
  throw lastError || new Error(`timed out waiting for ${url}`);
}

async function getJson(url) {
  const response = await requestText(url);
  assert.equal(response.statusCode, 200, `${url} should return HTTP 200`);
  return JSON.parse(response.body);
}

async function getTargets() {
  return getJson(`http://127.0.0.1:${cdpPort}/json/list`);
}

function spawnProcess(command, args, options = {}) {
  return spawn(command, args, {
    cwd: repoRoot,
    env: { ...process.env, ...options.env },
    stdio: debug ? 'inherit' : 'ignore',
    detached: false,
  });
}

async function stopProcess(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) {
    return;
  }
  child.kill('SIGTERM');
  await Promise.race([
    new Promise((resolve) => child.once('exit', resolve)),
    sleep(3000).then(() => {
      if (child.exitCode === null && child.signalCode === null) {
        child.kill('SIGKILL');
      }
    }),
  ]);
}

async function waitForPageTarget() {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const targets = await getTargets();
    const shellTarget = targets.find((item) =>
      item.type === 'page' &&
      item.webSocketDebuggerUrl &&
      (/OTF Browser Shell/i.test(item.title || '') ||
       item.url === devUrl ||
       item.url === `${devUrl}/`)
    );
    if (shellTarget) {
      return shellTarget;
    }
    const internalTarget = targets.find((item) =>
      item.type === 'page' &&
      item.webSocketDebuggerUrl &&
      item.url.startsWith('browser://')
    );
    if (internalTarget) {
      return internalTarget;
    }
    await sleep(250);
  }
  throw new Error('timed out waiting for a debuggable page target');
}

export async function waitForTarget(predicate, deadlineMs = timeoutMs) {
  const deadline = Date.now() + deadlineMs;
  let targets = [];
  while (Date.now() < deadline) {
    targets = await getTargets();
    const target = targets.find((item) => item.type === 'page' &&
      item.webSocketDebuggerUrl &&
      predicate(item));
    if (target) {
      return target;
    }
    await sleep(250);
  }
  throw new Error(`timed out waiting for target; last targets: ${JSON.stringify(targets)}`);
}

export class CdpClient {
  constructor(url) {
    this.nextId = 1;
    this.pending = new Map();
    this.socket = new WebSocket(url);
    this.socket.addEventListener('message', (event) => {
      const message = JSON.parse(event.data);
      if (!message.id || !this.pending.has(message.id)) {
        return;
      }
      const { resolve, reject } = this.pending.get(message.id);
      this.pending.delete(message.id);
      if (message.error) {
        reject(new Error(message.error.message || JSON.stringify(message.error)));
      } else {
        resolve(message.result);
      }
    });
  }

  async open() {
    if (this.socket.readyState === WebSocket.OPEN) {
      return;
    }
    await new Promise((resolve, reject) => {
      this.socket.addEventListener('open', resolve, { once: true });
      this.socket.addEventListener('error', reject, { once: true });
    });
  }

  send(method, params = {}) {
    const id = this.nextId++;
    const payload = JSON.stringify({ id, method, params });
    const promise = new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
    });
    this.socket.send(payload);
    return promise;
  }

  async evaluate(expression) {
    const result = await this.send('Runtime.evaluate', {
      expression,
      returnByValue: true,
      awaitPromise: true,
    });
    if (result.exceptionDetails) {
      throw new Error(result.exceptionDetails.text || 'CDP evaluation failed');
    }
    return result.result.value;
  }

  close() {
    this.socket.close();
  }
}

export async function waitForRoot(cdp) {
  const deadline = Date.now() + 10000;
  let lastValue = null;
  while (Date.now() < deadline) {
    lastValue = await cdp.evaluate(
      '({ href: location.href, title: document.title, readyState: document.readyState, hasRoot: !!document.querySelector("#root") })'
    );
    if (lastValue?.hasRoot) {
      return lastValue;
    }
    await sleep(250);
  }
  return lastValue;
}

export async function waitFor(cdp, expression, predicate, deadlineMs = 10000) {
  const deadline = Date.now() + deadlineMs;
  let value;
  while (Date.now() < deadline) {
    value = await cdp.evaluate(expression);
    if (predicate(value)) {
      return value;
    }
    await sleep(250);
  }
  throw new Error(`timed out waiting for ${expression}; last value: ${JSON.stringify(value)}`);
}

export async function clickSelector(cdp, selector) {
  const rect = await cdp.evaluate(`
    (() => {
      const el = document.querySelector(${JSON.stringify(selector)});
      if (!el) return null;
      const rect = el.getBoundingClientRect();
      return {
        x: rect.left + rect.width / 2,
        y: rect.top + rect.height / 2,
        width: rect.width,
        height: rect.height,
      };
    })()
  `);
  assert.ok(rect, `selector not found: ${selector}`);
  assert.ok(rect.width > 0 && rect.height > 0, `selector is not visible: ${selector}`);
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mousePressed',
    x: rect.x,
    y: rect.y,
    button: 'left',
    clickCount: 1,
  });
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mouseReleased',
    x: rect.x,
    y: rect.y,
    button: 'left',
    clickCount: 1,
  });
}

export async function clickByText(cdp, selector, text) {
  const rect = await cdp.evaluate(`
    (() => {
      const wanted = ${JSON.stringify(text)}.toLowerCase();
      const elements = [...document.querySelectorAll(${JSON.stringify(selector)})];
      const el = elements.find((item) => (item.textContent || '').toLowerCase().includes(wanted));
      if (!el) return null;
      const rect = el.getBoundingClientRect();
      return {
        x: rect.left + rect.width / 2,
        y: rect.top + rect.height / 2,
        width: rect.width,
        height: rect.height,
        text: el.textContent || '',
      };
    })()
  `);
  assert.ok(rect, `selector with text not found: ${selector} / ${text}`);
  assert.ok(rect.width > 0 && rect.height > 0, `selector with text is not visible: ${selector} / ${text}`);
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mousePressed',
    x: rect.x,
    y: rect.y,
    button: 'left',
    clickCount: 1,
  });
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mouseReleased',
    x: rect.x,
    y: rect.y,
    button: 'left',
    clickCount: 1,
  });
}

export async function pressKey(cdp, key, code = key) {
  await cdp.send('Input.dispatchKeyEvent', {
    type: 'keyDown',
    key,
    code,
    windowsVirtualKeyCode: key === 'Enter' ? 13 : undefined,
  });
  await cdp.send('Input.dispatchKeyEvent', {
    type: 'keyUp',
    key,
    code,
    windowsVirtualKeyCode: key === 'Enter' ? 13 : undefined,
  });
}

export async function typeText(cdp, text) {
  await cdp.send('Input.insertText', { text });
}

export async function launchDevBrowser() {
  assert.ok(existsSync(browserBin), `browser binary not found: ${browserBin}`);

  let vite = null;
  let browser = null;
  let cdp = null;
  const tempHome = await mkdtemp(path.join(os.tmpdir(), 'otf-browser-e2e-'));

  try {
    if (process.env.OTF_E2E_SKIP_VITE !== '1') {
      try {
        await waitForHttp(devUrl, 1000);
      } catch {
        vite = spawnProcess(process.execPath, [
          'x',
          'vite',
          'ui',
          '--host',
          '127.0.0.1',
          '--port',
          String(devPort),
          '--strictPort',
        ]);
      }
    }
    await waitForHttp(devUrl, timeoutMs);

    browser = spawnProcess(browserBin, [
      '--no-sandbox',
      `--dev-ui-url=${devUrl}`,
      '--ozone-platform=x11',
      `--remote-debugging-port=${cdpPort}`,
    ], {
      env: {
        HOME: tempHome,
        OTF_DEV_MODE: '1',
      },
    });

    await waitForHttp(`http://127.0.0.1:${cdpPort}/json/version`, timeoutMs);
    const version = await getJson(`http://127.0.0.1:${cdpPort}/json/version`);
    assert.match(version.Browser || '', /Chrome|HeadlessChrome|CEF|OTF/i);

    const target = await waitForPageTarget();
    assert.equal(target.type, 'page');

    cdp = new CdpClient(target.webSocketDebuggerUrl);
    await cdp.open();
    await cdp.send('Runtime.enable');
    await waitForRoot(cdp);

    return {
      cdp,
      devUrl,
      async connectToTarget(predicate, deadlineMs = timeoutMs) {
        const target = await waitForTarget(predicate, deadlineMs);
        const targetCdp = new CdpClient(target.webSocketDebuggerUrl);
        await targetCdp.open();
        await targetCdp.send('Runtime.enable');
        return targetCdp;
      },
      async close() {
        if (cdp) {
          cdp.close();
          cdp = null;
        }
        await stopProcess(browser);
        browser = null;
        await stopProcess(vite);
        vite = null;
        await rm(tempHome, { recursive: true, force: true });
      },
    };
  } catch (error) {
    if (cdp) {
      cdp.close();
    }
    await stopProcess(browser);
    await stopProcess(vite);
    await rm(tempHome, { recursive: true, force: true });
    throw error;
  }
}
