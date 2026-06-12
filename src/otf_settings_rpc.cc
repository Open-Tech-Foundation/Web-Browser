#include "otf_settings_rpc.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_parser.h"
#include "include/cef_version.h"
#include "include/cef_values.h"
#include "otf_handler.h"
#include "otf_utils.h"

#ifndef OTF_VERSION
#define OTF_VERSION "0.0.0-unknown"
#endif

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

std::string BuildVersionInfoJson() {
  const std::string chromium = std::to_string(CHROME_VERSION_MAJOR) + "." +
                               std::to_string(CHROME_VERSION_MINOR) + "." +
                               std::to_string(CHROME_VERSION_BUILD) + "." +
                               std::to_string(CHROME_VERSION_PATCH);
  return JsonObjectBuilder()
      .AddString("browser", OTF_VERSION)
      .AddString("chromium", chromium)
      .AddString("cef", CEF_VERSION)
      .Build();
}

std::string BuildStoragePathsJson() {
  JsonObjectBuilder b;
  b.AddString("activeDataDir", otf::PathToUtf8(otf::GetActiveDataDir()));
  b.AddString("activeCacheDir", otf::PathToUtf8(otf::GetActiveCacheDir()));
  b.AddString("activeDownloadsDir",
              otf::PathToUtf8(otf::GetActiveDownloadsDir()));

  const auto configured_cache = otf::GetConfiguredCacheDir();
  const auto configured_downloads = otf::GetConfiguredDownloadsDir();
  if (!configured_cache.empty()) {
    b.AddString("configuredCacheDir", otf::PathToUtf8(configured_cache));
  }
  if (!configured_downloads.empty()) {
    b.AddString("configuredDownloadsDir",
                otf::PathToUtf8(configured_downloads));
  }

  const std::string pending_raw = otf::LoadPendingPathsJson();
  if (pending_raw != "{}") {
    CefRefPtr<CefValue> pending_parsed =
        CefParseJSON(pending_raw, JSON_PARSER_ALLOW_TRAILING_COMMAS);
    if (pending_parsed && pending_parsed->GetType() == VTYPE_DICTIONARY) {
      CefRefPtr<CefDictionaryValue> pdict = pending_parsed->GetDictionary();
      if (pdict->HasKey("cacheDir")) {
        b.AddString("pendingCacheDir", pdict->GetString("cacheDir").ToString());
      }
      if (pdict->HasKey("downloadDir")) {
        b.AddString("pendingDownloadsDir",
                    pdict->GetString("downloadDir").ToString());
      }
    }
  }

  b.AddString("defaultCacheDir", otf::PathToUtf8(otf::GetAppCacheDir()));
  b.AddString("defaultDownloadsDir", otf::PathToUtf8(otf::GetDownloadsDir()));

  const auto active_cache = otf::GetActiveCacheDir();
  const auto active_downloads = otf::GetActiveDownloadsDir();
  b.AddRaw("cacheSize", std::to_string(otf::GetDirectorySize(active_cache)));
  b.AddRaw("downloadsSize",
           std::to_string(otf::GetDirectorySize(active_downloads)));
  return b.Build();
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

class RpcFolderPickerCallback : public CefRunFileDialogCallback {
 public:
  RpcFolderPickerCallback(CefRefPtr<Callback> callback,
                          NativeRpcRequest request)
      : callback_(callback), request_(std::move(request)) {}

  void OnFileDialogDismissed(
      const std::vector<CefString>& file_paths) override {
    if (!file_paths.empty()) {
      NativeRpcSuccessString(callback_, request_, file_paths[0].ToString());
      return;
    }
    NativeRpcSuccessString(callback_, request_, "cancelled");
  }

 private:
  CefRefPtr<Callback> callback_;
  NativeRpcRequest request_;
  IMPLEMENT_REFCOUNTING(RpcFolderPickerCallback);
};

bool SaveStoragePath(OtfHandler* handler,
                     CefRefPtr<Callback> callback,
                     const NativeRpcRequest& request) {
  std::string error;
  if (!request.params ||
      !HasOnlyParamKeys(request.params, {"purpose", "path"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  if (request.params->GetType("purpose") != VTYPE_STRING ||
      request.params->GetType("path") != VTYPE_STRING) {
    Failure(callback, request, "invalid_params",
            "purpose and path must be strings");
    return true;
  }

  const std::string purpose = request.params->GetString("purpose").ToString();
  const std::string path = request.params->GetString("path").ToString();
  if ((purpose != "cache" && purpose != "downloads") || path.empty()) {
    Failure(callback, request, "invalid_params",
            "purpose must be cache or downloads and path must be non-empty");
    return true;
  }

  error = otf::ValidateStoragePath(path, purpose);
  if (!error.empty()) {
    Failure(callback, request, "invalid_storage_path", error);
    return true;
  }

  std::string settings_raw = otf::LoadSettingsJson();
  CefRefPtr<CefValue> settings_root =
      CefParseJSON(settings_raw, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!settings_root || settings_root->GetType() != VTYPE_DICTIONARY) {
    Failure(callback, request, "settings_unavailable",
            "Failed to read settings");
    return true;
  }

  CefRefPtr<CefDictionaryValue> settings_dict = settings_root->GetDictionary();
  const std::string key = (purpose == "cache") ? "cacheDir" : "downloadDir";
  settings_dict->SetString(key, path);

  CefRefPtr<CefValue> out_root = CefValue::Create();
  out_root->SetDictionary(settings_dict);
  CefString out_json = CefWriteJSON(out_root, JSON_WRITER_DEFAULT);
  if (out_json.empty()) {
    Failure(callback, request, "settings_serialize_failed",
            "Failed to serialize settings");
    return true;
  }

  std::string normalized;
  if (!otf::NormalizeSettingsJson(out_json.ToString(), &normalized) ||
      !otf::SaveSettingsJson(normalized, nullptr)) {
    Failure(callback, request, "settings_save_failed",
            "Failed to save settings");
    return true;
  }

  std::string pending = otf::LoadPendingPathsJson();
  if (pending == "{}") {
    pending = "{\"" + key + "\":\"" + otf::JsonEscape(path) + "\"}";
  } else {
    CefRefPtr<CefValue> pending_root =
        CefParseJSON(pending, JSON_PARSER_ALLOW_TRAILING_COMMAS);
    if (pending_root && pending_root->GetType() == VTYPE_DICTIONARY) {
      CefRefPtr<CefDictionaryValue> pending_dict =
          pending_root->GetDictionary();
      pending_dict->SetString(key, path);
      CefRefPtr<CefValue> pending_out = CefValue::Create();
      pending_out->SetDictionary(pending_dict);
      CefString pending_json = CefWriteJSON(pending_out, JSON_WRITER_DEFAULT);
      if (!pending_json.empty()) {
        pending = pending_json.ToString();
      }
    }
  }
  otf::SavePendingPathsJson(pending);

  handler->SendEvent(JsonObjectBuilder()
                         .AddString("key", "settings-changed")
                         .AddRaw("settings", normalized)
                         .Build());
  SuccessRaw(callback, request, normalized);
  return true;
}

}  // namespace

bool HandleSettingsRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  if (!handler || request.method.rfind("settings.", 0) != 0) {
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

  if (request.method == "settings.versionInfo") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    SuccessRaw(callback, request, BuildVersionInfoJson());
    return true;
  }

  if (request.method == "settings.storagePaths") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    SuccessRaw(callback, request, BuildStoragePathsJson());
    return true;
  }

  if (request.method == "settings.storageTotals") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    SuccessRaw(callback, request, otf::BuildStorageTotalsJson());
    return true;
  }

  if (request.method == "settings.selectFolder") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!browser) {
      Failure(callback, request, "browser_unavailable",
              "No browser available for folder selection");
      return true;
    }
    browser->GetHost()->RunFileDialog(
        FILE_DIALOG_OPEN_FOLDER, "Select Directory", "",
        std::vector<CefString>(),
        new RpcFolderPickerCallback(callback, request));
    return true;
  }

  if (request.method == "settings.setStoragePath") {
    if (handler->guest_session_active_) {
      Failure(callback, request, "guest_session",
              "Storage paths cannot be changed in guest sessions");
      return true;
    }
    return SaveStoragePath(handler, callback, request);
  }

  if (request.method != "settings.set") {
    return false;
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
