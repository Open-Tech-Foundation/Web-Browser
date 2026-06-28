//! otf backend — the headless control brain (plan.md §1).
//!
//! Owns the tab model, navigation, privacy orchestration and the JS<->Rust
//! bridge dispatch. Drives Chromium's content layer exclusively through the C
//! shim (see ../shim/bridge.h); it holds no widgets and runs no GUI loop —
//! Chromium owns the event loop.

#[cfg(feature = "with-content")]
mod ffi {
    #![allow(non_upper_case_globals, non_camel_case_types, non_snake_case, dead_code)]
    include!(concat!(env!("OUT_DIR"), "/bridge_bindings.rs"));
}

mod backend;
mod bridge;

use std::os::raw::{c_char, c_int};

/// Process entry point, called from the C++ `main` (otf_main.cc).
///
/// Boots Chromium through the shim, installs the bridge dispatcher, opens the
/// UI surface, then enters Chromium's run loop (which blocks until shutdown).
///
/// # Safety
/// `argv` must be a valid `argc`-length array of NUL-terminated C strings, as
/// provided by the C runtime.
#[no_mangle]
pub extern "C" fn otf_backend_main(argc: c_int, argv: *mut *mut c_char) -> c_int {
    backend::Backend::run(argc, argv)
}

#[cfg(test)]
mod tests {
    use super::backend::Backend;

    #[test]
    fn tab_model_create_and_close() {
        let mut b = Backend::new_for_test();
        let a = b.open_tab("https://example.com");
        let c = b.open_tab("https://chromium.org");
        assert_eq!(b.tab_count(), 2);
        assert!(b.close_tab(a));
        assert_eq!(b.active_tab(), Some(c));
        assert_eq!(b.tab_count(), 1);
    }
}
