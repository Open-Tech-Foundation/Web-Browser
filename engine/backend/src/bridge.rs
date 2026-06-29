//! JS <-> Rust bridge dispatch (plan.md §5).
//!
//! Mirrors the existing wire protocol so the React UI's bridge adapter only
//! swaps transport, not call sites (see ui/src/shared/bridge.js and
//! MIGRATION-bridge-map.md):
//!   request:  { id, method, params }
//!   response: { id, ok, result | error: { code, message } }
//!   event:    { key, ... }
//!
//! Phase 2 wires the transport end to end: parse the inbound envelope, route to
//! a namespace, and shape the response. The ~130 concrete method handlers from
//! the bridge map are filled in module-by-module in Phase 3; until then a known
//! namespace resolves as `Deferred` (answered later by the content layer) and an
//! unknown method gets a synchronous `unknown_method` error, matching the
//! previous dispatcher's contract.

use serde_json::{json, Value};

/// A parsed inbound call. `id`/`params` are part of the stable call shape that
/// the Phase 3 per-method handlers consume; the namespace router only reads
/// `method` today.
#[allow(dead_code)]
pub struct Call<'a> {
    pub id: &'a str,
    pub method: &'a str,
    pub params: &'a Value,
}

/// Outcome of dispatching a call. `Ok`/`Err` are produced once concrete method
/// handlers land in Phase 3; the router resolves everything to `Deferred` today.
#[allow(dead_code)]
pub enum Reply {
    Ok(Value),                  // result payload
    Err { code: String, message: String },
    /// Method recognised but handled asynchronously; the response is emitted
    /// later via the shim once the content layer reports back.
    Deferred,
}

/// Minimal real answers for the handful of methods the React UI awaits during
/// boot, so its chrome can render against the Rust backend (Phase 2b). The full
/// ~130-method surface is filled in module-by-module in Phase 3; until then
/// everything else in a known namespace resolves as `Deferred`.
fn boot_stub(method: &str) -> Option<Reply> {
    let value = match method {
        "session.isGuest" => json!(false),
        "settings.get" => json!({
            "searchEngine": "",
            "customSearchEngines": [],
            "appearanceMode": "auto",
        }),
        "workspaces.list" => json!([{ "id": 1, "name": "Default", "active": true }]),
        "tabs.list" => json!([]),
        "tabs.active" => json!(1),
        "tabs.splitState" => json!({
            "enabled": false,
            "leftTabId": -1,
            "rightTabId": -1,
            "activeTabId": -1,
        }),
        _ => return None,
    };
    Some(Reply::Ok(value))
}

/// Route a method to its namespace handler. Returns None if unknown so the
/// caller can answer with `unknown_method`, matching the previous dispatcher.
pub fn dispatch(call: &Call) -> Option<Reply> {
    if let Some(reply) = boot_stub(call.method) {
        return Some(reply);
    }
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

/// Handle a raw inbound request envelope (`{ id, method, params }`).
///
/// Returns `Some(response_json)` when a response is ready to send back to JS
/// now (success, error, or `unknown_method`), or `None` when the call was routed
/// to the content layer and its response will be emitted later via
/// `otf_bridge_respond`. A malformed envelope yields a `bad_request` error
/// (with an empty id when none could be recovered) rather than panicking.
pub fn handle_request(request_json: &str) -> Option<String> {
    let parsed: Value = match serde_json::from_str(request_json) {
        Ok(v) => v,
        Err(_) => return Some(error_response("", "bad_request", "malformed request envelope")),
    };

    let id = parsed.get("id").and_then(Value::as_str).unwrap_or("");
    let Some(method) = parsed.get("method").and_then(Value::as_str) else {
        return Some(error_response(id, "bad_request", "missing method"));
    };
    let params = parsed.get("params").unwrap_or(&Value::Null);

    let call = Call { id, method, params };
    match dispatch(&call) {
        Some(Reply::Ok(result)) => Some(ok_response(id, result)),
        Some(Reply::Err { code, message }) => Some(error_response(id, &code, &message)),
        Some(Reply::Deferred) => None,
        None => Some(error_response(id, "unknown_method", method)),
    }
}

/// Build a success response envelope.
pub fn ok_response(id: &str, result: Value) -> String {
    json!({ "id": id, "ok": true, "result": result }).to_string()
}

/// Build an error response envelope.
pub fn error_response(id: &str, code: &str, message: &str) -> String {
    json!({
        "id": id,
        "ok": false,
        "error": { "code": code, "message": message },
    })
    .to_string()
}

/// Build an event envelope (`{ key, ... }`) pushed to a subscribed surface.
pub fn event(key: &str, fields: Value) -> String {
    let mut obj = match fields {
        Value::Object(map) => map,
        _ => serde_json::Map::new(),
    };
    obj.insert("key".to_owned(), Value::String(key.to_owned()));
    Value::Object(obj).to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_namespaces_route() {
        for m in ["tabs.list", "ui.events.subscribe", "Storage.getUsageAndQuota"] {
            let call = Call { id: "1", method: m, params: &Value::Null };
            assert!(dispatch(&call).is_some(), "{m} should route");
        }
    }

    #[test]
    fn unknown_method_is_none() {
        let call = Call { id: "1", method: "bogus.thing", params: &Value::Null };
        assert!(dispatch(&call).is_none());
    }

    #[test]
    fn known_method_defers() {
        // Routed to content -> no synchronous response (a non-boot-stub method).
        assert!(handle_request(r#"{"id":"7","method":"tabs.duplicate","params":{}}"#).is_none());
    }

    #[test]
    fn boot_stubs_answer_synchronously() {
        let resp = handle_request(r#"{"id":"9","method":"workspaces.list","params":{}}"#)
            .expect("boot stub responds");
        let v: Value = serde_json::from_str(&resp).unwrap();
        assert_eq!(v["id"], "9");
        assert_eq!(v["ok"], true);
        assert_eq!(v["result"][0]["active"], true);
    }

    #[test]
    fn unknown_method_responds_with_error() {
        let resp = handle_request(r#"{"id":"7","method":"bogus.thing"}"#).expect("sync error");
        let v: Value = serde_json::from_str(&resp).unwrap();
        assert_eq!(v["id"], "7");
        assert_eq!(v["ok"], false);
        assert_eq!(v["error"]["code"], "unknown_method");
    }

    #[test]
    fn malformed_envelope_is_bad_request() {
        let resp = handle_request("not json").expect("sync error");
        let v: Value = serde_json::from_str(&resp).unwrap();
        assert_eq!(v["error"]["code"], "bad_request");
    }

    #[test]
    fn event_carries_key() {
        let e = event("tabTitleChanged", json!({ "tabId": 3, "title": "Hi" }));
        let v: Value = serde_json::from_str(&e).unwrap();
        assert_eq!(v["key"], "tabTitleChanged");
        assert_eq!(v["tabId"], 3);
        assert_eq!(v["title"], "Hi");
    }
}
