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

# bindgen runs against the shim header on the with-content path. It needs
# Chromium's bundled libclang plus that clang's builtin resource headers
# (stddef.h / stdint.h live there, not on the system include path). Point both
# at the in-tree toolchain so the build is self-contained.
CR_SRC="$CHROMIUM_DIR/src"
export LIBCLANG_PATH="${LIBCLANG_PATH:-$CR_SRC/third_party/rust-toolchain/lib}"
CLANG_RES_INC="$(ls -d "$CR_SRC/third_party/llvm-build/Release+Asserts/lib/clang"/*/include 2>/dev/null | head -1)"
if [[ -n "$CLANG_RES_INC" ]]; then
  export BINDGEN_EXTRA_CLANG_ARGS="-isystem $CLANG_RES_INC ${BINDGEN_EXTRA_CLANG_ARGS:-}"
fi

# 1. Rust backend -> libotf_backend.a (with FFI bindings against the shim).
# Build with CHROMIUM'S Rust toolchain, not the system cargo: the final binary
# also links Chromium's own Rust std, and mixing two std versions yields
# duplicate-symbol link errors (plan.md §8 toolchain alignment). Using the same
# toolchain makes the (still-duplicated) std symbols identical, so the linker can
# safely merge them (see --allow-multiple-definition in //otf/BUILD.gn).
RUST_TOOLCHAIN="$CR_SRC/third_party/rust-toolchain"
export RUSTC="${RUSTC:-$RUST_TOOLCHAIN/bin/rustc}"
CARGO="${CARGO:-$RUST_TOOLCHAIN/bin/cargo}"
echo ">> cargo build (staticlib, with-content) using $CARGO ..."
( cd "$ENGINE_DIR/backend" && "$CARGO" build --release --features with-content )

# 2. gn/ninja link of the browser binary.
echo ">> autoninja otf_browser ..."
( cd "$CHROMIUM_DIR/src" && autoninja -C "$OUT" otf_browser )

echo ""
echo "Built: $CHROMIUM_DIR/src/$OUT/otf_browser"
