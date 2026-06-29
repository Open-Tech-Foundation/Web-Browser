//! Tab model + lifecycle. The model itself is pure logic so it unit-tests with
//! no Chromium tree; the shim (and the FFI trampolines that route content events
//! back into the model) are only compiled on the `with-content` path.

use std::os::raw::{c_char, c_int};

use crate::bridge;

pub type TabId = u64;

#[derive(Debug, Clone)]
pub struct Tab {
    pub id: TabId,
    pub url: String,
    pub title: String,
    pub loading: bool,
}

#[derive(Default)]
pub struct Backend {
    tabs: Vec<Tab>,
    active: Option<TabId>,
    next_id: TabId,
}

impl Backend {
    pub fn new_for_test() -> Self {
        Backend { next_id: 1, ..Default::default() }
    }

    /// Real entry: boot Chromium via the shim, open the UI surface, run the loop.
    pub fn run(argc: c_int, argv: *mut *mut c_char) -> c_int {
        let mut backend = Backend { next_id: 1, ..Default::default() };
        backend.boot(argc, argv);
        backend.open_tab("browser://newtab");
        backend.enter_run_loop()
    }

    fn boot(&mut self, _argc: c_int, _argv: *mut *mut c_char) {
        #[cfg(feature = "with-content")]
        unsafe {
            // user_data carries `self` back into the C callbacks; `backend` lives
            // on run()'s stack across the (blocking) run loop, so the pointer
            // stays valid for the whole content lifetime.
            let api = crate::ffi::Api::get();
            let callbacks = ffi_glue::callbacks_for(self as *mut Backend);
            api.init(_argc, _argv, callbacks);
            api.ui_create(c"browser://shell".as_ptr());
        }
    }

    fn enter_run_loop(&mut self) -> c_int {
        #[cfg(feature = "with-content")]
        {
            return crate::ffi::Api::get().run();
        }
        #[cfg(not(feature = "with-content"))]
        0
    }

    // --- tab model (transport-agnostic, fully testable) --------------------

    pub fn open_tab(&mut self, url: &str) -> TabId {
        let id = self.next_id;
        self.next_id += 1;
        self.tabs.push(Tab {
            id,
            url: url.to_owned(),
            title: String::from("New Tab"),
            loading: false,
        });
        self.active = Some(id);
        id
    }

    pub fn close_tab(&mut self, id: TabId) -> bool {
        let Some(pos) = self.tabs.iter().position(|t| t.id == id) else { return false };
        self.tabs.remove(pos);
        if self.active == Some(id) {
            self.active = self.tabs.last().map(|t| t.id);
        }
        true
    }

    pub fn tab_count(&self) -> usize { self.tabs.len() }
    pub fn active_tab(&self) -> Option<TabId> { self.active }
    pub fn tab(&self, id: TabId) -> Option<&Tab> { self.tabs.iter().find(|t| t.id == id) }

    fn tab_mut(&mut self, id: TabId) -> Option<&mut Tab> {
        self.tabs.iter_mut().find(|t| t.id == id)
    }

    // --- content event sinks (called from the FFI trampolines) -------------
    // Each updates the authoritative model and returns the UI event envelope to
    // push, or None when the handle is unknown.

    pub fn on_title_changed(&mut self, id: TabId, title: &str) -> Option<String> {
        let tab = self.tab_mut(id)?;
        tab.title = title.to_owned();
        Some(bridge::event(
            "tabTitleChanged",
            serde_json::json!({ "tabId": id, "title": title }),
        ))
    }

    pub fn on_url_changed(&mut self, id: TabId, url: &str) -> Option<String> {
        let tab = self.tab_mut(id)?;
        tab.url = url.to_owned();
        Some(bridge::event(
            "tabUrlChanged",
            serde_json::json!({ "tabId": id, "url": url }),
        ))
    }

    pub fn on_load_state(&mut self, id: TabId, loading: bool) -> Option<String> {
        let tab = self.tab_mut(id)?;
        tab.loading = loading;
        Some(bridge::event(
            "tabLoadStateChanged",
            serde_json::json!({ "tabId": id, "loading": loading }),
        ))
    }

    // --- bridge ------------------------------------------------------------

    /// Handle an inbound JS bridge request against the authoritative tab model.
    /// Stateful tab methods mutate the model and yield the UI events to push;
    /// everything else falls back to the stateless router (boot stubs, unknown).
    pub fn on_js_call(&mut self, request_json: &str) -> CallOutcome {
        let parsed: serde_json::Value = match serde_json::from_str(request_json) {
            Ok(v) => v,
            Err(_) => {
                return CallOutcome::reply(bridge::error_response(
                    "",
                    "bad_request",
                    "malformed request envelope",
                ))
            }
        };
        let id = parsed.get("id").and_then(|v| v.as_str()).unwrap_or("");
        let method = parsed.get("method").and_then(|v| v.as_str()).unwrap_or("");
        let params = parsed.get("params").cloned().unwrap_or(serde_json::Value::Null);
        let tab_id = || params.get("tabId").and_then(|v| v.as_u64());

        match method {
            // The UI's event stream opening: no RPC reply, replay current state.
            "ui.events.subscribe" => CallOutcome::events(self.replay_events()),

            // Reads that must reflect live state (the stateless boot stubs can't).
            "tabs.list" => {
                let tabs: Vec<serde_json::Value> =
                    self.tabs.iter().map(Self::tab_json).collect();
                CallOutcome::reply(bridge::ok_response(id, serde_json::json!(tabs)))
            }
            "tabs.active" => {
                CallOutcome::reply(bridge::ok_response(id, serde_json::json!(self.active)))
            }

            // Tab lifecycle: mutate the model and push the matching events.
            "navigation.newTab" => {
                let url = params
                    .get("url")
                    .and_then(|v| v.as_str())
                    .filter(|s| !s.is_empty())
                    .unwrap_or("browser://newtab")
                    .to_owned();
                let new_id = self.open_tab(&url);
                // Show the (so far page-less) tab so the content area reflects the
                // switch; its WebContents is created lazily on first navigation.
                self.platform_show(new_id);
                let events = vec![
                    self.new_tab_event(new_id),
                    bridge::event("active-tab-changed", serde_json::json!({ "id": new_id })),
                ];
                CallOutcome {
                    response: Some(bridge::ok_response(id, serde_json::json!({ "tabId": new_id }))),
                    events,
                }
            }
            // Navigate the active (or named) tab; the tab host lazily creates a
            // WebContents the first time and shows it in the content hole.
            "navigation.tab" => {
                let url = params.get("url").and_then(|v| v.as_str()).unwrap_or("");
                if let Some(tid) = tab_id() {
                    if let Some(t) = self.tab_mut(tid) {
                        t.url = url.to_owned();
                        t.loading = true;
                    }
                    self.platform_navigate(tid, url);
                }
                CallOutcome::reply(bridge::ok_response(id, serde_json::json!(true)))
            }
            "tabs.back" | "tabs.forward" | "tabs.reload" | "tabs.stop" => {
                if let Some(tid) = tab_id() {
                    self.platform_nav_action(method, tid);
                }
                CallOutcome::reply(bridge::ok_response(id, serde_json::json!(true)))
            }
            "tabs.switch" => {
                let mut events = Vec::new();
                if let Some(tid) = tab_id() {
                    if self.tab(tid).is_some() {
                        self.active = Some(tid);
                        self.platform_show(tid);
                        events.push(bridge::event(
                            "active-tab-changed",
                            serde_json::json!({ "id": tid }),
                        ));
                    }
                }
                CallOutcome {
                    response: Some(bridge::ok_response(id, serde_json::json!(true))),
                    events,
                }
            }
            "tabs.close" => {
                let mut events = Vec::new();
                if let Some(tid) = tab_id() {
                    if self.close_tab(tid) {
                        self.platform_close(tid);
                        events.push(bridge::event("tab-closed", serde_json::json!({ "id": tid })));
                        if let Some(active) = self.active {
                            self.platform_show(active);
                            events.push(bridge::event(
                                "active-tab-changed",
                                serde_json::json!({ "id": active }),
                            ));
                        }
                    }
                }
                CallOutcome {
                    response: Some(bridge::ok_response(id, serde_json::json!(true))),
                    events,
                }
            }

            _ => CallOutcome::maybe_reply(bridge::handle_request(request_json)),
        }
    }

    /// Replay the current model to a freshly subscribed UI: a `new-tab` per tab
    /// plus the active-tab marker, so the chrome populates from authoritative
    /// state without a separate fetch.
    fn replay_events(&self) -> Vec<String> {
        let mut events: Vec<String> =
            self.tabs.iter().map(|t| self.new_tab_event(t.id)).collect();
        if let Some(active) = self.active {
            events.push(bridge::event("active-tab-changed", serde_json::json!({ "id": active })));
        }
        events
    }

    fn new_tab_event(&self, id: TabId) -> String {
        let tab = self.tab(id).map(Self::tab_json).unwrap_or(serde_json::Value::Null);
        bridge::event("new-tab", serde_json::json!({ "tab": tab, "parentTabId": -1 }))
    }

    fn tab_json(t: &Tab) -> serde_json::Value {
        serde_json::json!({
            "id": t.id,
            "url": t.url,
            "title": t.title,
            "loading": t.loading,
        })
    }

    // --- platform tab ops (drive the content-layer tab host via the FFI) -----
    // The Rust tab id is the caller-assigned handle the shim maps to a
    // WebContents. These are no-ops in the standalone (no-Chromium) build.

    #[cfg(feature = "with-content")]
    fn platform_navigate(&self, id: TabId, url: &str) {
        if let Ok(c) = std::ffi::CString::new(url) {
            unsafe { crate::ffi::Api::get().tab_navigate(id, c.as_ptr()) };
        }
    }
    #[cfg(feature = "with-content")]
    fn platform_show(&self, id: TabId) {
        crate::ffi::Api::get().tab_show(id);
    }
    #[cfg(feature = "with-content")]
    fn platform_close(&self, id: TabId) {
        crate::ffi::Api::get().tab_close(id);
    }
    #[cfg(feature = "with-content")]
    fn platform_nav_action(&self, method: &str, id: TabId) {
        let api = crate::ffi::Api::get();
        match method {
            "tabs.back" => api.tab_go_back(id),
            "tabs.forward" => api.tab_go_forward(id),
            "tabs.reload" => api.tab_reload(id),
            "tabs.stop" => api.tab_stop(id),
            _ => 0,
        };
    }

    #[cfg(not(feature = "with-content"))]
    fn platform_navigate(&self, _id: TabId, _url: &str) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_show(&self, _id: TabId) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_close(&self, _id: TabId) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_nav_action(&self, _method: &str, _id: TabId) {}
}

/// Outcome of an inbound JS call: an optional RPC response plus any UI events to
/// push as a side effect (the trampoline sends the response, then emits events).
pub struct CallOutcome {
    pub response: Option<String>,
    pub events: Vec<String>,
}

impl CallOutcome {
    fn reply(response: String) -> Self {
        CallOutcome { response: Some(response), events: Vec::new() }
    }
    fn maybe_reply(response: Option<String>) -> Self {
        CallOutcome { response, events: Vec::new() }
    }
    fn events(events: Vec<String>) -> Self {
        CallOutcome { response: None, events }
    }
}

/// FFI trampolines: build the C callback table and marshal content events back
/// into the `Backend`. Only compiled when linked against the real shim.
#[cfg(feature = "with-content")]
mod ffi_glue {
    use super::Backend;
    use crate::ffi::{OtfCallbacks, OtfTabHandle};
    use std::ffi::{CStr, CString};
    use std::os::raw::{c_char, c_void};

    pub fn callbacks_for(backend: *mut Backend) -> OtfCallbacks {
        OtfCallbacks {
            user_data: backend as *mut c_void,
            on_js_call: Some(on_js_call),
            on_title_changed: Some(on_title_changed),
            on_url_changed: Some(on_url_changed),
            on_load_state: Some(on_load_state),
            on_unhandled_key: Some(on_unhandled_key),
        }
    }

    unsafe fn as_backend<'a>(user_data: *mut c_void) -> &'a mut Backend {
        &mut *(user_data as *mut Backend)
    }

    unsafe fn c_str<'a>(p: *const c_char) -> &'a str {
        if p.is_null() {
            return "";
        }
        // The shim hands us UTF-8 JSON / URLs; lossy-decode defensively.
        CStr::from_ptr(p).to_str().unwrap_or("")
    }

    /// Push a `{ key, ... }` event envelope back to the UI surface (target 0).
    unsafe fn emit(envelope: String) {
        if let Ok(c) = CString::new(envelope) {
            crate::ffi::Api::get().bridge_emit(0, c.as_ptr());
        }
    }

    unsafe extern "C" fn on_js_call(
        user_data: *mut c_void,
        reply_id: u64,
        request_json: *const c_char,
    ) {
        let backend = as_backend(user_data);
        let outcome = backend.on_js_call(c_str(request_json));
        if let Some(response) = outcome.response {
            if let Ok(c) = CString::new(response) {
                crate::ffi::Api::get().bridge_respond(reply_id, c.as_ptr());
            }
        }
        // No response -> deferred (answered later) or a subscription. Any pushed
        // events (tab lifecycle, subscribe replay) are emitted to the UI surface.
        for ev in outcome.events {
            emit(ev);
        }
    }

    unsafe extern "C" fn on_title_changed(
        user_data: *mut c_void,
        tab: OtfTabHandle,
        title: *const c_char,
    ) {
        if let Some(ev) = as_backend(user_data).on_title_changed(tab, c_str(title)) {
            emit(ev);
        }
    }

    unsafe extern "C" fn on_url_changed(
        user_data: *mut c_void,
        tab: OtfTabHandle,
        url: *const c_char,
    ) {
        if let Some(ev) = as_backend(user_data).on_url_changed(tab, c_str(url)) {
            emit(ev);
        }
    }

    unsafe extern "C" fn on_load_state(
        user_data: *mut c_void,
        tab: OtfTabHandle,
        is_loading: i32,
    ) {
        if let Some(ev) = as_backend(user_data).on_load_state(tab, is_loading != 0) {
            emit(ev);
        }
    }

    unsafe extern "C" fn on_unhandled_key(
        _user_data: *mut c_void,
        _tab: OtfTabHandle,
        _key_code: i32,
        _modifiers: u32,
    ) -> i32 {
        // Phase 3 wires the reserved-shortcut table; for now let it propagate.
        0
    }
}
