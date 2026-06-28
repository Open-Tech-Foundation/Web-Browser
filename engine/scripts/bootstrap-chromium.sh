#!/usr/bin/env bash
# Fetch the LATEST Chromium and configure the otf content-layer dev build.
#
# One-time, multi-hour, ~100GB+. Idempotent: re-running syncs to latest and
# regenerates the gn build dir. See engine/README.md.
#
#   CHROMIUM_DIR   where the checkout lives   (default: <repo>/../chromium)
#   DEPOT_TOOLS    depot_tools path           (default: <repo>/../depot_tools)
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ENGINE_DIR/.." && pwd)"
PROJECTS_DIR="$(cd "$REPO_DIR/.." && pwd)"

CHROMIUM_DIR="${CHROMIUM_DIR:-$PROJECTS_DIR/chromium}"
DEPOT_TOOLS="${DEPOT_TOOLS:-$PROJECTS_DIR/depot_tools}"

if [[ ! -x "$DEPOT_TOOLS/fetch" ]]; then
  echo "depot_tools not found at $DEPOT_TOOLS" >&2
  echo "  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git \"$DEPOT_TOOLS\"" >&2
  exit 1
fi
export PATH="$DEPOT_TOOLS:$PATH"
export DEPOT_TOOLS_UPDATE=1

mkdir -p "$CHROMIUM_DIR"
cd "$CHROMIUM_DIR"

# Fetch latest main (no history = smaller/faster). Resume-safe via gclient sync.
if [[ ! -d "$CHROMIUM_DIR/src/.git" ]]; then
  echo ">> fetching latest Chromium (this is the multi-hour step) ..."
  fetch --nohooks --no-history chromium
else
  echo ">> existing checkout — syncing to latest main ..."
  cd src && git checkout origin/main && cd ..
fi

cd "$CHROMIUM_DIR/src"

# Linux build dependencies (best-effort; may need sudo on a fresh host).
if [[ -x ./build/install-build-deps.sh ]]; then
  ./build/install-build-deps.sh --no-prompt || \
    echo "!! install-build-deps failed (continue if deps already present)"
fi

gclient runhooks

# Additive integration: expose engine/ as //otf without copying.
ln -sfn "$ENGINE_DIR" "$CHROMIUM_DIR/src/otf"

# gn only loads a BUILD.gn that is reachable from the root, so the //otf target
# is invisible until something references it. Wire it into the root gn_all group
# (idempotent; re-applied after an upstream re-sync — see plan.md §8). gn_all is
# testonly, which may still depend on our non-testonly target.
ROOT_BUILD="$CHROMIUM_DIR/src/BUILD.gn"
if ! grep -q '//otf:otf_browser' "$ROOT_BUILD"; then
  echo ">> wiring //otf:otf_browser into root gn_all ..."
  sed -i 's|\(^[[:space:]]*\)"//base:base_perftests",|\1"//otf:otf_browser",  # otf engine (additive)\n\1"//base:base_perftests",|' "$ROOT_BUILD"
fi

# gn build dir.
OUT="out/otf"
mkdir -p "$OUT"
cp "$ENGINE_DIR/gn/args.gn" "$OUT/args.gn"
gn gen "$OUT"

echo ""
echo "Bootstrap complete."
echo "  checkout: $CHROMIUM_DIR/src"
echo "  build:    engine/scripts/build-engine.sh"
