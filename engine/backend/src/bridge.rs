//! JS <-> Rust bridge dispatch (plan.md §5).
//!
//! Mirrors the existing wire protocol so the React UI's bridge adapter only
//! swaps transport, not call sites (see ui/src/shared/bridge.js and
//! MIGRATION-bridge-map.md):
//!   request:  { id, method, params }
//!   response: { id, ok, result | error: { code, message } }
//!   event:    { key, ... }
//!
//! Phase 1: protocol shape + routing skeleton. The ~130 methods from the bridge
//! map are wired module-by-module in Phase 3.

/// A parsed inbound call. (JSON parsing wired in Phase 3; kept minimal here.)
pub struct Call<'a> {
    pub id: &'a str,
    pub method: &'a str,
    pub params: &'a str,
}

/// Outcome of dispatching a call.
pub enum Reply {
    Ok(String),                 // result JSON
    Err { code: String, message: String },
    /// Method recognised but handled asynchronously; the response is emitted
    /// later via the shim once the content layer reports back.
    Deferred,
}

/// Route a method to its namespace handler. Returns None if unknown so the
/// caller can answer with `unknown_method`, matching the CEF dispatcher.
pub fn dispatch(call: &Call) -> Option<Reply> {
    let namespace = call.method.split('.').next().unwrap_or("");
    match namespace {
        "tabs" | "navigation" | "workspaces" | "split" => Some(Reply::Deferred),
        "settings" | "history" | "bookmarks" | "downloads" => Some(Reply::Deferred),
        "siteData" | "browsingData" | "cookieTracking" | "permissions" => Some(Reply::Deferred),
        "search" | "session" | "findbar" | "console" => Some(Reply::Deferred),
        "imagePreview" | "docPreview" | "ui" => Some(Reply::Deferred),
        // CDP-style capitalised domains kept verbatim for parity.
        "Storage" | "Browser" => Some(Reply::Deferred),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_namespaces_route() {
        for m in ["tabs.list", "ui.events.subscribe", "Storage.getUsageAndQuota"] {
            let call = Call { id: "1", method: m, params: "{}" };
            assert!(dispatch(&call).is_some(), "{m} should route");
        }
    }

    #[test]
    fn unknown_method_is_none() {
        let call = Call { id: "1", method: "bogus.thing", params: "{}" };
        assert!(dispatch(&call).is_none());
    }
}
