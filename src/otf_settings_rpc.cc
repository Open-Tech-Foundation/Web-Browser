#include "otf_settings_rpc.h"

#include <set>
#include <string>

#include "include/cef_parser.h"
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

bool IsNumberType(cef_value_type_t type) {
  return type == VTYPE_INT || type == VTYPE_DOUBLE;
}

bool ValidateCustomSearchEngines(CefRefPtr<CefListValue> list,
                                 std::string* error) {
  if (!list) {
    if (error) *error = "customSearchEngines must be an array";
    return false;
  }
  for (size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_DICTIONARY) {
      if (error) *error = "customSearchEngines entries must be objects";
      return false;
    }
    CefRefPtr<CefDictionaryValue> entry = list->GetDictionary(i);
    if (!HasOnlyParamKeys(entry, {"id", "name", "url"}, error)) {
      if (error && error->rfind("unexpected param:", 0) == 0) {
        *error = "customSearchEngines entry " + *error;
      }
      return false;
    }
    if (!entry->HasKey("id") || entry->GetType("id") != VTYPE_STRING ||
        !entry->HasKey("name") || entry->GetType("name") != VTYPE_STRING ||
        !entry->HasKey("url") || entry->GetType("url") != VTYPE_STRING) {
      if (error) *error = "customSearchEngines entries require string id, name, and url";
      return false;
    }
  }
  return true;
}

bool ValidateStartupUrls(CefRefPtr<CefListValue> list, std::string* error) {
  if (!list) {
    if (error) *error = "startupUrls must be an array";
    return false;
  }
  for (size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_STRING) {
      if (error) *error = "startupUrls must contain only strings";
      return false;
    }
  }
  return true;
}

bool ValidateSettingsParams(CefRefPtr<CefDictionaryValue> params,
                            std::string* error) {
  if (!params ||
      !HasOnlyParamKeys(params,
                        {"searchEngine", "historyEnabled", "downloadsEnabled",
                         "startupBehavior", "startupUrls", "httpsOnly",
                         "blockInsecure", "appearanceMode",
                         "customSearchEngines", "cacheDir", "downloadDir",
                         "windowX", "windowY", "windowWidth", "windowHeight",
                         "windowMaximized"},
                        error)) {
    return false;
  }

  CefDictionaryValue::KeyList keys;
  params->GetKeys(keys);
  for (const auto& key : keys) {
    const std::string k = key.ToString();
    const cef_value_type_t type = params->GetType(k);
    if (k == "searchEngine") {
      if (type != VTYPE_NULL && type != VTYPE_STRING) {
        if (error) *error = "searchEngine must be null or a string";
        return false;
      }
    } else if (k == "historyEnabled" || k == "downloadsEnabled" ||
               k == "httpsOnly" || k == "blockInsecure" ||
               k == "windowMaximized") {
      if (type != VTYPE_BOOL) {
        if (error) *error = k + " must be boolean";
        return false;
      }
    } else if (k == "startupBehavior" || k == "appearanceMode" ||
               k == "cacheDir" || k == "downloadDir") {
      if (type != VTYPE_STRING) {
        if (error) *error = k + " must be a string";
        return false;
      }
    } else if (k == "startupUrls") {
      if (type != VTYPE_LIST ||
          !ValidateStartupUrls(params->GetList(k), error)) {
        return false;
      }
    } else if (k == "customSearchEngines") {
      if (type != VTYPE_LIST ||
          !ValidateCustomSearchEngines(params->GetList(k), error)) {
        return false;
      }
    } else if (k == "windowX" || k == "windowY" ||
               k == "windowWidth" || k == "windowHeight") {
      if (!IsNumberType(type)) {
        if (error) *error = k + " must be a number";
        return false;
      }
    }
  }
  return true;
}

bool RequireNoParams(const NativeRpcRequest& request, std::string* error) {
  return request.params && HasOnlyParamKeys(request.params, {}, error);
}

std::string ParamsToJson(CefRefPtr<CefDictionaryValue> params) {
  CefRefPtr<CefValue> root = CefValue::Create();
  root->SetDictionary(params->Copy(false));
  return CefWriteJSON(root, JSON_WRITER_DEFAULT).ToString();
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

bool HandleSettingsRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler ||
      (request.method != "settings.get" && request.method != "settings.set")) {
    return false;
  }

  std::string error;
  if (request.method == "settings.get") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    SuccessRaw(callback, request,
               handler->guest_session_active_ ? "{}" : otf::LoadSettingsJson());
    return true;
  }

  if (handler->guest_session_active_) {
    Failure(callback, request, "guest_session",
            "Settings are disabled in guest sessions");
    return true;
  }
  if (!ValidateSettingsParams(request.params, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  std::string normalized_json;
  if (!otf::SaveSettingsJson(ParamsToJson(request.params), &normalized_json)) {
    Failure(callback, request, "invalid_settings", "Invalid settings payload");
    return true;
  }

  handler->SendEvent(JsonObjectBuilder()
                         .AddString("key", "settings-changed")
                         .AddRaw("settings", normalized_json)
                         .Build());
  SuccessRaw(callback, request, normalized_json);
  return true;
}

}  // namespace otf
