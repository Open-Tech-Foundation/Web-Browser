#include "otf_native_rpc.h"

#include <set>

#include "include/cef_parser.h"
#include "otf_utils.h"

namespace otf {
namespace {

bool HasOnlyKeys(CefRefPtr<CefDictionaryValue> dict,
                 const std::set<std::string>& allowed,
                 std::string* error) {
  CefDictionaryValue::KeyList keys;
  dict->GetKeys(keys);
  for (const auto& key : keys) {
    const std::string k = key.ToString();
    if (!allowed.count(k)) {
      if (error) *error = "unexpected key: " + k;
      return false;
    }
  }
  return true;
}

std::string ErrorJson(const std::string& code, const std::string& message) {
  return JsonObjectBuilder()
      .AddString("code", code)
      .AddString("message", message)
      .Build();
}

}  // namespace

bool ParseNativeRpcRequest(const std::string& wire,
                           NativeRpcRequest* out,
                           std::string* error) {
  if (!out) return false;
  CefRefPtr<CefValue> root =
      CefParseJSON(wire, JSON_PARSER_RFC);
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    if (error) *error = "RPC request must be a JSON object";
    return false;
  }

  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict || !HasOnlyKeys(dict, {"id", "method", "params"}, error)) {
    return false;
  }

  if (!dict->HasKey("id") || dict->GetType("id") != VTYPE_STRING) {
    if (error) *error = "RPC id must be a string";
    return false;
  }
  if (!dict->HasKey("method") || dict->GetType("method") != VTYPE_STRING) {
    if (error) *error = "RPC method must be a string";
    return false;
  }
  if (!dict->HasKey("params") || dict->GetType("params") != VTYPE_DICTIONARY) {
    if (error) *error = "RPC params must be an object";
    return false;
  }

  out->id = dict->GetString("id").ToString();
  out->method = dict->GetString("method").ToString();
  CefRefPtr<CefDictionaryValue> params = dict->GetDictionary("params");
  out->params = params ? params->Copy(false) : nullptr;
  if (out->id.empty() || out->id.size() > 80) {
    if (error) *error = "RPC id must be 1-80 characters";
    return false;
  }
  if (out->method.empty() || out->method.size() > 96) {
    if (error) *error = "RPC method must be 1-96 characters";
    return false;
  }
  return true;
}

void NativeRpcSuccessRaw(
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request,
    const std::string& result_json) {
  callback->Success(JsonObjectBuilder()
                        .AddString("id", request.id)
                        .AddBool("ok", true)
                        .AddRaw("result", result_json.empty() ? "null" : result_json)
                        .Build());
}

void NativeRpcSuccessString(
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request,
    const std::string& result) {
  NativeRpcSuccessRaw(callback, request, JsonString(result));
}

void NativeRpcFailure(
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request,
    const std::string& code,
    const std::string& message) {
  callback->Success(JsonObjectBuilder()
                        .AddString("id", request.id)
                        .AddBool("ok", false)
                        .AddRaw("error", ErrorJson(code, message))
                        .Build());
}

}  // namespace otf
