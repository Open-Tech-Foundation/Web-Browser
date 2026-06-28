#!/usr/bin/env bash
# Build the otf browser binary: cargo staticlib -> gn/ninja link.
# Run engine/scripts/bootstrap-chromium.sh first.
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ENGINE_DIR/.." && pwd)"
PROJECTS_DIR="$(cd "$REPO_DIR/.." && pwd)"

CHROMIUM_DIR="${CHROMIUM_DIR:-$PROJECTS_DIR/chromium}"
DEPOT_TOOLS="${DEPOT_TOOLS:-$PROJECTS_DIR/depot_tools}"
OUT="${OUT:-out/otf}"

export PATH="$DEPOT_TOOLS:$PATH"

# 1. Rust backend -> libotf_backend.a (with FFI bindings against the shim).
echo ">> cargo build (staticlib, with-content) ..."
( cd "$ENGINE_DIR/backend" && cargo build --release --features with-content )

# 2. gn/ninja link of the browser binary.
echo ">> autoninja otf_browser ..."
( cd "$CHROMIUM_DIR/src" && autoninja -C "$OUT" otf_browser )

echo ""
echo "Built: $CHROMIUM_DIR/src/$OUT/otf_browser"
