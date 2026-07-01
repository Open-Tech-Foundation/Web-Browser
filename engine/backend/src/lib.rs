//! otf backend — the headless control brain (plan.md §1).
//!
//! Owns the tab model, navigation, privacy orchestration and the JS<->Rust
//! bridge dispatch. Drives Chromium's content layer exclusively through the C
//! shim (see ../shim/bridge.h); it holds no widgets and runs no GUI loop —
//! Chromium owns the event loop.

#[cfg(feature = "with-content")]
mod ffi {
    #![allow(non_upper_case_globals, non_camel_case_types, non_snake_case, dead_code)]

    // The bindgen-generated `extern "C"` decls for shim/bridge.h. Two build paths
    // produce the same surface:
    //   * in-tree gn (`in-tree` feature): the //otf:otf_bridge_bindgen
    //     rust_bindgen target generates them; pull it in via the Chromium prelude
    //     and re-export so the rest of this module sees `OtfApi`, `otf_api`, etc.
    //   * standalone cargo (`with-content` only): build.rs writes them to OUT_DIR
    //     and we include the file directly.
    #[cfg(feature = "in-tree")]
    chromium::import! { "//otf:otf_bridge_bindgen"; }
    #[cfg(feature = "in-tree")]
    pub use otf_bridge_bindgen::*;

    #[cfg(not(feature = "in-tree"))]
    include!(concat!(env!("OUT_DIR"), "/bridge_bindings.rs"));

    use std::os::raw::{c_char, c_int};

    /// Safe facade over the grouped C interface table (bridge.h `OtfApi`).
    ///
    /// Fetches the immutable, process-wide table from `otf_api()` once and
    /// forwards each call to the matching sub-interface's function pointer, so
    /// the backend writes `Api::get().tab_create(url)` instead of dereferencing
    /// raw vtables. The grouping (lifecycle / ui / tabs / bridge) mirrors the C
    /// side one-to-one; new areas (cookies, network, gpu, …) get new methods
    /// here as they land.
    #[derive(Clone, Copy)]
    pub struct Api {
        raw: *const OtfApi,
    }

    impl Api {
        pub fn get() -> Api {
            Api { raw: unsafe { otf_api() } }
        }

        fn root(&self) -> &OtfApi {
            unsafe { &*self.raw }
        }
        fn lifecycle(&self) -> &OtfLifecycleApi {
            unsafe { &*self.root().lifecycle }
        }
        fn ui(&self) -> &OtfUiApi {
            unsafe { &*self.root().ui }
        }
        fn tabs(&self) -> &OtfTabsApi {
            unsafe { &*self.root().tabs }
        }
        fn bridge(&self) -> &OtfBridgeApi {
            unsafe { &*self.root().bridge }
        }

        // --- lifecycle ---
        /// # Safety: `argv` must be a valid `argc`-length C string array.
        pub unsafe fn init(
            &self,
            argc: c_int,
            argv: *mut *mut c_char,
            callbacks: OtfCallbacks,
        ) -> OtfStatus {
            (self.lifecycle().init.unwrap())(argc, argv, callbacks)
        }
        pub fn run(&self) -> OtfStatus {
            unsafe { (self.lifecycle().run.unwrap())() }
        }

        // --- ui ---
        /// # Safety: `url` must be a valid NUL-terminated C string.
        pub unsafe fn ui_create(&self, url: *const c_char) -> OtfStatus {
            (self.ui().create.unwrap())(url)
        }
        /// # Safety: `name` must be a valid NUL-terminated C string.
        pub unsafe fn ui_popup_show(&self, name: *const c_char) -> OtfStatus {
            (self.ui().popup_show.unwrap())(name)
        }
        /// # Safety: `name` must be a valid NUL-terminated C string.
        pub unsafe fn ui_popup_hide(&self, name: *const c_char) -> OtfStatus {
            (self.ui().popup_hide.unwrap())(name)
        }

        // --- tabs (caller-assigned ids: Rust owns the id space) ---
        /// # Safety: `url` must be a valid NUL-terminated C string.
        pub unsafe fn tab_navigate(&self, tab: OtfTabHandle, url: *const c_char) -> OtfStatus {
            (self.tabs().navigate.unwrap())(tab, url)
        }
        pub fn tab_show(&self, tab: OtfTabHandle) -> OtfStatus {
            unsafe { (self.tabs().show.unwrap())(tab) }
        }
        pub fn tab_close(&self, tab: OtfTabHandle) -> OtfStatus {
            unsafe { (self.tabs().close.unwrap())(tab) }
        }
        pub fn tab_reload(&self, tab: OtfTabHandle) -> OtfStatus {
            unsafe { (self.tabs().reload.unwrap())(tab) }
        }
        pub fn tab_stop(&self, tab: OtfTabHandle) -> OtfStatus {
            unsafe { (self.tabs().stop.unwrap())(tab) }
        }
        pub fn tab_go_back(&self, tab: OtfTabHandle) -> OtfStatus {
            unsafe { (self.tabs().go_back.unwrap())(tab) }
        }
        pub fn tab_go_forward(&self, tab: OtfTabHandle) -> OtfStatus {
            unsafe { (self.tabs().go_forward.unwrap())(tab) }
        }

        // --- bridge ---
        /// # Safety: `json` must be a valid NUL-terminated C string.
        pub unsafe fn bridge_respond(&self, reply_id: u64, json: *const c_char) -> OtfStatus {
            (self.bridge().respond.unwrap())(reply_id, json)
        }
        /// # Safety: `json` must be a valid NUL-terminated C string.
        pub unsafe fn bridge_emit(&self, target: OtfTabHandle, json: *const c_char) -> OtfStatus {
            (self.bridge().emit.unwrap())(target, json)
        }
    }
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
    use super::backend::{Backend, CallOutcome};

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

        // Each sink returns a per-tab property delta `{ id, key, value }` (the
        // shape App.jsx routes through UPDATE_TAB).
        for (env, key, value) in [
            (&title_ev, "title", serde_json::json!("Example")),
            (&url_ev, "url", serde_json::json!("https://example.com/page")),
            (&load_ev, "loading", serde_json::json!(false)),
        ] {
            let v: serde_json::Value = serde_json::from_str(env).unwrap();
            assert_eq!(v["key"], key);
            assert_eq!(v["id"], id);
            assert_eq!(v["value"], value);
        }
    }

    #[test]
    fn content_events_for_unknown_tab_are_ignored() {
        let mut b = Backend::new_for_test();
        assert!(b.on_title_changed(999, "nope").is_none());
        assert!(b.on_url_changed(999, "nope").is_none());
        assert!(b.on_load_state(999, true).is_none());
    }

    fn event_keys(events: &[String]) -> Vec<String> {
        events
            .iter()
            .map(|e| {
                serde_json::from_str::<serde_json::Value>(e).unwrap()["key"]
                    .as_str()
                    .unwrap()
                    .to_owned()
            })
            .collect()
    }

    #[test]
    fn subscribe_replays_current_tabs_then_active_marker() {
        let mut b = Backend::new_for_test();
        let id = b.open_tab("https://example.com");

        let outcome = b.on_js_call(r#"{"id":"s1","method":"ui.events.subscribe"}"#);
        assert!(outcome.response.is_none(), "subscriptions have no RPC reply");
        assert_eq!(
            event_keys(&outcome.events),
            ["workspaces-updated", "workspace-changed", "new-tab", "active-tab-changed"]
        );
        let tab_event: serde_json::Value = serde_json::from_str(&outcome.events[2]).unwrap();
        assert_eq!(tab_event["tab"]["id"], id);
        assert_eq!(tab_event["tab"]["url"], "https://example.com");
    }

    #[test]
    fn new_tab_adds_to_model_and_emits_events() {
        let mut b = Backend::new_for_test();
        let outcome = b.on_js_call(r#"{"id":"7","method":"navigation.newTab","params":{}}"#);

        assert_eq!(b.tab_count(), 1);
        let resp: serde_json::Value = serde_json::from_str(&outcome.response.unwrap()).unwrap();
        assert_eq!(resp["ok"], true);
        let new_id = resp["result"]["tabId"].as_u64().unwrap();
        assert_eq!(b.active_tab(), Some(new_id));
        assert_eq!(event_keys(&outcome.events), ["new-tab", "active-tab-changed"]);
    }

    #[test]
    fn switch_tab_updates_active_and_emits() {
        let mut b = Backend::new_for_test();
        let a = b.open_tab("https://a.test");
        let _b = b.open_tab("https://b.test");

        let req = format!(r#"{{"id":"1","method":"tabs.switch","params":{{"tabId":{a}}}}}"#);
        let outcome = b.on_js_call(&req);
        assert_eq!(b.active_tab(), Some(a));
        assert_eq!(event_keys(&outcome.events), ["active-tab-changed"]);
    }

    #[test]
    fn close_tab_removes_and_emits_closed_plus_new_active() {
        let mut b = Backend::new_for_test();
        let a = b.open_tab("https://a.test");
        let z = b.open_tab("https://b.test"); // active

        let req = format!(r#"{{"id":"1","method":"tabs.close","params":{{"tabId":{z}}}}}"#);
        let outcome = b.on_js_call(&req);
        assert_eq!(b.tab_count(), 1);
        assert_eq!(b.active_tab(), Some(a));
        assert_eq!(event_keys(&outcome.events), ["tab-closed", "active-tab-changed"]);
    }

    #[test]
    fn tabs_list_reflects_live_model() {
        let mut b = Backend::new_for_test();
        b.open_tab("https://a.test");
        b.open_tab("https://b.test");
        let outcome = b.on_js_call(r#"{"id":"1","method":"tabs.list","params":{}}"#);
        let resp: serde_json::Value = serde_json::from_str(&outcome.response.unwrap()).unwrap();
        assert_eq!(resp["result"].as_array().unwrap().len(), 2);
    }

    fn result(outcome: &CallOutcome) -> serde_json::Value {
        serde_json::from_str(outcome.response.as_deref().unwrap()).unwrap()
    }

    fn call(b: &mut Backend, method: &str, params: serde_json::Value) -> CallOutcome {
        let req = serde_json::json!({ "id": "t", "method": method, "params": params }).to_string();
        b.on_js_call(&req)
    }

    #[test]
    fn workspaces_start_with_one_default() {
        let mut b = Backend::new_for_test();
        let out = call(&mut b, "workspaces.list", serde_json::json!({}));
        let list = result(&out);
        let arr = list["result"].as_array().unwrap();
        assert_eq!(arr.len(), 1);
        assert_eq!(arr[0]["name"], "Default");
        assert_eq!(arr[0]["active"], true);
    }

    #[test]
    fn create_workspace_switches_and_scopes_tabs() {
        let mut b = Backend::new_for_test();
        b.open_tab("https://a.test"); // lives in Default

        let out = call(&mut b, "workspaces.create", serde_json::json!({ "name": "Work" }));
        let new_ws = result(&out)["result"]["id"].as_u64().unwrap();
        // A fresh tab was opened in the new workspace and it is now active.
        assert_eq!(
            event_keys(&out.events),
            ["new-tab", "workspaces-updated", "workspace-changed", "active-tab-changed"]
        );

        // tabs.list is now scoped to the new (active) workspace: only its own tab.
        let list = result(&call(&mut b, "tabs.list", serde_json::json!({})));
        assert_eq!(list["result"].as_array().unwrap().len(), 1);

        // Switch back to Default and its original tab reappears.
        call(&mut b, "workspaces.switch", serde_json::json!({ "id": 1 }));
        let back = result(&call(&mut b, "tabs.list", serde_json::json!({})));
        assert_eq!(back["result"].as_array().unwrap().len(), 1);
        assert_eq!(back["result"][0]["url"], "https://a.test");
        let _ = new_ws;
    }

    #[test]
    fn create_rejects_duplicate_name() {
        let mut b = Backend::new_for_test();
        let out = call(&mut b, "workspaces.create", serde_json::json!({ "name": "Default" }));
        let resp = result(&out);
        assert_eq!(resp["ok"], false);
        assert_eq!(resp["error"]["code"], "duplicate name");
        assert!(out.events.is_empty());
    }

    #[test]
    fn rename_workspace_updates_list() {
        let mut b = Backend::new_for_test();
        let out = call(&mut b, "workspaces.rename", serde_json::json!({ "id": 1, "name": "Home" }));
        assert_eq!(event_keys(&out.events), ["workspaces-updated"]);
        let list = result(&call(&mut b, "workspaces.list", serde_json::json!({})));
        assert_eq!(list["result"][0]["name"], "Home");
    }

    #[test]
    fn cannot_delete_last_workspace() {
        let mut b = Backend::new_for_test();
        let out = call(&mut b, "workspaces.delete", serde_json::json!({ "id": 1 }));
        assert_eq!(result(&out)["error"]["code"], "last_workspace");
    }

    #[test]
    fn delete_active_workspace_switches_away() {
        let mut b = Backend::new_for_test();
        let created = call(&mut b, "workspaces.create", serde_json::json!({ "name": "Work" }));
        let ws = result(&created)["result"]["id"].as_u64().unwrap();

        let out = call(&mut b, "workspaces.delete", serde_json::json!({ "id": ws }));
        assert_eq!(result(&out)["ok"], true);
        // Back on Default, and Work's tab is gone.
        let list = result(&call(&mut b, "workspaces.list", serde_json::json!({})));
        assert_eq!(list["result"].as_array().unwrap().len(), 1);
        assert_eq!(list["result"][0]["id"], 1);
    }
}
