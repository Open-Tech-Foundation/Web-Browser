#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUN_BIN="${BUN_BIN:-${HOME}/.bun/bin/bun}"
PORT="${OTF_E2E_DEV_PORT:-5000}"
DEV_URL="${OTF_E2E_DEV_URL:-http://127.0.0.1:${PORT}}"

cd "$ROOT_DIR"

"$BUN_BIN" run build:ui >/dev/null
BUN_SERVER_CODE="$(cat <<'JS'
import path from 'node:path';

const root = path.join(process.cwd(), 'build', 'Release', 'ui');
const port = Number(process.env.OTF_E2E_STATIC_PORT || 5000);

function safePath(urlPath) {
  const decoded = decodeURIComponent(urlPath);
  const normalized = path.normalize(decoded);
  let relative = normalized === '/' ? 'index.html' : normalized;
  while (relative.startsWith('../') || relative.startsWith('..\\')) {
    relative = relative.slice(3);
  }
  while (relative.startsWith('/') || relative.startsWith('\\')) {
    relative = relative.slice(1);
  }
  const fullPath = path.join(root, relative);
  return fullPath.startsWith(root) ? fullPath : path.join(root, 'index.html');
}

Bun.serve({
  hostname: '127.0.0.1',
  port,
  async fetch(request) {
    const url = new URL(request.url);
    let file = Bun.file(safePath(url.pathname));
    if (!(await file.exists())) {
      file = Bun.file(path.join(root, 'index.html'));
    }
    return new Response(file);
  },
});

console.log(`OTF_E2E_STATIC_READY http://127.0.0.1:${port}`);
await new Promise(() => {});
JS
)"

OTF_E2E_STATIC_PORT="$PORT" "$BUN_BIN" --eval "$BUN_SERVER_CODE" >/dev/null 2>&1 &
SERVER_PID="$!"

cleanup() {
  if kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT INT TERM

for _ in $(seq 1 120); do
  if "$BUN_BIN" --eval "try { const response = await fetch('${DEV_URL}'); process.exit(response.status < 500 ? 0 : 1); } catch { process.exit(1); }" >/dev/null 2>&1; then
    OTF_E2E_SKIP_VITE=1 OTF_E2E_DEV_URL="$DEV_URL" "$BUN_BIN" test --max-concurrency=1 "$@"
    exit "$?"
  fi
  if ! kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    wait "$SERVER_PID"
  fi
  sleep 0.25
done

echo "timed out waiting for static UI server at ${DEV_URL}" >&2
exit 1
