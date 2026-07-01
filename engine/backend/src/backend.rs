//! Tab model + lifecycle. The model itself is pure logic so it unit-tests with
//! no Chromium tree; the shim (and the FFI trampolines that route content events
//! back into the model) are only compiled on the `with-content` path.

use std::os::raw::{c_char, c_int};

use crate::bridge;

pub type TabId = u64;
pub type WorkspaceId = u64;

#[derive(Debug, Clone)]
pub struct Tab {
    pub id: TabId,
    pub url: String,
    pub title: String,
    pub loading: bool,
    /// The workspace this tab belongs to. Tabs are only visible while their
    /// workspace is active (`tabs.list` filters by it).
    pub workspace: WorkspaceId,
    /// A private tab: its storage is the workspace's shared in-memory context,
    /// isolated from the workspace's persistent data.
    pub private: bool,
}

/// A named group of tabs. Exactly one workspace is active at a time; switching
/// swaps which tabs the UI sees and which one is shown in the content area.
#[derive(Debug, Clone)]
pub struct Workspace {
    pub id: WorkspaceId,
    pub name: String,
    /// The tab shown when this workspace is active (restored across switches).
    pub active_tab: Option<TabId>,
}

#[derive(Default)]
pub struct Backend {
    tabs: Vec<Tab>,
    next_id: TabId,
    workspaces: Vec<Workspace>,
    active_workspace: WorkspaceId,
    next_workspace_id: WorkspaceId,
    /// Names of popup overlays currently open, so `ui.popup.toggle` knows which
    /// way to flip and the shim's click-outside dismissal can clear one.
    open_popups: Vec<String>,
    /// Set once the UI has subscribed; used to lazily load the boot tab's page
    /// (the content layer isn't ready to host a WebContents until then).
    ui_ready: bool,
    /// The most recent page context-menu hit-test, so the menu overlay can fetch
    /// it on mount (`contextMenu.current`) without racing the show.
    last_context_menu: Option<serde_json::Value>,
}

impl Backend {
    /// A fresh backend with a single default workspace and no tabs.
    fn new() -> Self {
        Backend {
            tabs: Vec::new(),
            next_id: 1,
            workspaces: vec![Workspace {
                id: 1,
                name: String::from("Default"),
                active_tab: None,
            }],
            active_workspace: 1,
            next_workspace_id: 2,
            open_popups: Vec::new(),
            ui_ready: false,
            last_context_menu: None,
        }
    }

    #[cfg(test)]
    pub fn new_for_test() -> Self {
        Backend::new()
    }

    /// Real entry: boot Chromium via the shim, open the UI surface, run the loop.
    pub fn run(argc: c_int, argv: *mut *mut c_char) -> c_int {
        let mut backend = Backend::new();
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
        self.open_tab_ex(url, false)
    }

    fn open_tab_ex(&mut self, url: &str, private: bool) -> TabId {
        let id = self.next_id;
        self.next_id += 1;
        let workspace = self.active_workspace;
        self.tabs.push(Tab {
            id,
            url: url.to_owned(),
            title: String::from("New Tab"),
            loading: false,
            workspace,
            private,
        });
        // Bind the tab to its workspace's storage before its WebContents exists;
        // a private tab uses the workspace's shared in-memory context.
        self.platform_set_tab_workspace(id, workspace, private);
        self.set_active_tab(Some(id));
        id
    }

    pub fn close_tab(&mut self, id: TabId) -> bool {
        let Some(pos) = self.tabs.iter().position(|t| t.id == id) else { return false };
        let ws = self.tabs[pos].workspace;
        self.tabs.remove(pos);
        // If the closed tab was its workspace's active one, fall back to the last
        // remaining tab in that same workspace (or none).
        if self.workspace(ws).and_then(|w| w.active_tab) == Some(id) {
            let fallback = self.tabs.iter().rev().find(|t| t.workspace == ws).map(|t| t.id);
            if let Some(w) = self.workspace_mut(ws) {
                w.active_tab = fallback;
            }
        }
        true
    }

    #[cfg(test)]
    pub fn tab_count(&self) -> usize { self.tabs.len() }
    /// The active tab of the active workspace.
    pub fn active_tab(&self) -> Option<TabId> {
        self.workspace(self.active_workspace).and_then(|w| w.active_tab)
    }
    pub fn tab(&self, id: TabId) -> Option<&Tab> { self.tabs.iter().find(|t| t.id == id) }

    fn tab_mut(&mut self, id: TabId) -> Option<&mut Tab> {
        self.tabs.iter_mut().find(|t| t.id == id)
    }

    // --- workspaces --------------------------------------------------------

    fn workspace(&self, id: WorkspaceId) -> Option<&Workspace> {
        self.workspaces.iter().find(|w| w.id == id)
    }
    fn workspace_mut(&mut self, id: WorkspaceId) -> Option<&mut Workspace> {
        self.workspaces.iter_mut().find(|w| w.id == id)
    }

    /// Record `id` as the active tab of the currently active workspace.
    fn set_active_tab(&mut self, id: Option<TabId>) {
        let ws = self.active_workspace;
        if let Some(w) = self.workspace_mut(ws) {
            w.active_tab = id;
        }
    }

    // --- content event sinks (called from the FFI trampolines) -------------
    // Each updates the authoritative model and returns the UI event envelope to
    // push, or None when the handle is unknown.

    // These emit per-tab *property deltas* in the UI's shape (bridge map §5b):
    // `{ id, key, value }`, where `key` is the tab property. App.jsx routes any
    // event whose key isn't a known event type through UPDATE_TAB, so the address
    // bar / title / spinner track live navigation.

    pub fn on_title_changed(&mut self, id: TabId, title: &str) -> Option<String> {
        let tab = self.tab_mut(id)?;
        tab.title = title.to_owned();
        Some(bridge::event("title", serde_json::json!({ "id": id, "value": title })))
    }

    pub fn on_url_changed(&mut self, id: TabId, url: &str) -> Option<String> {
        let tab = self.tab_mut(id)?;
        tab.url = url.to_owned();
        Some(bridge::event("url", serde_json::json!({ "id": id, "value": url })))
    }

    pub fn on_load_state(&mut self, id: TabId, loading: bool) -> Option<String> {
        let tab = self.tab_mut(id)?;
        tab.loading = loading;
        Some(bridge::event("loading", serde_json::json!({ "id": id, "value": loading })))
    }

    /// A page right-click: stash the hit-test, open otf's own menu overlay, and
    /// push a `context-menu` event (the overlay also fetches it via
    /// `contextMenu.current` on mount to avoid racing the show). Returns the
    /// events to emit.
    pub fn on_context_menu(&mut self, id: TabId, params_json: &str) -> Vec<String> {
        let params: serde_json::Value =
            serde_json::from_str(params_json).unwrap_or_else(|_| serde_json::json!({}));
        let payload = serde_json::json!({ "tabId": id, "params": params });
        self.last_context_menu = Some(payload.clone());
        self.platform_popup_show("contextmenu");
        if !self.open_popups.iter().any(|n| n == "contextmenu") {
            self.open_popups.push("contextmenu".to_owned());
        }
        vec![bridge::event("context-menu", payload)]
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
            // On the first subscribe the content layer is up, so load the boot
            // tab's page (it had no WebContents until now).
            "ui.events.subscribe" => {
                let events = self.replay_events();
                if !self.ui_ready {
                    self.ui_ready = true;
                    if let Some(active) = self.active_tab() {
                        let url = self.tab(active).map(|t| t.url.clone()).unwrap_or_default();
                        if !url.is_empty() {
                            self.platform_navigate(active, &url);
                        }
                    }
                }
                CallOutcome::events(events)
            }

            // Reads that must reflect live state (the stateless boot stubs can't).
            // Tabs are scoped to the active workspace.
            "tabs.list" => {
                let tabs: Vec<serde_json::Value> = self
                    .tabs
                    .iter()
                    .filter(|t| t.workspace == self.active_workspace)
                    .map(Self::tab_json)
                    .collect();
                CallOutcome::reply(bridge::ok_response(id, serde_json::json!(tabs)))
            }
            "tabs.active" => {
                CallOutcome::reply(bridge::ok_response(id, serde_json::json!(self.active_tab())))
            }

            // --- workspaces ---------------------------------------------------
            "workspaces.list" => {
                CallOutcome::reply(bridge::ok_response(id, serde_json::json!(self.workspaces_json())))
            }
            "workspaces.create" => self.workspace_create(id, &params),
            "workspaces.rename" => self.workspace_rename(id, &params),
            "workspaces.delete" => self.workspace_delete(id, &params),
            "workspaces.switch" => self.workspace_switch(id, &params),

            // --- popup overlays ----------------------------------------------
            "ui.popup.show" => self.popup_show(id, &params),
            "ui.popup.hide" => self.popup_hide(id, &params),
            "ui.popup.toggle" => self.popup_toggle(id, &params),
            // A popup's restore stream: no RPC reply. The overlay receives
            // `popup-restore` (broadcast on each show) and filters it by name.
            "ui.popup.restoreSubscribe" => CallOutcome::events(Vec::new()),

            // --- page context menu -------------------------------------------
            // The menu overlay's event stream (broadcast context-menu events).
            "contextMenu.subscribe" => CallOutcome::events(Vec::new()),
            // The overlay fetches the current hit-test on mount.
            "contextMenu.current" => {
                let cur = self.last_context_menu.clone().unwrap_or(serde_json::Value::Null);
                CallOutcome::reply(bridge::ok_response(id, cur))
            }
            // Run a page action (copy/paste/selectAll/copyImage/…) on the tab.
            "contextMenu.exec" => {
                let action = params.get("action").and_then(|v| v.as_str()).unwrap_or("");
                let x = params.get("x").and_then(|v| v.as_i64()).unwrap_or(0) as i32;
                let y = params.get("y").and_then(|v| v.as_i64()).unwrap_or(0) as i32;
                if let Some(tid) = tab_id() {
                    self.platform_context_action(tid, action, x, y);
                }
                CallOutcome::reply(bridge::ok_response(id, serde_json::json!(true)))
            }

            // Tab lifecycle: mutate the model and push the matching events.
            "navigation.newTab" => self.open_new_tab(id, &params, false),
            // A private tab in the current workspace (shared in-memory storage).
            "navigation.newPrivateTab" => self.open_new_tab(id, &params, true),
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
                        self.set_active_tab(Some(tid));
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
                        if let Some(active) = self.active_tab() {
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

    /// Replay the current model to a freshly subscribed UI: the workspace list
    /// and active workspace, a `new-tab` per tab in the active workspace, plus the
    /// active-tab marker, so the chrome populates from authoritative state without
    /// a separate fetch.
    fn replay_events(&self) -> Vec<String> {
        let mut events = vec![
            bridge::event(
                "workspaces-updated",
                serde_json::json!({ "workspaces": self.workspaces_json() }),
            ),
            bridge::event("workspace-changed", serde_json::json!({ "id": self.active_workspace })),
        ];
        events.extend(
            self.tabs
                .iter()
                .filter(|t| t.workspace == self.active_workspace)
                .map(|t| self.new_tab_event(t.id)),
        );
        if let Some(active) = self.active_tab() {
            events.push(bridge::event("active-tab-changed", serde_json::json!({ "id": active })));
        }
        events
    }

    // --- workspace RPC handlers -------------------------------------------

    /// Workspace list in the UI's shape: `[{ id, name, active }]`.
    fn workspaces_json(&self) -> Vec<serde_json::Value> {
        self.workspaces
            .iter()
            .map(|w| {
                serde_json::json!({
                    "id": w.id,
                    "name": w.name,
                    "active": w.id == self.active_workspace,
                })
            })
            .collect()
    }

    fn name_taken(&self, name: &str, except: Option<WorkspaceId>) -> bool {
        self.workspaces
            .iter()
            .any(|w| Some(w.id) != except && w.name == name)
    }

    /// `workspaces.create { name }` — add a workspace, switch to it, and open a
    /// fresh tab in it so it has an active tab. Rejects duplicate names.
    fn workspace_create(&mut self, id: &str, params: &serde_json::Value) -> CallOutcome {
        let name = params.get("name").and_then(|v| v.as_str()).unwrap_or("").trim();
        if name.is_empty() {
            return CallOutcome::reply(bridge::error_response(id, "bad_request", "missing name"));
        }
        if self.name_taken(name, None) {
            return CallOutcome::reply(bridge::error_response(id, "duplicate name", "duplicate name"));
        }
        let ws_id = self.next_workspace_id;
        self.next_workspace_id += 1;
        self.workspaces.push(Workspace { id: ws_id, name: name.to_owned(), active_tab: None });
        self.active_workspace = ws_id;
        let tab_id = self.open_tab("browser://newtab");
        self.platform_show(tab_id);
        let events = vec![
            self.new_tab_event(tab_id),
            bridge::event("workspaces-updated", serde_json::json!({ "workspaces": self.workspaces_json() })),
            bridge::event("workspace-changed", serde_json::json!({ "id": ws_id })),
            bridge::event("active-tab-changed", serde_json::json!({ "id": tab_id })),
        ];
        CallOutcome {
            response: Some(bridge::ok_response(id, serde_json::json!({ "id": ws_id }))),
            events,
        }
    }

    /// `workspaces.rename { id, name }` — rename, rejecting duplicate names.
    fn workspace_rename(&mut self, id: &str, params: &serde_json::Value) -> CallOutcome {
        let ws_id = params.get("id").and_then(|v| v.as_u64());
        let name = params.get("name").and_then(|v| v.as_str()).unwrap_or("").trim().to_owned();
        if name.is_empty() {
            return CallOutcome::reply(bridge::error_response(id, "bad_request", "missing name"));
        }
        if self.name_taken(&name, ws_id) {
            return CallOutcome::reply(bridge::error_response(id, "duplicate name", "duplicate name"));
        }
        let mut events = Vec::new();
        if let Some(w) = ws_id.and_then(|wid| self.workspace_mut(wid)) {
            w.name = name;
            events.push(bridge::event(
                "workspaces-updated",
                serde_json::json!({ "workspaces": self.workspaces_json() }),
            ));
        }
        CallOutcome {
            response: Some(bridge::ok_response(id, serde_json::json!(true))),
            events,
        }
    }

    /// `workspaces.delete { id }` — remove a workspace and its tabs; refuses to
    /// delete the last one. Switches away if the active workspace was deleted.
    fn workspace_delete(&mut self, id: &str, params: &serde_json::Value) -> CallOutcome {
        let ws_id = params.get("id").and_then(|v| v.as_u64());
        if self.workspaces.len() <= 1 {
            return CallOutcome::reply(bridge::error_response(id, "last_workspace", "cannot delete the last workspace"));
        }
        let Some(ws_id) = ws_id.filter(|wid| self.workspace(*wid).is_some()) else {
            return CallOutcome::reply(bridge::ok_response(id, serde_json::json!(false)));
        };

        // Drop the workspace's tabs (and their WebContents).
        let doomed: Vec<TabId> =
            self.tabs.iter().filter(|t| t.workspace == ws_id).map(|t| t.id).collect();
        for tid in &doomed {
            self.platform_close(*tid);
        }
        self.tabs.retain(|t| t.workspace != ws_id);
        self.workspaces.retain(|w| w.id != ws_id);
        // Drop its storage context and mark its data for wipe on next launch.
        self.platform_release_workspace(ws_id);

        let mut events = vec![bridge::event(
            "workspaces-updated",
            serde_json::json!({ "workspaces": self.workspaces_json() }),
        )];
        // If we deleted the active workspace, switch to the first remaining one.
        if self.active_workspace == ws_id {
            self.active_workspace = self.workspaces[0].id;
            events.push(bridge::event("workspace-changed", serde_json::json!({ "id": self.active_workspace })));
            if let Some(active) = self.active_tab() {
                self.platform_show(active);
                events.push(bridge::event("active-tab-changed", serde_json::json!({ "id": active })));
            }
        }
        CallOutcome {
            response: Some(bridge::ok_response(id, serde_json::json!(true))),
            events,
        }
    }

    /// `workspaces.switch { id }` — make a workspace active and show its tab.
    fn workspace_switch(&mut self, id: &str, params: &serde_json::Value) -> CallOutcome {
        let mut events = Vec::new();
        if let Some(ws_id) = params.get("id").and_then(|v| v.as_u64()) {
            if self.workspace(ws_id).is_some() && ws_id != self.active_workspace {
                self.active_workspace = ws_id;
                events.push(bridge::event("workspace-changed", serde_json::json!({ "id": ws_id })));
                if let Some(active) = self.active_tab() {
                    self.platform_show(active);
                    events.push(bridge::event("active-tab-changed", serde_json::json!({ "id": active })));
                }
            }
        }
        CallOutcome {
            response: Some(bridge::ok_response(id, serde_json::json!(true))),
            events,
        }
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
            "private": t.private,
        })
    }

    // --- popup overlays ---------------------------------------------------

    /// Open a new tab (optionally private) in the active workspace, navigate it,
    /// and return the events to push.
    fn open_new_tab(&mut self, id: &str, params: &serde_json::Value, private: bool) -> CallOutcome {
        let url = params
            .get("url")
            .and_then(|v| v.as_str())
            .filter(|s| !s.is_empty())
            .unwrap_or("browser://newtab")
            .to_owned();
        let new_id = self.open_tab_ex(&url, private);
        // Navigate so its WebContents is created, loads the page, and is shown.
        self.platform_navigate(new_id, &url);
        let events = vec![
            self.new_tab_event(new_id),
            bridge::event("active-tab-changed", serde_json::json!({ "id": new_id })),
        ];
        CallOutcome {
            response: Some(bridge::ok_response(id, serde_json::json!({ "tabId": new_id }))),
            events,
        }
    }

    /// `ui.popup.show { name }` — open/raise the named overlay and tell it to
    /// reset transient state (`popup-restore`, broadcast; the overlay filters).
    fn popup_show(&mut self, id: &str, params: &serde_json::Value) -> CallOutcome {
        let name = params.get("name").and_then(|v| v.as_str()).unwrap_or("").to_owned();
        if name.is_empty() {
            return CallOutcome::reply(bridge::error_response(id, "bad_request", "missing name"));
        }
        self.platform_popup_show(&name);
        if !self.open_popups.iter().any(|n| n == &name) {
            self.open_popups.push(name.clone());
        }
        let events = vec![bridge::event("popup-restore", serde_json::json!({ "name": name }))];
        CallOutcome {
            response: Some(bridge::ok_response(id, serde_json::json!(true))),
            events,
        }
    }

    /// `ui.popup.hide { name }` — dismiss the named overlay.
    fn popup_hide(&mut self, id: &str, params: &serde_json::Value) -> CallOutcome {
        let name = params.get("name").and_then(|v| v.as_str()).unwrap_or("").to_owned();
        self.platform_popup_hide(&name);
        self.open_popups.retain(|n| n != &name);
        CallOutcome::reply(bridge::ok_response(id, serde_json::json!(true)))
    }

    /// `ui.popup.toggle { name }` — show if closed, hide if open.
    fn popup_toggle(&mut self, id: &str, params: &serde_json::Value) -> CallOutcome {
        let name = params.get("name").and_then(|v| v.as_str()).unwrap_or("");
        if name.is_empty() {
            return CallOutcome::reply(bridge::error_response(id, "bad_request", "missing name"));
        }
        if self.open_popups.iter().any(|n| n == name) {
            self.popup_hide(id, params)
        } else {
            self.popup_show(id, params)
        }
    }

    /// The shim dismissed a popup (click outside its bounds); drop its open-state.
    pub fn on_popup_closed(&mut self, name: &str) {
        self.open_popups.retain(|n| n != name);
    }

    // --- platform tab ops (drive the content-layer tab host via the FFI) -----
    // The Rust tab id is the caller-assigned handle the shim maps to a
    // WebContents. These are no-ops in the standalone (no-Chromium) build.

    #[cfg(feature = "with-content")]
    fn platform_set_tab_workspace(&self, id: TabId, workspace: WorkspaceId, private: bool) {
        // The workspace id is passed as a string so it stays UUID-ready.
        if let Ok(c) = std::ffi::CString::new(workspace.to_string()) {
            unsafe { crate::ffi::Api::get().tab_set_workspace(id, c.as_ptr(), private as i32) };
        }
    }
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
    #[cfg(feature = "with-content")]
    fn platform_popup_show(&self, name: &str) {
        if let Ok(c) = std::ffi::CString::new(name) {
            unsafe { crate::ffi::Api::get().ui_popup_show(c.as_ptr()) };
        }
    }
    #[cfg(feature = "with-content")]
    fn platform_popup_hide(&self, name: &str) {
        if let Ok(c) = std::ffi::CString::new(name) {
            unsafe { crate::ffi::Api::get().ui_popup_hide(c.as_ptr()) };
        }
    }
    #[cfg(feature = "with-content")]
    fn platform_context_action(&self, id: TabId, action: &str, x: i32, y: i32) {
        if let Ok(c) = std::ffi::CString::new(action) {
            unsafe { crate::ffi::Api::get().tab_context_action(id, c.as_ptr(), x, y) };
        }
    }
    #[cfg(feature = "with-content")]
    fn platform_release_workspace(&self, workspace: WorkspaceId) {
        if let Ok(c) = std::ffi::CString::new(workspace.to_string()) {
            unsafe { crate::ffi::Api::get().ui_workspace_release(c.as_ptr()) };
        }
    }

    #[cfg(not(feature = "with-content"))]
    fn platform_set_tab_workspace(&self, _id: TabId, _workspace: WorkspaceId, _private: bool) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_navigate(&self, _id: TabId, _url: &str) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_show(&self, _id: TabId) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_close(&self, _id: TabId) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_nav_action(&self, _method: &str, _id: TabId) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_popup_show(&self, _name: &str) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_popup_hide(&self, _name: &str) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_context_action(&self, _id: TabId, _action: &str, _x: i32, _y: i32) {}
    #[cfg(not(feature = "with-content"))]
    fn platform_release_workspace(&self, _workspace: WorkspaceId) {}
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
            on_popup_closed: Some(on_popup_closed),
            on_context_menu: Some(on_context_menu),
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

    unsafe extern "C" fn on_popup_closed(user_data: *mut c_void, name: *const c_char) {
        as_backend(user_data).on_popup_closed(c_str(name));
    }

    unsafe extern "C" fn on_context_menu(
        user_data: *mut c_void,
        tab: OtfTabHandle,
        params_json: *const c_char,
    ) {
        for ev in as_backend(user_data).on_context_menu(tab, c_str(params_json)) {
            emit(ev);
        }
    }
}
