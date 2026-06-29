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

    #[test]
    fn content_events_update_model_and_emit() {
        let mut b = Backend::new_for_test();
        let id = b.open_tab("https://example.com");

        let title_ev = b.on_title_changed(id, "Example").expect("known tab");
        let url_ev = b.on_url_changed(id, "https://example.com/page").expect("known tab");
        let load_ev = b.on_load_state(id, false).expect("known tab");

        let tab = b.tab(id).unwrap();
        assert_eq!(tab.title, "Example");
        assert_eq!(tab.url, "https://example.com/page");
        assert!(!tab.loading);

        // Each sink returns the UI event envelope keyed for the bridge.
        for (env, key) in [
            (&title_ev, "tabTitleChanged"),
            (&url_ev, "tabUrlChanged"),
            (&load_ev, "tabLoadStateChanged"),
        ] {
            let v: serde_json::Value = serde_json::from_str(env).unwrap();
            assert_eq!(v["key"], key);
            assert_eq!(v["tabId"], id);
        }
    }

    #[test]
    fn content_events_for_unknown_tab_are_ignored() {
        let mut b = Backend::new_for_test();
        assert!(b.on_title_changed(999, "nope").is_none());
        assert!(b.on_url_changed(999, "nope").is_none());
        assert!(b.on_load_state(999, true).is_none());
    }

    #[test]
    fn subscribe_replays_current_tabs_as_new_tab_events() {
        let mut b = Backend::new_for_test();
        let id = b.open_tab("https://example.com");

        let events = b.replay_for_subscribe(r#"{"id":"s1","method":"ui.events.subscribe"}"#);
        assert_eq!(events.len(), 1);
        let v: serde_json::Value = serde_json::from_str(&events[0]).unwrap();
        assert_eq!(v["key"], "new-tab");
        assert_eq!(v["tab"]["id"], id);
        assert_eq!(v["tab"]["url"], "https://example.com");

        // Non-subscription calls replay nothing.
        assert!(b
            .replay_for_subscribe(r#"{"id":"x","method":"tabs.list"}"#)
            .is_empty());
    }
}
