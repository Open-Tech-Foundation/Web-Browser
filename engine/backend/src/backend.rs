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
            let callbacks = ffi_glue::callbacks_for(self as *mut Backend);
            crate::ffi::otf_browser_init(_argc, _argv, callbacks);
            crate::ffi::otf_ui_create(c"browser://shell".as_ptr());
        }
    }

    fn enter_run_loop(&mut self) -> c_int {
        #[cfg(feature = "with-content")]
        unsafe {
            return crate::ffi::otf_browser_run();
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

    /// Handle an inbound JS bridge request. `Some` is a response to send now;
    /// `None` means the call was routed to the content layer (answered later).
    pub fn on_js_call(&mut self, request_json: &str) -> Option<String> {
        bridge::handle_request(request_json)
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
            crate::ffi::otf_bridge_emit(0, c.as_ptr());
        }
    }

    unsafe extern "C" fn on_js_call(
        user_data: *mut c_void,
        reply_id: u64,
        request_json: *const c_char,
    ) {
        let backend = as_backend(user_data);
        if let Some(response) = backend.on_js_call(c_str(request_json)) {
            if let Ok(c) = CString::new(response) {
                crate::ffi::otf_bridge_respond(reply_id, c.as_ptr());
            }
        }
        // None -> deferred; the content layer responds later via the shim.
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
