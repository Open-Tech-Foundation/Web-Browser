#include "otf_doc_preview_rpc.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "otf_app.h"
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

bool RequireNoParams(const NativeRpcRequest& request, std::string* error) {
  return request.params && HasOnlyParamKeys(request.params, {}, error);
}

bool ReadOptionalTabId(CefRefPtr<CefDictionaryValue> params,
                       int* tab_id,
                       std::string* error) {
  if (!params || !params->HasKey("tabId")) {
    return true;
  }
  if (params->GetType("tabId") != VTYPE_INT) {
    if (error) *error = "tabId must be an integer";
    return false;
  }
  const int parsed = params->GetInt("tabId");
  if (parsed < 0) {
    if (error) *error = "tabId must be non-negative";
    return false;
  }
  if (tab_id) *tab_id = parsed;
  return true;
}

bool ReadUrl(CefRefPtr<CefDictionaryValue> params,
             std::string* url,
             std::string* error) {
  if (!params || !params->HasKey("url") ||
      params->GetType("url") != VTYPE_STRING) {
    if (error) *error = "url must be a string";
    return false;
  }
  if (url) *url = params->GetString("url").ToString();
  return true;
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

void Success(CefRefPtr<Callback> callback, const NativeRpcRequest& request) {
  NativeRpcSuccessRaw(callback, request, "null");
}

int ResolveDocPreviewTabId(OtfHandler* handler,
                           CefRefPtr<CefBrowser> browser,
                           int explicit_tab_id) {
  if (explicit_tab_id >= 0) {
    return explicit_tab_id;
  }
  int tab_id = -1;
  if (handler && handler->tab_manager_) {
    tab_id = handler->tab_manager_->GetId(browser);
  }
  if (tab_id == -1) {
    if (OtfApp* app = OtfApp::GetInstance()) {
      tab_id = app->GetCurrentTabId();
    }
  }
  return tab_id;
}

bool PersistCompletedDownload(OtfHandler* handler,
                              const std::string& url,
                              const std::string& original_url,
                              const std::string& target_path,
                              const std::string& suggested_name,
                              const std::string& mime_type,
                              int64_t size_bytes) {
  if (!handler || !handler->store_) {
    return false;
  }
  const int download_id = handler->store_->CreateDownload(
      url, original_url, target_path, suggested_name, mime_type, "completed");
  if (download_id <= 0) {
    return false;
  }

  PersistedDownload download;
  download.id = download_id;
  download.url = url;
  download.original_url = original_url;
  download.target_path = target_path;
  download.filename = suggested_name;
  download.total_bytes = size_bytes;
  download.received_bytes = size_bytes;
  download.status = "completed";
  download.mime_type = mime_type;
  handler->store_->UpdateDownload(download);
  handler->NotifyDownloadsChanged();
  handler->NotifyDownloadBadge();
  return true;
}

bool SaveLocalPreviewDocument(OtfHandler* handler,
                              CefRefPtr<Callback> callback,
                              const NativeRpcRequest& request,
                              const std::string& download_url,
                              const std::string& local_path,
                              const std::string& mime_type) {
  std::error_code ec;
  const std::filesystem::path source_path(local_path);
  const std::string suggested_name =
      source_path.filename().empty()
          ? "download.txt"
          : SanitizeFilename(source_path.filename().string());
  const std::string target_path = otf::BuildDownloadPath(suggested_name);
  std::filesystem::copy_file(local_path, target_path,
                             std::filesystem::copy_options::none, ec);
  if (ec) {
    Failure(callback, request, "failed", "Could not save document file");
    return true;
  }

  const auto file_size = std::filesystem::file_size(local_path, ec);
  PersistCompletedDownload(
      handler, download_url, local_path, target_path, suggested_name,
      mime_type.empty() ? "application/octet-stream" : mime_type,
      ec ? 0 : static_cast<int64_t>(file_size));
  Success(callback, request);
  return true;
}

bool SaveMemoryPreviewDocument(OtfHandler* handler,
                               CefRefPtr<Callback> callback,
                               const NativeRpcRequest& request,
                               const std::string& download_url,
                               const std::string& content_url) {
  static const std::string kContentPrefix = "browser://doc-preview/content/";
  if (content_url.rfind(kContentPrefix, 0) != 0) {
    return false;
  }

  const std::string token = content_url.substr(kContentPrefix.size());
  std::vector<uint8_t> bytes;
  std::string mem_mime;
  if (!otf::GetDocContentBytes(token, &bytes, &mem_mime)) {
    return false;
  }

  const std::string raw_name = token.substr(token.find_last_of('/') + 1);
  const std::string suggested_name =
      SanitizeFilename(raw_name.empty() ? "download.txt" : raw_name);
  const std::string target_path = otf::BuildDownloadPath(suggested_name);
  if (!otf::WriteFileBinary(target_path, bytes.data(), bytes.size())) {
    Failure(callback, request, "failed", "Could not save document file");
    return true;
  }

  const std::string save_mime =
      mem_mime.empty() ? "application/octet-stream" : mem_mime;
  PersistCompletedDownload(handler, download_url, target_path, target_path,
                           suggested_name, save_mime,
                           static_cast<int64_t>(bytes.size()));
  Success(callback, request);
  return true;
}

}  // namespace

bool HandleDocPreviewRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  if (!handler ||
      (request.method != "docPreview.refresh" &&
       request.method != "docPreview.close" &&
       request.method != "docPreview.download")) {
    return false;
  }

  std::string error;
  if (request.method == "docPreview.refresh") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    const int tab_id = ResolveDocPreviewTabId(handler, browser, -1);
    const std::string event =
        tab_id != -1 ? handler->BuildDocPreviewLoadEvent(tab_id)
                     : std::string();
    NativeRpcSuccessRaw(callback, request, event.empty() ? "{}" : event);
    return true;
  }

  if (request.method == "docPreview.close") {
    if (!HasOnlyParamKeys(request.params, {"tabId"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    int tab_id = -1;
    if (!ReadOptionalTabId(request.params, &tab_id, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    tab_id = ResolveDocPreviewTabId(handler, browser, tab_id);

    OtfApp* app = OtfApp::GetInstance();
    if (!app) {
      Success(callback, request);
      return true;
    }

    const bool is_dedicated_preview_tab =
        handler->tab_manager_ &&
        tab_id != -1 &&
        handler->tab_manager_->GetDocPreviewMode(tab_id) ==
            DocPreviewMode::kDedicated;
    if (is_dedicated_preview_tab) {
      handler->CloseTabAndNotify(tab_id);
    } else {
      handler->ClearDocPreviewStateForTab(tab_id);
      app->HideDocPreviewOverlay();
    }
    Success(callback, request);
    return true;
  }

  if (!HasOnlyParamKeys(request.params, {"url"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  std::string download_url;
  if (!ReadUrl(request.params, &download_url, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  int tab_id = -1;
  std::string local_path;
  std::string mime_type;
  if (download_url.rfind("browser://doc-preview/", 0) == 0 ||
      download_url.empty()) {
    if (handler->tab_manager_) {
      tab_id = handler->tab_manager_->GetId(browser);
      std::string mapped_url = handler->GetDocPreviewUrlForTab(tab_id);
      if (!mapped_url.empty()) {
        download_url = mapped_url;
      }
      local_path = handler->GetDocPreviewLocalFileForTab(tab_id);
      if (!local_path.empty()) {
        mime_type = otf::GuessDocumentMimeType(local_path);
      }
    }
  }

  if (!local_path.empty()) {
    return SaveLocalPreviewDocument(handler, callback, request, download_url,
                                    local_path, mime_type);
  }

  const std::string content_url = handler->GetDocPreviewContentUrlForTab(tab_id);
  if (SaveMemoryPreviewDocument(handler, callback, request, download_url,
                                content_url)) {
    return true;
  }

  if (download_url.rfind("file://", 0) == 0) {
    Failure(callback, request, "denied", "file scheme is disabled");
    return true;
  }
  browser->GetHost()->StartDownload(download_url);
  Success(callback, request);
  return true;
}

}  // namespace otf
