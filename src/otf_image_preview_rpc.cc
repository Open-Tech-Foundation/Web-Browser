#include "otf_image_preview_rpc.h"

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

#include "include/cef_request.h"
#include "include/cef_request_context.h"
#include "include/cef_response.h"
#include "include/cef_urlrequest.h"
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
  if (!params) {
    if (error) *error = "params must be an object";
    return false;
  }
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

bool ReadOptionalTabId(CefRefPtr<CefDictionaryValue> params,
                       int* tab_id,
                       std::string* error) {
  if (!params || !params->HasKey("tabId")) return true;
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

bool ReadOptionalBool(CefRefPtr<CefDictionaryValue> params,
                      const std::string& key,
                      bool* value,
                      std::string* error) {
  if (!params || !params->HasKey(key)) return true;
  if (params->GetType(key) != VTYPE_BOOL) {
    if (error) *error = key + " must be a boolean";
    return false;
  }
  if (value) *value = params->GetBool(key);
  return true;
}

bool ReadRequiredInt(CefRefPtr<CefDictionaryValue> params,
                     const std::string& key,
                     int* value,
                     std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_INT) {
    if (error) *error = key + " must be an integer";
    return false;
  }
  const int parsed = params->GetInt(key);
  if (parsed < 0) {
    if (error) *error = key + " must be non-negative";
    return false;
  }
  if (value) *value = parsed;
  return true;
}

bool ReadRequiredString(CefRefPtr<CefDictionaryValue> params,
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

bool ReadRequiredUint64(CefRefPtr<CefDictionaryValue> params,
                        const std::string& key,
                        uint64_t* value,
                        std::string* error) {
  if (!params || !params->HasKey(key)) {
    if (error) *error = key + " must be a string or integer";
    return false;
  }
  if (params->GetType(key) == VTYPE_STRING) {
    const auto parsed = ParseUint64Strict(params->GetString(key).ToString());
    if (!parsed) {
      if (error) *error = key + " must be an unsigned integer";
      return false;
    }
    if (value) *value = *parsed;
    return true;
  }
  if (params->GetType(key) == VTYPE_INT) {
    const int parsed = params->GetInt(key);
    if (parsed < 0) {
      if (error) *error = key + " must be non-negative";
      return false;
    }
    if (value) *value = static_cast<uint64_t>(parsed);
    return true;
  }
  if (error) *error = key + " must be a string or integer";
  return false;
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

int ResolvePreviewTabId(OtfHandler* handler,
                        CefRefPtr<CefBrowser> browser,
                        int explicit_tab_id) {
  if (explicit_tab_id >= 0) return explicit_tab_id;
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

std::string ResolvePreviewUrl(OtfHandler* handler,
                              CefRefPtr<CefBrowser> browser,
                              int explicit_tab_id,
                              std::string url,
                              int* tab_id_out,
                              std::string* local_path_out) {
  int tab_id = ResolvePreviewTabId(handler, browser, explicit_tab_id);
  if (url.rfind("browser://image-preview/", 0) == 0 || url.empty()) {
    if (handler && handler->tab_manager_ && tab_id != -1) {
      std::string mapped_url = handler->GetImagePreviewUrlForTab(tab_id);
      if (!mapped_url.empty()) url = mapped_url;
      if (local_path_out) {
        *local_path_out = handler->GetImagePreviewLocalFileForTab(tab_id);
      }
    }
  }
  if (tab_id_out) *tab_id_out = tab_id;
  return url;
}

bool HandleRefresh(OtfHandler* handler,
                   CefRefPtr<CefBrowser> browser,
                   CefRefPtr<Callback> callback,
                   const NativeRpcRequest& request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"tabId"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int tab_id = -1;
  if (!ReadOptionalTabId(request.params, &tab_id, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  tab_id = ResolvePreviewTabId(handler, browser, tab_id);
  std::string event = tab_id != -1
                          ? handler->BuildImagePreviewLoadEvent(tab_id, false)
                          : std::string();
  NativeRpcSuccessRaw(callback, request, event.empty() ? "{}" : event);
  return true;
}

bool HandleSetMeta(OtfHandler* handler,
                   CefRefPtr<CefBrowser> browser,
                   CefRefPtr<Callback> callback,
                   const NativeRpcRequest& request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"tabId", "width", "height"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int tab_id = -1;
  int width = 0;
  int height = 0;
  if (!ReadOptionalTabId(request.params, &tab_id, &error) ||
      !ReadRequiredInt(request.params, "width", &width, &error) ||
      !ReadRequiredInt(request.params, "height", &height, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  tab_id = ResolvePreviewTabId(handler, browser, tab_id);
  if (handler->tab_manager_ && tab_id != -1) {
    handler->tab_manager_->SetImagePreviewDimensions(tab_id, width, height);
    const std::string event = handler->BuildImagePreviewLoadEvent(tab_id, false);
    NativeRpcSuccessRaw(callback, request, event.empty() ? "{}" : event);
  } else {
    NativeRpcSuccessRaw(callback, request, "{}");
  }
  return true;
}

bool HandleSetInfoVisible(OtfHandler* handler,
                          CefRefPtr<CefBrowser> browser,
                          CefRefPtr<Callback> callback,
                          const NativeRpcRequest& request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"tabId", "visible"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int tab_id = -1;
  bool visible = true;
  if (!ReadOptionalTabId(request.params, &tab_id, &error) ||
      !ReadOptionalBool(request.params, "visible", &visible, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  tab_id = ResolvePreviewTabId(handler, browser, tab_id);
  if (handler->tab_manager_ && tab_id != -1) {
    handler->tab_manager_->SetImagePreviewInfoVisible(tab_id, visible);
    const std::string event = handler->BuildImagePreviewLoadEvent(tab_id, false);
    NativeRpcSuccessRaw(callback, request, event.empty() ? "{}" : event);
  } else {
    NativeRpcSuccessRaw(callback, request, "{}");
  }
  return true;
}

bool HandleClose(OtfHandler* handler,
                 CefRefPtr<CefBrowser> browser,
                 CefRefPtr<Callback> callback,
                 const NativeRpcRequest& request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"tabId"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int tab_id = -1;
  if (!ReadOptionalTabId(request.params, &tab_id, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  tab_id = ResolvePreviewTabId(handler, browser, tab_id);
  OtfApp* app = OtfApp::GetInstance();
  if (!app) {
    Success(callback, request);
    return true;
  }
  const bool is_dedicated_preview_tab =
      handler->tab_manager_ &&
      tab_id != -1 &&
      handler->tab_manager_->GetImagePreviewMode(tab_id) ==
          ImagePreviewMode::kDedicated;
  if (is_dedicated_preview_tab) {
    handler->CloseTabAndNotify(tab_id);
  } else {
    handler->ClearInlineImagePreviewForTab(tab_id);
    app->HideImagePreviewOverlay();
  }
  Success(callback, request);
  return true;
}

bool HandleDownload(OtfHandler* handler,
                    CefRefPtr<CefBrowser> browser,
                    CefRefPtr<Callback> callback,
                    const NativeRpcRequest& request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"tabId", "url"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int explicit_tab_id = -1;
  std::string download_url;
  if (!ReadOptionalTabId(request.params, &explicit_tab_id, &error) ||
      !ReadRequiredString(request.params, "url", &download_url, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  int tab_id = -1;
  std::string local_path;
  download_url = ResolvePreviewUrl(handler, browser, explicit_tab_id,
                                   download_url, &tab_id, &local_path);
  if (!local_path.empty()) {
    std::error_code ec;
    const std::filesystem::path source_path(local_path);
    const std::string suggested_name =
        source_path.filename().empty()
            ? "download.tiff"
            : SanitizeFilename(source_path.filename().string());
    const std::string target_path = otf::BuildDownloadPath(suggested_name);
    std::filesystem::copy_file(local_path, target_path,
                               std::filesystem::copy_options::none, ec);
    if (ec) {
      Failure(callback, request, "failed", "Could not save TIFF file");
      return true;
    }
    if (handler->store_) {
      const auto file_size = std::filesystem::file_size(local_path, ec);
      const int download_id = handler->store_->CreateDownload(
          download_url, local_path, target_path, suggested_name,
          "image/tiff", "completed");
      if (download_id > 0) {
        PersistedDownload download;
        download.id = download_id;
        download.url = download_url;
        download.original_url = local_path;
        download.target_path = target_path;
        download.filename = suggested_name;
        download.total_bytes = ec ? 0 : static_cast<int64_t>(file_size);
        download.received_bytes = ec ? 0 : static_cast<int64_t>(file_size);
        download.status = "completed";
        download.mime_type = "image/tiff";
        handler->store_->UpdateDownload(download);
        handler->NotifyDownloadsChanged();
        handler->NotifyDownloadBadge();
      }
    }
    Success(callback, request);
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

class ImageSizeRequestClient : public CefURLRequestClient {
 public:
  ImageSizeRequestClient(CefRefPtr<Callback> callback, NativeRpcRequest request)
      : callback_(callback), request_(std::move(request)) {}

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    if (request->GetRequestStatus() == UR_SUCCESS) {
      CefRefPtr<CefResponse> response = request->GetResponse();
      if (response) {
        const std::string len_str =
            response->GetHeaderByName("Content-Length").ToString();
        if (!len_str.empty()) {
          NativeRpcSuccessRaw(callback_, request_, len_str);
          return;
        }
      }
    }
    Failure(callback_, request_, "failed", "Could not fetch size");
  }

  void OnUploadProgress(CefRefPtr<CefURLRequest> request,
                        int64_t current,
                        int64_t total) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                          int64_t current,
                          int64_t total) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override {}
  bool GetAuthCredentials(bool isProxy,
                          const CefString& host,
                          int port,
                          const CefString& realm,
                          const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override {
    return false;
  }

 private:
  CefRefPtr<Callback> callback_;
  NativeRpcRequest request_;
  IMPLEMENT_REFCOUNTING(ImageSizeRequestClient);
};

bool HandleGetSize(OtfHandler* handler,
                   CefRefPtr<CefBrowser> browser,
                   CefRefPtr<Callback> callback,
                   const NativeRpcRequest& request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"tabId", "url"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int explicit_tab_id = -1;
  std::string img_url;
  if (!ReadOptionalTabId(request.params, &explicit_tab_id, &error) ||
      !ReadRequiredString(request.params, "url", &img_url, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  int tab_id = -1;
  std::string local_path;
  img_url = ResolvePreviewUrl(handler, browser, explicit_tab_id, img_url,
                              &tab_id, &local_path);
  if (!local_path.empty()) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(local_path, ec);
    if (!ec) {
      NativeRpcSuccessRaw(callback, request, std::to_string(size));
    } else {
      Failure(callback, request, "failed", "Could not read downloaded file size");
    }
    return true;
  }

  if (img_url.rfind("file://", 0) == 0) {
    Failure(callback, request, "denied", "file scheme is disabled");
    return true;
  }
  if (img_url.rfind("http://", 0) == 0 ||
      img_url.rfind("https://", 0) == 0) {
    CefRefPtr<CefRequestContext> request_context =
        browser ? browser->GetHost()->GetRequestContext()
                : CefRequestContext::GetGlobalContext();
    CefRefPtr<CefRequest> head_request = CefRequest::Create();
    head_request->SetURL(img_url);
    head_request->SetMethod("HEAD");
    CefRefPtr<ImageSizeRequestClient> client =
        new ImageSizeRequestClient(callback, request);
    CefURLRequest::Create(head_request, client, request_context);
    return true;
  }
  Failure(callback, request, "unsupported_scheme", "Unsupported scheme");
  return true;
}

bool HandleDecode(OtfHandler* handler,
                  CefRefPtr<CefBrowser> browser,
                  CefRefPtr<Callback> callback,
                  const NativeRpcRequest& request,
                  bool thumbnail_request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"tabId", "url", "page", "decodeNonce"},
                        &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int explicit_tab_id = -1;
  int page = 0;
  uint64_t decode_nonce = 0;
  std::string url;
  if (!ReadOptionalTabId(request.params, &explicit_tab_id, &error) ||
      !ReadRequiredString(request.params, "url", &url, &error) ||
      !ReadRequiredInt(request.params, "page", &page, &error) ||
      !ReadRequiredUint64(request.params, "decodeNonce", &decode_nonce, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  return handler->HandleImagePreviewDecodeRequest(
      browser, callback, request, thumbnail_request, decode_nonce, page,
      explicit_tab_id, url);
}

}  // namespace

bool HandleImagePreviewRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  if (!handler ||
      (request.method != "imagePreview.refresh" &&
       request.method != "imagePreview.setMeta" &&
       request.method != "imagePreview.setInfoVisible" &&
       request.method != "imagePreview.close" &&
       request.method != "imagePreview.download" &&
       request.method != "imagePreview.getSize" &&
       request.method != "imagePreview.decode" &&
       request.method != "imagePreview.thumbnail")) {
    return false;
  }

  if (request.method == "imagePreview.refresh") {
    return HandleRefresh(handler, browser, callback, request);
  }
  if (request.method == "imagePreview.setMeta") {
    return HandleSetMeta(handler, browser, callback, request);
  }
  if (request.method == "imagePreview.setInfoVisible") {
    return HandleSetInfoVisible(handler, browser, callback, request);
  }
  if (request.method == "imagePreview.close") {
    return HandleClose(handler, browser, callback, request);
  }
  if (request.method == "imagePreview.download") {
    return HandleDownload(handler, browser, callback, request);
  }
  if (request.method == "imagePreview.getSize") {
    return HandleGetSize(handler, browser, callback, request);
  }
  if (request.method == "imagePreview.decode") {
    return HandleDecode(handler, browser, callback, request, false);
  }
  if (request.method == "imagePreview.thumbnail") {
    return HandleDecode(handler, browser, callback, request, true);
  }
  return true;
}

}  // namespace otf
