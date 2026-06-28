// Generates Rust FFI bindings from the shim's C header (plan.md §3:
// "clean C header -> bindgen -> safe Rust wrappers").
//
// Linking of the staticlib into the final binary is done by the gn target, not
// here, so this script only produces bindings.

use std::env;
use std::path::PathBuf;

fn main() {
    // Only the content build needs the FFI bindings (and thus libclang). The
    // standalone harness (`cargo test`) skips bindgen entirely so it builds with
    // no Chromium toolchain present.
    if env::var_os("CARGO_FEATURE_WITH_CONTENT").is_none() {
        return;
    }

    let header = "../shim/bridge.h";
    println!("cargo:rerun-if-changed={header}");

    let bindings = bindgen::Builder::default()
        .header(header)
        .allowlist_function("otf_.*")
        .allowlist_type("Otf.*")
        .generate()
        .expect("failed to generate bindings for bridge.h");

    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out.join("bridge_bindings.rs"))
        .expect("failed to write bridge_bindings.rs");
}
