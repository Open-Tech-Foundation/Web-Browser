//! Tab model + lifecycle. Pure logic so it unit-tests with no Chromium tree;
//! the shim is only touched on the `with-content` path.

use std::os::raw::{c_char, c_int};

pub type TabId = u64;

#[derive(Debug, Clone)]
pub struct Tab {
    pub id: TabId,
    pub url: String,
    pub title: String,
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
    pub fn run(_argc: c_int, _argv: *mut *mut c_char) -> c_int {
        let mut backend = Backend { next_id: 1, ..Default::default() };
        backend.boot();
        backend.open_tab("browser://newtab");
        backend.enter_run_loop()
    }

    fn boot(&mut self) {
        #[cfg(feature = "with-content")]
        unsafe {
            // TODO(phase2): pass real argc/argv and a populated OtfCallbacks
            // whose function pointers route back into self via user_data.
            crate::ffi::otf_browser_init(0, std::ptr::null_mut(), std::mem::zeroed());
            crate::ffi::otf_ui_create(c"browser://shell".as_ptr() as *const _);
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
        self.tabs.push(Tab { id, url: url.to_owned(), title: String::from("New Tab") });
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
}
