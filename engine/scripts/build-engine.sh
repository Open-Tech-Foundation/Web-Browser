#!/usr/bin/env bash
# Build the otf browser binary.
#
# The Rust backend is now built IN-TREE by gn (//otf:otf_backend, a
# rust_static_library) alongside the C++ shim, so this is a single ninja
# invocation — no separate cargo staticlib step, no bindgen toolchain env, and
# no Rust-std duplicate-symbol hack. cargo is only used for the standalone
# `cargo test` inner loop (see `bun run test:engine`).
#
# Run engine/scripts/bootstrap-chromium.sh first.
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ENGINE_DIR/.." && pwd)"
PROJECTS_DIR="$(cd "$REPO_DIR/.." && pwd)"

CHROMIUM_DIR="${CHROMIUM_DIR:-$PROJECTS_DIR/chromium}"
DEPOT_TOOLS="${DEPOT_TOOLS:-$PROJECTS_DIR/depot_tools}"
OUT="${OUT:-out/otf}"

export PATH="$DEPOT_TOOLS:$PATH"

echo ">> autoninja otf_browser (builds //otf:otf_bridge_bindgen + :otf_backend + the shim) ..."
( cd "$CHROMIUM_DIR/src" && autoninja -C "$OUT" otf_browser )

echo ""
echo "Built: $CHROMIUM_DIR/src/$OUT/otf_browser"
