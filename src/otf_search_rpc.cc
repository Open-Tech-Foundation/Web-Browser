#include "otf_search_rpc.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "otf_handler.h"
#include "otf_store.h"
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

bool RequireStringParam(CefRefPtr<CefDictionaryValue> params,
                        const std::string& key,
                        std::string* value,
                        std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_STRING) {
    if (error) *error = key + " must be a string";
    return false;
  }
  if (value) *value = params->GetString(key).ToString();
  return true;
}

bool ReadLimit(CefRefPtr<CefDictionaryValue> params,
               int default_limit,
               int* limit,
               std::string* error) {
  if (!params->HasKey("limit")) {
    if (limit) *limit = default_limit;
    return true;
  }
  if (params->GetType("limit") != VTYPE_INT) {
    if (error) *error = "limit must be an integer";
    return false;
  }
  const int raw_limit = params->GetInt("limit");
  if (raw_limit < 1 || raw_limit > 50) {
    if (error) *error = "limit must be between 1 and 50";
    return false;
  }
  if (limit) *limit = raw_limit;
  return true;
}

std::string StringListJson(const std::vector<std::string>& values) {
  std::string json = "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) json += ",";
    json += JsonString(values[i]);
  }
  json += "]";
  return json;
}

void SuccessRaw(CefRefPtr<Callback> callback,
                const NativeRpcRequest& request,
                const std::string& result_json) {
  NativeRpcSuccessRaw(callback, request, result_json);
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

}  // namespace

bool HandleSearchRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler ||
      (request.method != "search.history.add" &&
       request.method != "search.suggestions")) {
    return false;
  }

  std::string error;
  if (request.method == "search.history.add") {
    if (!request.params ||
        !HasOnlyParamKeys(request.params, {"query"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    std::string query;
    if (!RequireStringParam(request.params, "query", &query, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!handler->IsGuestSessionActive() && handler->GetStore() &&
        !query.empty()) {
      handler->GetStore()->AddSearchHistory(query);
    }
    SuccessRaw(callback, request, "null");
    return true;
  }

  if (!request.params ||
      !HasOnlyParamKeys(request.params, {"prefix", "limit"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  std::string prefix;
  if (!RequireStringParam(request.params, "prefix", &prefix, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int limit = 10;
  if (!ReadLimit(request.params, 10, &limit, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  if (handler->IsGuestSessionActive() || !handler->GetStore() ||
      prefix.empty()) {
    SuccessRaw(callback, request, "[]");
    return true;
  }

  const auto suggestions = handler->GetStore()->GetSearchSuggestions(
      prefix, std::clamp(limit, 1, 50));
  SuccessRaw(callback, request, StringListJson(suggestions));
  return true;
}

}  // namespace otf
