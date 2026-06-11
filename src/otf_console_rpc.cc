#include "otf_console_rpc.h"

#include <deque>
#include <set>
#include <string>

#include "otf_app.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {
namespace {

using Callback = CefMessageRouterBrowserSide::Handler::Callback;

bool HasOnlyParamKeys(CefRefPtr<CefDictionaryValue> params,
                      const std::set<std::string>& allowed,
                      std::string* error) {
  CefDictionaryValue::KeyList keys;
  params->GetKeys(keys);
  for (const auto& key : keys) {
    const std::string k = key.ToString();
    if (!allowed.count(k)) {
      if (error) *error = "unexpected param: " + k;
      return false;
    }
  }
  return true;
}

bool ReadIntParam(CefRefPtr<CefDictionaryValue> params,
                  const std::string& key,
                  int* value,
                  std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_INT) {
    if (error) *error = key + " must be an integer";
    return false;
  }
  if (value) *value = params->GetInt(key);
  return true;
}

bool ReadTabIdParams(CefRefPtr<CefDictionaryValue> params,
                     int* tab_id,
                     std::string* error) {
  return params && HasOnlyParamKeys(params, {"tabId"}, error) &&
         ReadIntParam(params, "tabId", tab_id, error);
}

std::string ConsoleLogsJson(const std::deque<ConsoleEntry>& logs) {
  std::string json = "[";
  bool first = true;
  for (const auto& e : logs) {
    if (!first) json += ",";
    first = false;
    json += JsonObjectBuilder()
                .AddInt("level", e.level)
                .AddString("message", e.message)
                .AddString("source", e.source)
                .AddInt("line", e.line)
                .AddRaw("ts", std::to_string(e.timestamp_ms))
                .Build();
  }
  json += "]";
  return json;
}

void Success(CefRefPtr<Callback> callback, const NativeRpcRequest& request) {
  NativeRpcSuccessRaw(callback, request, "null");
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

}  // namespace

bool HandleConsoleRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler ||
      (request.method != "console.logs" &&
       request.method != "console.clear" &&
       request.method != "console.setWidth")) {
    return false;
  }

  std::string error;
  if (request.method == "console.logs") {
    int tab_id = -1;
    if (!ReadTabIdParams(request.params, &tab_id, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!handler->tab_manager_) {
      NativeRpcSuccessRaw(callback, request, "[]");
      return true;
    }
    NativeRpcSuccessRaw(
        callback, request,
        ConsoleLogsJson(handler->tab_manager_->GetConsoleLogs(tab_id)));
    return true;
  }

  if (request.method == "console.clear") {
    int tab_id = -1;
    if (!ReadTabIdParams(request.params, &tab_id, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (handler->tab_manager_) {
      handler->tab_manager_->ClearConsoleLogs(tab_id);
    }
    Success(callback, request);
    return true;
  }

  if (!request.params ||
      !HasOnlyParamKeys(request.params, {"width"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int width = 0;
  if (!ReadIntParam(request.params, "width", &width, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  if (auto* app = OtfApp::GetInstance()) {
    app->SetConsoleWidth(width);
  }
  Success(callback, request);
  return true;
}

}  // namespace otf
