#include "otf_settings_rpc.h"

#include <set>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <memory>

#include "include/base/cef_callback.h"
#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/cef_version.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
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

struct ResetRequest {
  bool startup = true;
  bool search_engine = true;
  bool cookies = true;
  bool cache = true;
  bool ssl = true;
  bool service_workers = true;
  bool permissions = true;
  bool storage = true;
  bool bookmarks = false;
  bool history = false;
  bool downloads = false;
  bool passwords = false;
};

bool ParseResetRequest(const NativeRpcRequest& request,
                       ResetRequest* reset,
                       std::string* error) {
  if (!reset ||
      !request.params ||
      !HasOnlyParamKeys(request.params,
                        {"startup", "searchEngine", "cookies", "cache", "ssl",
                         "serviceWorkers", "permissions", "storage",
                         "bookmarks", "history", "downloads", "passwords"},
                        error)) {
    return false;
  }

  const std::pair<const char*, bool ResetRequest::*> keys[] = {
      {"startup", &ResetRequest::startup},
      {"searchEngine", &ResetRequest::search_engine},
      {"cookies", &ResetRequest::cookies},
      {"cache", &ResetRequest::cache},
      {"ssl", &ResetRequest::ssl},
      {"serviceWorkers", &ResetRequest::service_workers},
      {"permissions", &ResetRequest::permissions},
      {"storage", &ResetRequest::storage},
      {"bookmarks", &ResetRequest::bookmarks},
      {"history", &ResetRequest::history},
      {"downloads", &ResetRequest::downloads},
      {"passwords", &ResetRequest::passwords},
  };

  for (const auto& [key, field] : keys) {
    if (!request.params->HasKey(key)) continue;
    if (request.params->GetType(key) != VTYPE_BOOL) {
      if (error) *error = std::string(key) + " must be boolean";
      return false;
    }
    reset->*field = request.params->GetBool(key);
  }
  return true;
}

std::string BuildJsonStringArray(const std::vector<std::string>& values) {
  std::string json = "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) json += ",";
    json += JsonString(values[i]);
  }
  json += "]";
  return json;
}

class ResetCompletionState : public CefBaseRefCounted {
 public:
  ResetCompletionState(CefRefPtr<Callback> callback, NativeRpcRequest request)
      : callback_(callback), request_(std::move(request)) {}

  void AddPending(const std::string& name) {
    (void)name;
    ++pending_ops_;
  }

  void MarkCompleted(const std::string& name) { completed_.push_back(name); }
  void MarkUnsupported(const std::string& name) { unsupported_.push_back(name); }
  void MarkFailed(const std::string& name) { failed_.push_back(name); }

  void FinishOp() {
    if (pending_ops_ > 0) --pending_ops_;
    MaybeFinalize();
  }

  void MaybeFinalize() {
    if (finished_ || pending_ops_ > 0 || !callback_) return;
    finished_ = true;
    const std::string result = JsonObjectBuilder()
        .AddBool("ok", failed_.empty())
        .AddRaw("completed", BuildJsonStringArray(completed_))
        .AddRaw("pendingRestart", "[]")
        .AddRaw("unsupported", BuildJsonStringArray(unsupported_))
        .AddRaw("failed", BuildJsonStringArray(failed_))
        .AddBool("requiresRestart", false)
        .Build();
    NativeRpcSuccessRaw(callback_, request_, result);
  }

 private:
  CefRefPtr<Callback> callback_;
  NativeRpcRequest request_;
  int pending_ops_ = 0;
  bool finished_ = false;
  std::vector<std::string> completed_;
  std::vector<std::string> unsupported_;
  std::vector<std::string> failed_;

  IMPLEMENT_REFCOUNTING(ResetCompletionState);
};

class ResetAsyncCallback : public CefCompletionCallback,
                           public CefDeleteCookiesCallback {
 public:
  ResetAsyncCallback(CefRefPtr<ResetCompletionState> state, std::string name)
      : state_(state), name_(std::move(name)) {}

  void OnComplete() override {
    if (!state_) return;
    state_->MarkCompleted(name_);
    state_->FinishOp();
  }

  void OnComplete(int num_deleted) override {
    (void)num_deleted;
    if (!state_) return;
    state_->MarkCompleted(name_);
    state_->FinishOp();
  }

 private:
  CefRefPtr<ResetCompletionState> state_;
  std::string name_;

  IMPLEMENT_REFCOUNTING(ResetAsyncCallback);
};

std::string BuildResetSettingsJson(const std::string& current_settings_json) {
  bool history_enabled = false;
  bool downloads_enabled = false;
  bool https_only = false;
  bool block_insecure = true;
  std::string appearance_mode = "auto";
  std::vector<CustomSearchEngine> custom_engines;

  CefRefPtr<CefValue> root =
      CefParseJSON(current_settings_json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (root && root->GetType() == VTYPE_DICTIONARY) {
    CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
    if (dict) {
      if (dict->HasKey("historyEnabled") &&
          dict->GetType("historyEnabled") == VTYPE_BOOL) {
        history_enabled = dict->GetBool("historyEnabled");
      }
      if (dict->HasKey("downloadsEnabled") &&
          dict->GetType("downloadsEnabled") == VTYPE_BOOL) {
        downloads_enabled = dict->GetBool("downloadsEnabled");
      }
      if (dict->HasKey("httpsOnly") &&
          dict->GetType("httpsOnly") == VTYPE_BOOL) {
        https_only = dict->GetBool("httpsOnly");
      }
      if (dict->HasKey("blockInsecure") &&
          dict->GetType("blockInsecure") == VTYPE_BOOL) {
        block_insecure = dict->GetBool("blockInsecure");
      }
      if (dict->HasKey("appearanceMode") &&
          dict->GetType("appearanceMode") == VTYPE_STRING) {
        appearance_mode = dict->GetString("appearanceMode");
        if (appearance_mode != "auto" && appearance_mode != "light" &&
            appearance_mode != "dark") {
          appearance_mode = "auto";
        }
      }
      if (dict->HasKey("customSearchEngines") &&
          dict->GetType("customSearchEngines") == VTYPE_LIST) {
        CefRefPtr<CefListValue> list = dict->GetList("customSearchEngines");
        for (size_t i = 0; i < list->GetSize(); ++i) {
          if (list->GetType(i) != VTYPE_DICTIONARY) continue;
          CefRefPtr<CefDictionaryValue> entry = list->GetDictionary(i);
          if (!entry->HasKey("id") || entry->GetType("id") != VTYPE_STRING) continue;
          if (!entry->HasKey("name") || entry->GetType("name") != VTYPE_STRING) continue;
          if (!entry->HasKey("url") || entry->GetType("url") != VTYPE_STRING) continue;
          std::string id = entry->GetString("id");
          std::string name = entry->GetString("name");
          std::string url = entry->GetString("url");
          if (id.empty() || name.empty() || url.empty()) continue;
          custom_engines.push_back({std::move(id), std::move(name), std::move(url)});
        }
      }
    }
  }

  return JsonObjectBuilder()
      .AddNull("searchEngine")
      .AddBool("historyEnabled", history_enabled)
      .AddBool("downloadsEnabled", downloads_enabled)
      .AddString("startupBehavior", "newtab")
      .AddRaw("startupUrls", "[]")
      .AddBool("httpsOnly", https_only)
      .AddBool("blockInsecure", block_insecure)
      .AddString("appearanceMode", appearance_mode)
      .AddRaw("customSearchEngines",
              otf::BuildCustomEnginesJson(custom_engines))
      .Build();
}

bool ResetBrowserData(OtfHandler* handler,
                      CefRefPtr<CefBrowser> browser,
                      CefRefPtr<Callback> callback,
                      const NativeRpcRequest& request) {
  ResetRequest reset_request;
  std::string error;
  if (!ParseResetRequest(request, &reset_request, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  CefRefPtr<ResetCompletionState> state =
      new ResetCompletionState(callback, request);

  const std::string current_settings = otf::LoadSettingsJson();
  const std::string reset_settings = BuildResetSettingsJson(current_settings);
  std::string normalized_settings;
  if (otf::SaveSettingsJson(reset_settings, &normalized_settings)) {
    state->MarkCompleted("settings");
    handler->SendEvent(JsonObjectBuilder()
                           .AddString("key", "settings-changed")
                           .AddRaw("settings", normalized_settings)
                           .Build());
  } else {
    state->MarkFailed("settings");
  }

  if (reset_request.history) {
    if (handler->store_ && handler->store_->ClearHistory()) {
      state->MarkCompleted("history");
      if (handler->tab_manager_) {
        for (int tab_id : handler->tab_manager_->GetAllTabIds()) {
          const std::string url = handler->tab_manager_->GetUrl(tab_id);
          if (otf::IsPersistableWebUrl(url) && !otf::IsInternalUiUrl(url)) {
            handler->tab_manager_->SetHistorySuppressedUrl(tab_id, url);
          }
        }
      }
    } else {
      state->MarkFailed("history");
    }
  }

  if (reset_request.bookmarks) {
    if (handler->store_ && handler->store_->ClearBookmarks()) {
      state->MarkCompleted("bookmarks");
      if (handler->tab_manager_) {
        for (int tab_id : handler->tab_manager_->GetAllTabIds()) {
          handler->NotifyBookmarkStateForTab(tab_id);
        }
      } else {
        handler->SendEvent(JsonObjectBuilder()
                               .AddString("key", "bookmarks-changed")
                               .AddBool("bookmarked", false)
                               .Build());
      }
    } else {
      state->MarkFailed("bookmarks");
    }
  }

  if (reset_request.downloads) {
    if (handler->store_ && handler->store_->ClearDownloads()) {
      for (const auto& entry : handler->download_callbacks_) {
        if (entry.second) entry.second->Cancel();
      }
      handler->downloads_.clear();
      handler->download_callbacks_.clear();
      handler->runtime_download_ids_.clear();
      handler->NotifyDownloadsChanged();
      handler->NotifyDownloadBadge();
      state->MarkCompleted("downloads");
    } else {
      state->MarkFailed("downloads");
    }
  }

  if (reset_request.cookies) {
    CefRefPtr<CefCookieManager> cookie_manager =
        CefCookieManager::GetGlobalManager(nullptr);
    if (cookie_manager) {
      state->AddPending("cookies");
      cookie_manager->DeleteCookies(
          "", "", new ResetAsyncCallback(state, "cookies"));
    } else {
      state->MarkFailed("cookies");
    }
  }

  CefRefPtr<CefRequestContext> request_context =
      browser ? browser->GetHost()->GetRequestContext()
              : CefRequestContext::GetGlobalContext();
  if (reset_request.cache && request_context) {
#if CEF_API_ADDED(14400)
    state->AddPending("cache");
    request_context->ClearHttpCache(new ResetAsyncCallback(state, "cache"));
#else
    state->MarkUnsupported("cache");
#endif
  } else if (reset_request.cache) {
    state->MarkFailed("cache");
  }

  if (reset_request.ssl && request_context) {
    state->AddPending("ssl-exceptions");
    request_context->ClearCertificateExceptions(
        new ResetAsyncCallback(state, "ssl-exceptions"));
    state->AddPending("http-auth");
    request_context->ClearHttpAuthCredentials(
        new ResetAsyncCallback(state, "http-auth"));
    state->AddPending("connections");
    request_context->CloseAllConnections(
        new ResetAsyncCallback(state, "connections"));
  } else if (reset_request.ssl) {
    state->MarkFailed("ssl");
  }

  if (reset_request.service_workers) {
    state->MarkUnsupported("serviceWorkers");
  }
  if (reset_request.permissions) {
    if (handler->store_) {
      handler->store_->ClearAllSitePermissions();
      state->MarkCompleted("permissions");
    } else {
      state->MarkFailed("permissions");
    }
  }
  if (reset_request.storage) {
    state->MarkUnsupported("storage");
  }
  if (reset_request.passwords) {
    state->MarkUnsupported("passwords");
  }

  state->MaybeFinalize();
  return true;
}

uint64_t ReadUsageBytes(CefRefPtr<CefValue> usage_value) {
  if (!usage_value) return 0;
  if (usage_value->GetType() == VTYPE_DOUBLE) {
    const double d = usage_value->GetDouble();
    return d > 0 ? static_cast<uint64_t>(d) : 0;
  }
  if (usage_value->GetType() == VTYPE_INT) {
    const int v = usage_value->GetInt();
    return v > 0 ? static_cast<uint64_t>(v) : 0;
  }
  return 0;
}

void QuerySiteUsageOverCdp(OtfHandler* handler,
                           CefRefPtr<CefBrowser> ui_browser,
                           CefRefPtr<Callback> callback,
                           const NativeRpcRequest& request,
                           std::vector<std::string> extra_origins,
                           std::map<std::string, uint64_t> cookie_sizes,
                           std::map<std::string, uint64_t> cookie_counts) {
  struct Aggregation {
    CefRefPtr<Callback> callback;
    NativeRpcRequest request;
    std::vector<std::string> extra_origins;
    std::map<std::string, uint64_t> cookie_sizes;
    std::map<std::string, uint64_t> cookie_counts;
    std::map<std::string, uint64_t> local_storage;
    std::map<std::string, uint64_t> indexed_db;
    std::map<std::string, uint64_t> cache_storage;
    int pending = 0;
    bool resolved = false;

    void Resolve() {
      if (resolved) return;
      resolved = true;
      NativeRpcSuccessRaw(callback, request,
                          otf::BuildSiteUsageJson(
                              extra_origins, cookie_sizes, cookie_counts,
                              local_storage, indexed_db, cache_storage));
    }
  };

  std::set<std::string> origins(extra_origins.begin(), extra_origins.end());
  for (const auto& [origin, size] : cookie_sizes) {
    (void)size;
    origins.insert(origin);
  }

  auto agg = std::make_shared<Aggregation>();
  agg->callback = callback;
  agg->request = request;
  agg->extra_origins = std::move(extra_origins);
  agg->cookie_sizes = std::move(cookie_sizes);
  agg->cookie_counts = std::move(cookie_counts);

  if (!handler || !handler->devtools_bridge_ || !ui_browser ||
      origins.empty()) {
    agg->Resolve();
    return;
  }
  handler->devtools_bridge_->Attach(ui_browser);

  for (const auto& origin : origins) {
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
    params->SetString("origin", origin);
    const int message_id = handler->devtools_bridge_->Execute(
        "Storage.getUsageAndQuota", params,
        [agg, origin](bool ok, const std::string& result_json) {
          --agg->pending;
          if (ok) {
            CefRefPtr<CefValue> parsed =
                CefParseJSON(result_json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
            CefRefPtr<CefDictionaryValue> dict =
                (parsed && parsed->GetType() == VTYPE_DICTIONARY)
                    ? parsed->GetDictionary()
                    : nullptr;
            CefRefPtr<CefListValue> breakdown =
                (dict && dict->HasKey("usageBreakdown"))
                    ? dict->GetList("usageBreakdown")
                    : nullptr;
            if (breakdown) {
              for (size_t i = 0; i < breakdown->GetSize(); ++i) {
                CefRefPtr<CefDictionaryValue> entry =
                    breakdown->GetDictionary(i);
                if (!entry) continue;
                const std::string type =
                    entry->GetString("storageType").ToString();
                const uint64_t usage = ReadUsageBytes(entry->GetValue("usage"));
                if (usage == 0) continue;
                if (type == "indexeddb") {
                  agg->indexed_db[origin] += usage;
                } else if (type == "cache_storage") {
                  agg->cache_storage[origin] += usage;
                } else if (type == "local_storage") {
                  agg->local_storage[origin] += usage;
                }
              }
            }
          }
          if (agg->pending == 0) agg->Resolve();
        });
    if (message_id != 0) ++agg->pending;
  }

  if (agg->pending == 0) {
    agg->Resolve();
    return;
  }

  CefPostDelayedTask(
      TID_UI,
      base::BindOnce([](std::shared_ptr<Aggregation> a) { a->Resolve(); },
                     agg),
      5000);
}

bool GetSiteUsageList(OtfHandler* handler,
                      CefRefPtr<CefBrowser> browser,
                      CefRefPtr<Callback> callback,
                      const NativeRpcRequest& request) {
  std::string error;
  if (!RequireNoParams(request, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  std::vector<std::string> history_origins;
  if (handler->store_) {
    history_origins = handler->store_->GetDistinctOrigins();
  }

  CefRefPtr<CefCookieManager> mgr = CefCookieManager::GetGlobalManager(nullptr);
  if (!mgr) {
    QuerySiteUsageOverCdp(handler, browser, callback, request,
                          std::move(history_origins), {}, {});
    return true;
  }

  class CookieSizeVisitor : public CefCookieVisitor {
   public:
    CookieSizeVisitor(OtfHandler* handler,
                      CefRefPtr<CefBrowser> ui_browser,
                      CefRefPtr<Callback> callback,
                      NativeRpcRequest request,
                      std::vector<std::string> extra)
        : handler_(handler),
          ui_browser_(ui_browser),
          callback_(callback),
          request_(std::move(request)),
          extra_origins_(std::move(extra)) {}

    ~CookieSizeVisitor() override {
      QuerySiteUsageOverCdp(handler_, ui_browser_, callback_, request_,
                            std::move(extra_origins_),
                            std::move(cookie_sizes_),
                            std::move(cookie_counts_));
    }

    bool Visit(const CefCookie& cookie,
               int count,
               int total,
               bool& delete_cookie) override {
      delete_cookie = false;
      std::string domain = CefString(&cookie.domain).ToString();
      if (!domain.empty() && domain[0] == '.') domain = domain.substr(1);
      if (domain.empty()) return true;
      const std::string origin =
          cookie.secure ? "https://" + domain : "http://" + domain;
      const uint64_t size = CefString(&cookie.name).length() +
                            CefString(&cookie.value).length() +
                            CefString(&cookie.domain).length() +
                            CefString(&cookie.path).length() + 50;
      cookie_sizes_[origin] += size;
      cookie_counts_[origin] += 1;
      return true;
    }

   private:
    OtfHandler* handler_;
    CefRefPtr<CefBrowser> ui_browser_;
    CefRefPtr<Callback> callback_;
    NativeRpcRequest request_;
    std::vector<std::string> extra_origins_;
    std::map<std::string, uint64_t> cookie_sizes_;
    std::map<std::string, uint64_t> cookie_counts_;

    IMPLEMENT_REFCOUNTING(CookieSizeVisitor);
  };

  mgr->VisitAllCookies(new CookieSizeVisitor(
      handler, browser, callback, request, std::move(history_origins)));
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

  if (request.method == "settings.siteUsageList") {
    return GetSiteUsageList(handler, browser, callback, request);
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

  if (request.method == "settings.resetBrowserData") {
    if (handler->guest_session_active_) {
      Failure(callback, request, "guest_session",
              "Resetting browser data is disabled in guest sessions");
      return true;
    }
    return ResetBrowserData(handler, browser, callback, request);
  }

  if (request.method == "settings.restart") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!handler->RestartBrowser()) {
      Failure(callback, request, "restart_failed", "Unable to restart browser");
      return true;
    }
    NativeRpcSuccessString(callback, request, "ok");
    handler->CloseAllBrowsers(false);
    return true;
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
