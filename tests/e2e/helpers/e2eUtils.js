import assert from 'node:assert/strict';

import {
  navigateFromAddressBar,
  waitFor,
} from './browserHarness.js';

const permissionLabels = {
  downloads: 'Downloads',
  popup: 'Pop-ups',
  images: 'Images',
  javascript: 'JavaScript',
};

export async function connectShell(browser, deadlineMs = 15000) {
  return browser.connectToTarget((target) =>
    target.url === browser.devUrl || target.url === `${browser.devUrl}/`,
    deadlineMs,
  );
}

export async function waitForAddress(cdp, predicate, deadlineMs = 15000) {
  return waitFor(
    cdp,
    `document.querySelector('input[placeholder="Search or enter address..."]')?.value || ''`,
    predicate,
    deadlineMs,
  );
}

export async function clickButtonByText(cdp, text) {
  const clicked = await cdp.evaluate(`
    (() => {
      const needle = ${JSON.stringify(text)};
      const button = [...document.querySelectorAll('button')]
        .find((item) => (item.textContent || '').includes(needle));
      if (!button) return false;
      button.click();
      return true;
    })()
  `);
  assert.equal(clicked, true, `expected to click button containing: ${text}`);
}

// Permission UI button labels (lowercased) keyed by the stored setting value.
const settingButtonLabels = { ask: 'ask', allow: 'allow', block: 'block' };

export async function setSitePermissionFromUi(browser, origin, permission, setting) {
  const label = permissionLabels[permission];
  assert.ok(label, `unknown permission: ${permission}`);
  const targetButtonLabel = settingButtonLabels[setting];
  assert.ok(targetButtonLabel, `unknown setting: ${setting}`);

  const siteDataUrl = `browser://sitedata?origin=${encodeURIComponent(origin)}`;
  await navigateFromAddressBar(browser.cdp, siteDataUrl);
  const siteDataCdp = await browser.connectToTarget((target) =>
    /sitedata\.html/i.test(target.url || '') ||
    /browser:\/\/sitedata/i.test(target.url || ''),
    15000,
  );
  try {
    // Wait for the sitedata page to be fully loaded — both window.cefQuery
    // (the bridge) and the rendered origin text must be present before we
    // poke the back-end, otherwise the evaluate races with navigation.
    await waitFor(
      siteDataCdp,
      `(typeof window.cefQuery === 'function') && (document.body?.innerText || '').includes(${JSON.stringify(origin)}) ? 'ready' : 'pending'`,
      (v) => v === 'ready',
      15000,
    );

    // Drive the permission through the strict site-data RPC. This is the same
    // back-end method the cycle button triggers, but skips the UI click cycle
    // which is fragile when the table is below the viewport or React hasn't
    // finished hydrating.
    const result = await siteDataCdp.evaluate(`
      new Promise((resolve) => {
        if (!window.cefQuery) { resolve({ ok: false, error: 'no cefQuery' }); return; }
        window.cefQuery({
          request: JSON.stringify({
            id: 'e2e-set-site-permission',
            method: 'siteData.setPermission',
            params: {
              origin: ${JSON.stringify(origin)},
              permission: ${JSON.stringify(permission)},
              setting: ${JSON.stringify(setting)},
            },
          }),
          onSuccess: (response) => {
            try { resolve(JSON.parse(response)); } catch (_) { resolve({ ok: false, error: 'bad json' }); }
          },
          onFailure: (code, msg) => resolve({ ok: false, error: msg || ('code ' + code) }),
        });
      })
    `);
    assert.ok(result?.ok, `siteData.setPermission failed: ${result?.error?.message || result?.error || 'unknown'}`);

    // Verify the change by reading back via cefQuery — avoids depending on
    // the React UI reflecting the new value before we return.
    const verify = await siteDataCdp.evaluate(`
      new Promise((resolve) => {
        if (!window.cefQuery) { resolve({}); return; }
        window.cefQuery({
          request: JSON.stringify({
            id: 'e2e-get-site-permissions',
            method: 'siteData.getPermissions',
            params: { origin: ${JSON.stringify(origin)} },
          }),
          onSuccess: (json) => {
            try {
              const parsed = JSON.parse(json);
              resolve(parsed?.ok ? parsed.result : {});
            } catch (_) { resolve({}); }
          },
          onFailure: () => resolve({}),
        });
      })
    `);
    assert.equal(verify?.[permission], setting,
      `${label} permission did not persist (got ${verify?.[permission]}, expected ${setting})`);
  } finally {
    siteDataCdp.close();
  }
}

export async function pressShortcut(cdp, key, code, windowsVirtualKeyCode, modifiers = 2) {
  await cdp.send('Input.dispatchKeyEvent', {
    type: 'rawKeyDown',
    key,
    code,
    windowsVirtualKeyCode,
    nativeVirtualKeyCode: windowsVirtualKeyCode,
    modifiers,
  });
  await cdp.send('Input.dispatchKeyEvent', {
    type: 'keyUp',
    key,
    code,
    windowsVirtualKeyCode,
    nativeVirtualKeyCode: windowsVirtualKeyCode,
    modifiers,
  });
}
