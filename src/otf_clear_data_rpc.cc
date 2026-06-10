#include "otf_clear_data_rpc.h"

#include <algorithm>
#include <ctime>
#include <set>
#include <string>
#include <vector>

#include "include/cef_cookie.h"
#include "include/cef_request_context.h"
#include "include/cef_values.h"
#include "include/internal/cef_time.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {
namespace {

using Callback = CefMessageRouterBrowserSide::Handler::Callback;

constexpr const char* kCategoriesKey = "categories";
constexpr const char* kTimeRangeKey = "timeRange";

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

bool IsValidCategory(const std::string& category) {
  return category == "history" || category == "downloads" ||
         category == "cookies" || category == "cache" ||
         category == "siteData";
}

bool IsValidTimeRange(const std::string& range) {
  return range == "all" || range == "hour" || range == "day" ||
         range == "week" || range == "month";
}

bool ParseClearRequest(const NativeRpcRequest& request,
                       std::vector<std::string>* categories,
                       std::string* time_range,
                       std::string* error) {
  if (!request.params ||
      !HasOnlyParamKeys(request.params, {kCategoriesKey, kTimeRangeKey}, error)) {
    return false;
  }
  if (!request.params->HasKey(kCategoriesKey) ||
      request.params->GetType(kCategoriesKey) != VTYPE_LIST) {
    if (error) *error = "categories must be an array";
    return false;
  }
  if (!request.params->HasKey(kTimeRangeKey) ||
      request.params->GetType(kTimeRangeKey) != VTYPE_STRING) {
    if (error) *error = "timeRange must be a string";
    return false;
  }
  *time_range = request.params->GetString(kTimeRangeKey).ToString();
  if (!IsValidTimeRange(*time_range)) {
    if (error) *error = "invalid timeRange";
    return false;
  }
  CefRefPtr<CefListValue> list = request.params->GetList(kCategoriesKey);
  if (!list || list->GetSize() == 0) {
    if (error) *error = "categories must not be empty";
    return false;
  }
  std::set<std::string> seen;
  for (size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_STRING) {
      if (error) *error = "categories must contain only strings";
      return false;
    }
    std::string category = list->GetString(i).ToString();
    if (!IsValidCategory(category)) {
      if (error) *error = "invalid category: " + category;
      return false;
    }
    if (seen.insert(category).second) categories->push_back(category);
  }
  return true;
}

bool Contains(const std::vector<std::string>& categories,
              const std::string& category) {
  return std::find(categories.begin(), categories.end(), category) !=
         categories.end();
}

int64_t CutoffForRange(const std::string& time_range) {
  if (time_range == "all") return 0;
  const int64_t now = std::time(nullptr);
  if (time_range == "hour") return now - 3600;
  if (time_range == "day") return now - 86400;
  if (time_range == "week") return now - 604800;
  if (time_range == "month") return now - 2592000;
  return 0;
}

int64_t CefBaseTimeToUnixSeconds(cef_basetime_t base_time) {
  cef_time_t cef_time{};
  time_t out = 0;
  if (cef_time_from_basetime(base_time, &cef_time) &&
      cef_time_to_timet(&cef_time, &out)) {
    return static_cast<int64_t>(out);
  }
  return 0;
}

void Success(CefRefPtr<Callback> callback, const NativeRpcRequest& request) {
  NativeRpcSuccessString(callback, request, "ok");
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

void ClearCookieData(CefRefPtr<CefBrowser> browser, int64_t cutoff) {
  CefRefPtr<CefRequestContext> req_ctx =
      browser ? browser->GetHost()->GetRequestContext() : nullptr;
  CefRefPtr<CefCookieManager> mgr =
      req_ctx ? req_ctx->GetCookieManager(nullptr) : nullptr;
  if (!mgr) return;
  if (cutoff > 0) {
    class TimeRangeCookieDeleter : public CefCookieVisitor {
     public:
      explicit TimeRangeCookieDeleter(int64_t cutoff) : cutoff_(cutoff) {}
      bool Visit(const CefCookie& cookie, int count, int total,
                 bool& delete_cookie) override {
        int64_t created_at = CefBaseTimeToUnixSeconds(cookie.creation);
        if (created_at <= 0) {
          created_at = CefBaseTimeToUnixSeconds(cookie.last_access);
        }
        delete_cookie = created_at >= cutoff_;
        return true;
      }
     private:
      int64_t cutoff_;
      IMPLEMENT_REFCOUNTING(TimeRangeCookieDeleter);
    };
    mgr->VisitAllCookies(new TimeRangeCookieDeleter(cutoff));
  } else {
    mgr->DeleteCookies("", "", nullptr);
  }
}

void ClearCacheData(CefRefPtr<CefBrowser> browser,
                    int64_t cutoff,
                    const std::vector<std::string>& filtered_origins) {
  CefRefPtr<CefRequestContext> req_ctx =
      browser ? browser->GetHost()->GetRequestContext() : nullptr;
  if (!req_ctx) return;
  if (!filtered_origins.empty()) {
    for (const auto& origin : filtered_origins) {
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("origin", origin);
      params->SetString("storageTypes", "cache_storage");
      browser->GetHost()->ExecuteDevToolsMethod(
          0, "Storage.clearDataForOrigin", params);
    }
  } else if (cutoff == 0) {
    req_ctx->ClearHttpCache(nullptr);
  }
}

void ClearHistoryData(OtfHandler* handler, int64_t cutoff) {
  if (!handler || !handler->store_) return;
  if (cutoff > 0) {
    handler->store_->ClearHistorySince(cutoff, handler->active_workspace_id_);
  } else {
    handler->store_->ClearHistory(handler->active_workspace_id_);
  }
  if (handler->tab_manager_) {
    for (int tab_id : handler->tab_manager_->GetAllTabIds()) {
      const std::string url = handler->tab_manager_->GetUrl(tab_id);
      if (otf::IsPersistableWebUrl(url) && !otf::IsInternalUiUrl(url)) {
        handler->tab_manager_->SetHistorySuppressedUrl(tab_id, url);
      }
    }
  }
  handler->SendEvent(JsonObjectBuilder()
                         .AddString("key", "history-changed")
                         .Build());
}

void ClearDownloadData(OtfHandler* handler, int64_t cutoff) {
  if (!handler || !handler->store_) return;
  if (cutoff > 0) {
    handler->store_->ClearDownloadsSince(cutoff);
  } else {
    handler->store_->ClearDownloads();
    for (const auto& entry : handler->download_callbacks_) {
      if (entry.second) entry.second->Cancel();
    }
    handler->downloads_.clear();
    handler->download_callbacks_.clear();
    handler->runtime_download_ids_.clear();
  }
  handler->NotifyDownloadsChanged();
  handler->NotifyDownloadBadge();
}

void ClearSiteStorageData(OtfHandler* handler,
                          CefRefPtr<CefBrowser> browser,
                          int64_t cutoff,
                          const std::vector<std::string>& filtered_origins) {
  CefRefPtr<CefRequestContext> req_ctx =
      browser ? browser->GetHost()->GetRequestContext() : nullptr;
  if (req_ctx) {
    req_ctx->ClearHttpAuthCredentials(nullptr);
    req_ctx->ClearCertificateExceptions(nullptr);
  }
  if (handler && handler->store_) {
    handler->store_->ClearAllSitePermissions();
  }
  CefRefPtr<CefCookieManager> mgr =
      req_ctx ? req_ctx->GetCookieManager(nullptr) : nullptr;
  if (!mgr || !browser) return;
  if (!filtered_origins.empty()) {
    for (const auto& origin : filtered_origins) {
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("origin", origin);
      params->SetString("storageTypes",
          "appcache,file_systems,indexeddb,local_storage,"
          "shader_cache,websql,service_workers,cache_storage");
      browser->GetHost()->ExecuteDevToolsMethod(
          0, "Storage.clearDataForOrigin", params);
    }
  } else if (cutoff == 0) {
    class StorageClearVisitor : public CefCookieVisitor {
     public:
      explicit StorageClearVisitor(CefRefPtr<CefBrowser> browser)
          : browser_(browser) {}
      bool Visit(const CefCookie& cookie, int count, int total,
                 bool& delete_cookie) override {
        delete_cookie = false;
        std::string domain = CefString(&cookie.domain).ToString();
        if (!domain.empty() && domain[0] == '.') domain = domain.substr(1);
        if (domain.empty()) return true;
        std::string origin =
            cookie.secure ? "https://" + domain : "http://" + domain;
        if (origins_.insert(origin).second) {
          CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
          params->SetString("origin", origin);
          params->SetString("storageTypes",
              "appcache,file_systems,indexeddb,local_storage,"
              "shader_cache,websql,service_workers,cache_storage");
          browser_->GetHost()->ExecuteDevToolsMethod(
              0, "Storage.clearDataForOrigin", params);
        }
        return true;
      }
     private:
      CefRefPtr<CefBrowser> browser_;
      std::set<std::string> origins_;
      IMPLEMENT_REFCOUNTING(StorageClearVisitor);
    };
    mgr->VisitAllCookies(new StorageClearVisitor(browser));
  }
}

}  // namespace

bool HandleClearDataRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  if (!handler || request.method != "browsingData.clear") return false;

  std::vector<std::string> categories;
  std::string time_range;
  std::string error;
  if (!ParseClearRequest(request, &categories, &time_range, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  const int64_t cutoff = CutoffForRange(time_range);
  std::vector<std::string> filtered_origins;
  if (cutoff > 0 && handler->store_) {
    filtered_origins = handler->store_->GetDistinctOriginsSince(
        cutoff, handler->active_workspace_id_);
  }

  if (Contains(categories, "cookies")) ClearCookieData(browser, cutoff);
  if (Contains(categories, "cache")) {
    ClearCacheData(browser, cutoff, filtered_origins);
  }
  if (Contains(categories, "history")) ClearHistoryData(handler, cutoff);
  if (Contains(categories, "downloads")) ClearDownloadData(handler, cutoff);
  if (Contains(categories, "siteData")) {
    ClearSiteStorageData(handler, browser, cutoff, filtered_origins);
  }

  Success(callback, request);
  return true;
}

}  // namespace otf
