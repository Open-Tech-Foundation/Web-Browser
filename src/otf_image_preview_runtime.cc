#include "otf_image_preview_runtime.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>

#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_request_context.h"
#include "include/cef_response.h"
#include "include/cef_task.h"
#include "include/cef_urlrequest.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_native_rpc.h"
#include "otf_utils.h"

namespace otf {
namespace {

using Callback = CefMessageRouterBrowserSide::Handler::Callback;

bool IsTiffFileWithinInputLimit(const std::string& file_path,
                                std::string* error_reason) {
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(file_path, ec);
  if (ec) {
    if (error_reason) {
      *error_reason = "Could not read TIFF file";
    }
    return false;
  }
  if (file_size > kMaxTiffInputBytes) {
    if (error_reason) {
      *error_reason = "TIFF exceeds 64 MB size limit";
    }
    return false;
  }
  return true;
}

class DeferredImagePreviewPushTask : public CefTask {
 public:
  explicit DeferredImagePreviewPushTask(int tab_id) : tab_id_(tab_id) {}

  void Execute() override {
    OtfApp* app = OtfApp::GetInstance();
    OtfHandler* handler = OtfHandler::GetInstance();
    if (!app || !handler || tab_id_ < 0) {
      return;
    }

    std::string event = handler->BuildImagePreviewLoadEvent(tab_id_);
    if (event.empty()) {
      return;
    }

    auto tab_sub = handler->tab_image_preview_subscriptions_.find(tab_id_);
    if (tab_sub != handler->tab_image_preview_subscriptions_.end() &&
        tab_sub->second) {
      tab_sub->second->Success(event);
    } else if (handler->image_preview_subscription_) {
      handler->image_preview_subscription_->Success(event);
    }

    CefRefPtr<CefBrowser> tab_browser =
        handler->tab_manager_ ? handler->tab_manager_->GetBrowser(tab_id_)
                              : nullptr;
    CefRefPtr<CefFrame> tab_frame =
        tab_browser ? tab_browser->GetMainFrame() : nullptr;
    if (tab_frame) {
      std::string js =
          "if(window.__otfApplyImagePreview)window.__otfApplyImagePreview(" +
          event + ");";
      tab_frame->ExecuteJavaScript(js, tab_frame->GetURL(), 0);
    }

    if (app->image_preview_overlay_) {
      CefRefPtr<CefBrowserView> preview_view =
          app->image_preview_overlay_->GetContentsView()->AsBrowserView();
      CefRefPtr<CefBrowser> browser =
          preview_view ? preview_view->GetBrowser() : nullptr;
      CefRefPtr<CefFrame> frame = browser ? browser->GetMainFrame() : nullptr;
      if (frame) {
        std::string js =
            "if(window.__otfApplyImagePreview)window.__otfApplyImagePreview(" +
            event + ");";
        frame->ExecuteJavaScript(js, frame->GetURL(), 0);
      }
    }
  }

 private:
  int tab_id_;
  IMPLEMENT_REFCOUNTING(DeferredImagePreviewPushTask);
};

class OtfTiffDecodeClient : public CefURLRequestClient {
 public:
  OtfTiffDecodeClient(const std::string& source_url,
                      int page,
                      int tab_id,
                      uint64_t decode_nonce,
                      bool thumbnail_request,
                      CefRefPtr<Callback> callback,
                      NativeRpcRequest request)
      : source_url_(source_url),
        page_(page),
        tab_id_(tab_id),
        decode_nonce_(decode_nonce),
        thumbnail_request_(thumbnail_request),
        is_tiff_(otf::IsTiffUrl(source_url)),
        callback_(callback),
        request_(std::move(request)) {}

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    if (rejected_) {
      NativeRpcFailure(callback_, request_, "failed", reject_reason_);
      return;
    }
    OtfHandler* h = OtfHandler::GetInstance();
    if (h && tab_id_ != -1 &&
        h->GetImagePreviewDecodeNonceForTab(tab_id_) != decode_nonce_) {
      NativeRpcSuccessRaw(callback_, request_,
                          JsonObjectBuilder().AddBool("stale", true).Build());
      return;
    }
    if (request->GetRequestStatus() != UR_SUCCESS || raw_bytes_.empty()) {
      NativeRpcFailure(callback_, request_, "failed",
                       is_tiff_ ? "Failed to download or decode TIFF image"
                                : "Failed to download image");
      return;
    }

    std::string mime_type = mime_type_;
    if (mime_type.empty()) {
      CefRefPtr<CefResponse> response = request->GetResponse();
      if (response) {
        mime_type = response->GetMimeType().ToString();
      }
    }
    if (mime_type.empty()) {
      mime_type = GuessImageMimeType(source_url_);
    }

    std::string preview_format = GuessPreviewFormat(source_url_);
    if (preview_format.empty()) {
      preview_format = MimeTypeToPreviewFormat(mime_type);
    }

    if (is_tiff_) {
      std::string png_base64;
      int page_count = 1;
      if (!otf::DecodeTiffBufferToPngBase64(raw_bytes_.data(),
                                            raw_bytes_.size(), page_,
                                            png_base64, page_count)) {
        NativeRpcFailure(callback_, request_, "failed",
                         "Failed to download or decode TIFF image");
        return;
      }
      std::string display_url;
      if (h && tab_id_ != -1) {
        const std::string token = BuildImageContentToken(
            tab_id_, "remote-tiff", page_, source_url_ + ".png");
        std::vector<uint8_t> png_bytes = DecodeDataUrlBytes(png_base64);
        if (png_bytes.empty()) {
          NativeRpcFailure(callback_, request_, "failed",
                           "Failed to prepare decoded TIFF image");
          return;
        }
        otf::RegisterImageContentBytes(token, std::move(png_bytes),
                                       "image/png");
        display_url = "browser://image-preview/content/" + token;
        if (!thumbnail_request_) {
          h->SetImagePreviewPageForTab(tab_id_, page_);
          h->SetImagePreviewPageCountForTab(tab_id_, page_count);
        }
        h->SetImagePreviewFileSizeForTab(
            tab_id_, static_cast<int64_t>(raw_bytes_.size()));
        h->SetImagePreviewFormatForTab(tab_id_, "TIFF");
        if (!thumbnail_request_) {
          h->tab_image_preview_download_cache_[tab_id_] =
              OtfHandler::ImagePreviewDownloadCache{
                  source_url_, mime_type, raw_bytes_, display_url,
                  static_cast<int64_t>(raw_bytes_.size()), page_, page_count,
                  true};
          h->tab_image_preview_render_cache_[tab_id_] =
              OtfHandler::ImagePreviewRenderCache{
                  source_url_, display_url, page_, page_count};
        }
      }
      NativeRpcSuccessRaw(
          callback_, request_,
          JsonObjectBuilder()
              .AddString("displayUrl", display_url)
              .AddInt("pageCount", page_count)
              .AddInt("currentPage", page_)
              .AddInt("fileSizeBytes",
                      static_cast<int>(std::min<size_t>(
                          raw_bytes_.size(),
                          static_cast<size_t>(
                              std::numeric_limits<int>::max()))))
              .AddString("format", "TIFF")
              .Build());
      return;
    }

    std::string display_url;
    if (h && tab_id_ != -1) {
      const std::string token =
          BuildImageContentToken(tab_id_, "remote", 0, source_url_);
      std::vector<uint8_t> image_bytes(raw_bytes_.begin(), raw_bytes_.end());
      otf::RegisterImageContentBytes(token, std::move(image_bytes), mime_type);
      display_url = "browser://image-preview/content/" + token;
      h->SetImagePreviewFileSizeForTab(
          tab_id_, static_cast<int64_t>(raw_bytes_.size()));
      h->SetImagePreviewFormatForTab(tab_id_, preview_format);
      h->tab_image_preview_download_cache_[tab_id_] =
          OtfHandler::ImagePreviewDownloadCache{
              source_url_, mime_type, raw_bytes_, display_url,
              static_cast<int64_t>(raw_bytes_.size()), 0, 1, false};
      h->tab_image_preview_render_cache_[tab_id_] =
          OtfHandler::ImagePreviewRenderCache{source_url_, display_url, 0, 1};
    }
    NativeRpcSuccessRaw(
        callback_, request_,
        JsonObjectBuilder()
            .AddString("displayUrl", display_url)
            .AddInt("pageCount", 1)
            .AddInt("currentPage", 0)
            .AddInt("fileSizeBytes",
                    static_cast<int>(std::min<size_t>(
                        raw_bytes_.size(),
                        static_cast<size_t>(
                            std::numeric_limits<int>::max()))))
            .AddString("format", preview_format)
            .Build());
  }

  void OnUploadProgress(CefRefPtr<CefURLRequest> request,
                        int64_t current,
                        int64_t total) override {}

  void OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                          int64_t current,
                          int64_t total) override {
    if (total > 0 && static_cast<uint64_t>(total) > kMaxTiffInputBytes) {
      RejectAndCancel(request);
      return;
    }
    if (rejected_) {
      return;
    }
    OtfHandler* h = OtfHandler::GetInstance();
    if (!h || tab_id_ == -1 ||
        h->GetImagePreviewDecodeNonceForTab(tab_id_) != decode_nonce_) {
      return;
    }
    if (total > 0) {
      h->SetImagePreviewFileSizeForTab(tab_id_, total);
      if (is_tiff_) {
        h->SetImagePreviewFormatForTab(tab_id_, "TIFF");
      }
    }
    const int received_bytes =
        current < 0
            ? 0
            : static_cast<int>(std::min<int64_t>(
                  current, std::numeric_limits<int>::max()));
    const int total_bytes =
        total < 0
            ? -1
            : static_cast<int>(std::min<int64_t>(
                  total, std::numeric_limits<int>::max()));
    if (total_bytes > 0) {
      const int percent = static_cast<int>(
          std::clamp<int64_t>((current * 100) / total, 0, 100));
      if (percent == last_reported_percent_) {
        return;
      }
      last_reported_percent_ = percent;
    } else {
      if (received_bytes - last_reported_received_bytes_ < 256 * 1024 &&
          received_bytes != 0) {
        return;
      }
      last_reported_received_bytes_ = received_bytes;
    }
    h->NotifyImagePreviewDownloadProgress(tab_id_, decode_nonce_,
                                          received_bytes, total_bytes);
  }

  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override {
    if (rejected_) {
      return;
    }
    if (data_length > kMaxTiffInputBytes ||
        raw_bytes_.size() > kMaxTiffInputBytes - data_length) {
      RejectAndCancel(request);
      return;
    }
    const char* bytes = static_cast<const char*>(data);
    raw_bytes_.append(bytes, data_length);
  }

  bool GetAuthCredentials(bool isProxy,
                          const CefString& host,
                          int port,
                          const CefString& realm,
                          const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override {
    return false;
  }

 private:
  void RejectAndCancel(CefRefPtr<CefURLRequest> request) {
    if (rejected_) {
      return;
    }
    rejected_ = true;
    reject_reason_ = is_tiff_ ? "Remote TIFF exceeds 64 MB size limit"
                              : "Remote image exceeds 64 MB size limit";
    raw_bytes_.clear();
    if (request) {
      request->Cancel();
    }
  }

  std::string source_url_;
  int page_;
  int tab_id_;
  uint64_t decode_nonce_;
  bool thumbnail_request_;
  bool is_tiff_;
  CefRefPtr<Callback> callback_;
  NativeRpcRequest request_;
  std::string raw_bytes_;
  std::string mime_type_;
  bool rejected_ = false;
  std::string reject_reason_;
  int last_reported_percent_ = -1;
  int last_reported_received_bytes_ = 0;

  IMPLEMENT_REFCOUNTING(OtfTiffDecodeClient);
};

}  // namespace

void OtfHandler::ScheduleImagePreviewPushForTab(int tab_id) {
  CefPostTask(TID_UI, new DeferredImagePreviewPushTask(tab_id));
}

void OtfHandler::ScheduleDelayedImagePreviewPushForTab(int tab_id,
                                                       int64_t delay_ms) {
  CefPostDelayedTask(TID_UI, new DeferredImagePreviewPushTask(tab_id),
                     delay_ms);
}

std::string GuessImageMimeType(const std::string& url) {
  auto ends_with = [&](std::string_view suffix) {
    return url.size() >= suffix.size() &&
           url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  if (ends_with(".png")) return "image/png";
  if (ends_with(".gif")) return "image/gif";
  if (ends_with(".jpg") || ends_with(".jpeg")) return "image/jpeg";
  if (ends_with(".webp")) return "image/webp";
  if (ends_with(".bmp")) return "image/bmp";
  if (ends_with(".ico")) return "image/x-icon";
  if (ends_with(".svg")) return "image/svg+xml";
  if (ends_with(".avif")) return "image/avif";
  if (ends_with(".tif") || ends_with(".tiff")) return "image/tiff";
  return "application/octet-stream";
}

std::string BuildImageContentToken(int tab_id,
                                   const std::string& kind,
                                   int page,
                                   const std::string& name_hint) {
  std::string name = SanitizeFilename(name_hint.empty() ? "preview" : name_hint);
  if (name.empty()) {
    name = "preview";
  }
  return std::to_string(tab_id) + "/" + kind + "/" + std::to_string(page) +
         "/" + name;
}

std::vector<uint8_t> DecodeDataUrlBytes(const std::string& data_url) {
  const size_t comma = data_url.find(',');
  if (comma == std::string::npos) {
    return {};
  }
  CefRefPtr<CefBinaryValue> decoded =
      CefBase64Decode(data_url.substr(comma + 1));
  if (!decoded) {
    return {};
  }
  std::vector<uint8_t> bytes(decoded->GetSize());
  if (!bytes.empty()) {
    decoded->GetData(bytes.data(), bytes.size(), 0);
  }
  return bytes;
}

std::string MimeTypeToPreviewFormat(const std::string& mime_type) {
  if (mime_type == "image/jpeg") return "JPEG";
  if (mime_type == "image/png") return "PNG";
  if (mime_type == "image/gif") return "GIF";
  if (mime_type == "image/webp") return "WEBP";
  if (mime_type == "image/bmp") return "BMP";
  if (mime_type == "image/x-icon" ||
      mime_type == "image/vnd.microsoft.icon") {
    return "ICO";
  }
  if (mime_type == "image/svg+xml") return "SVG";
  if (mime_type == "image/avif") return "AVIF";
  if (mime_type == "image/tiff") return "TIFF";
  return "";
}

bool DecodeLocalTiffPreview(const std::string& file_path,
                            int page_index,
                            std::string* png_base64,
                            int* page_count,
                            std::string* error_reason) {
  if (!IsTiffFileWithinInputLimit(file_path, error_reason)) {
    return false;
  }
  std::string local_png;
  int local_page_count = 1;
  if (!otf::DecodeTiffToPngBase64(file_path, page_index, local_png,
                                  local_page_count)) {
    if (error_reason && error_reason->empty()) {
      *error_reason = "Failed to decode TIFF image";
    }
    return false;
  }
  if (png_base64) {
    *png_base64 = std::move(local_png);
  }
  if (page_count) {
    *page_count = local_page_count;
  }
  return true;
}

int64_t GetFileSizeBytes(const std::string& file_path,
                         std::string* error_reason) {
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(file_path, ec);
  if (ec) {
    if (error_reason) {
      *error_reason = "Could not read TIFF file size";
    }
    return -1;
  }
  return static_cast<int64_t>(file_size);
}

std::string GuessPreviewFormat(const std::string& url) {
  const size_t end = url.find_first_of("?#");
  std::string clean_url = (end == std::string::npos) ? url : url.substr(0, end);
  const size_t slash = clean_url.find_last_of('/');
  const size_t dot = clean_url.find_last_of('.');
  if (dot == std::string::npos ||
      (slash != std::string::npos && dot < slash)) {
    return "";
  }
  std::string ext = clean_url.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  if (ext == "TIF" || ext == "TIFF" || ext == "PNG" || ext == "JPG" ||
      ext == "JPEG" || ext == "GIF" || ext == "WEBP" || ext == "BMP" ||
      ext == "ICO" || ext == "SVG" || ext == "AVIF") {
    return ext;
  }
  return "";
}

bool OtfHandler::HandleImagePreviewDecodeRequest(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<Callback> callback,
    const NativeRpcRequest& request,
    bool thumbnail_request,
    uint64_t decode_nonce,
    int page_index,
    int explicit_tab_id,
    const std::string& requested_source_url) {
  std::string source_url = requested_source_url;
  int preview_tab_id = explicit_tab_id >= 0 ? explicit_tab_id : -1;
  if (preview_tab_id == -1 && tab_manager_) {
    preview_tab_id = tab_manager_->GetId(browser);
  }
  if (preview_tab_id == -1) {
    OtfApp* app = OtfApp::GetInstance();
    if (app) preview_tab_id = app->GetCurrentTabId();
  }
  if (preview_tab_id == -1) {
    for (const auto& [tab_id, stored_url] : tab_image_preview_urls_) {
      if (stored_url == source_url) {
        preview_tab_id = tab_id;
        break;
      }
    }
  }

  if (source_url.rfind("browser://image-preview/", 0) == 0 ||
      source_url.empty()) {
    if (preview_tab_id != -1) {
      std::string mapped_url = GetImagePreviewUrlForTab(preview_tab_id);
      if (!mapped_url.empty()) {
        source_url = mapped_url;
      }
    }
  }

  if (preview_tab_id != -1) {
    const std::string local_path = GetImagePreviewLocalFileForTab(preview_tab_id);
    if (!local_path.empty()) {
      if (!otf::IsTiffUrl(source_url)) {
        NativeRpcFailure(callback, request, "unsupported_scheme",
                         "Unsupported image scheme");
        return true;
      }
      std::string png_base64;
      int page_count = 1;
      std::string error_reason;
      if (DecodeLocalTiffPreview(local_path, page_index, &png_base64,
                                 &page_count, &error_reason)) {
        if (!thumbnail_request) {
          SetImagePreviewPageForTab(preview_tab_id, page_index);
          SetImagePreviewPageCountForTab(preview_tab_id, page_count);
        }
        const int64_t file_size = GetFileSizeBytes(local_path, &error_reason);
        if (file_size >= 0) {
          SetImagePreviewFileSizeForTab(preview_tab_id, file_size);
          SetImagePreviewFormatForTab(preview_tab_id, "TIFF");
        }
        const std::string token = BuildImageContentToken(
            preview_tab_id, "local-tiff", page_index, source_url + ".png");
        std::vector<uint8_t> png_bytes = DecodeDataUrlBytes(png_base64);
        if (png_bytes.empty()) {
          NativeRpcFailure(callback, request, "failed",
                           "Failed to prepare decoded TIFF image");
          return true;
        }
        otf::RegisterImageContentBytes(token, std::move(png_bytes),
                                       "image/png");
        const std::string display_url =
            "browser://image-preview/content/" + token;
        if (!thumbnail_request) {
          tab_image_preview_render_cache_[preview_tab_id] =
              OtfHandler::ImagePreviewRenderCache{
                  local_path, display_url, page_index, page_count};
        }
        NativeRpcSuccessRaw(
            callback, request,
            JsonObjectBuilder()
                .AddString("displayUrl", display_url)
                .AddInt("pageCount", page_count)
                .AddInt("currentPage", page_index)
                .AddInt("fileSizeBytes",
                        file_size >= 0
                            ? static_cast<int>(
                                  std::min<int64_t>(
                                      file_size,
                                      std::numeric_limits<int>::max()))
                            : -1)
                .AddString("format", "TIFF")
                .Build());
      } else {
        NativeRpcFailure(callback, request, "failed",
                         error_reason.empty()
                             ? "Failed to decode downloaded TIFF file"
                             : error_reason);
      }
      return true;
    }

    if (source_url.rfind("http://", 0) == 0 ||
        source_url.rfind("https://", 0) == 0) {
      auto cache_it = tab_image_preview_download_cache_.find(preview_tab_id);
      if (cache_it != tab_image_preview_download_cache_.end() &&
          cache_it->second.source_url == source_url &&
          !cache_it->second.display_url.empty()) {
        NativeRpcSuccessRaw(callback, request,
                            BuildImagePreviewLoadEvent(preview_tab_id, false));
        return true;
      }

      CefRefPtr<CefRequestContext> request_context =
          browser ? browser->GetHost()->GetRequestContext()
                  : CefRequestContext::GetGlobalContext();
      CefRefPtr<CefRequest> decode_request = CefRequest::Create();
      decode_request->SetURL(source_url);
      decode_request->SetMethod("GET");
      decode_request->SetFlags(UR_FLAG_ALLOW_STORED_CREDENTIALS |
                               UR_FLAG_NO_RETRY_ON_5XX);
      CefRefPtr<OtfTiffDecodeClient> client =
          new OtfTiffDecodeClient(source_url, page_index, preview_tab_id,
                                  decode_nonce, thumbnail_request, callback,
                                  request);
      CefURLRequest::Create(decode_request, client, request_context);
      return true;
    }
  }

  if (source_url.rfind("file://", 0) == 0) {
    NativeRpcFailure(callback, request, "denied", "file scheme is disabled");
    return true;
  }
  NativeRpcFailure(callback, request, "unsupported_scheme",
                   "Unsupported TIFF scheme");
  return true;
}

void OtfHandler::SetImagePreviewUrlForTab(int tab_id,
                                          const std::string& url) {
  otf::UnregisterImageContentForTab(tab_id);
  tab_image_preview_urls_[tab_id] = url;
  tab_image_preview_local_files_.erase(tab_id);
  tab_image_preview_file_sizes_.erase(tab_id);
  tab_image_preview_formats_.erase(tab_id);
  tab_image_preview_render_cache_.erase(tab_id);
  tab_image_preview_download_cache_.erase(tab_id);
  tab_image_preview_decode_nonces_.erase(tab_id);
  if (tab_manager_) {
    tab_manager_->SetImagePreviewDimensions(tab_id, 0, 0);
    tab_manager_->SetImagePreviewInfoVisible(tab_id, true);
  }
  // Setting or clearing the URL resets navigation state; a different image
  // invalidates the previous TIFF page index.
  tab_image_preview_pages_[tab_id] = 0;
  tab_image_preview_page_counts_[tab_id] = 1;
  if (!url.empty()) {
    ScheduleImagePreviewPushForTab(tab_id);
  }
}

void OtfHandler::ClearInlineImagePreviewForTab(int tab_id) {
  if (tab_id < 0) {
    return;
  }
  if (tab_manager_) {
    tab_manager_->SetImagePreviewMode(tab_id, ImagePreviewMode::kNone);
    if (tab_manager_->GetUrl(tab_id) != "browser://imagepreview") {
      tab_manager_->SetSchemeUrl(tab_id, "");
    }
  }
  SetImagePreviewUrlForTab(tab_id, "");
}

void OtfHandler::ClearImagePreviewStateForTab(int tab_id) {
  tab_image_preview_subscriptions_.erase(tab_id);
  tab_image_preview_urls_.erase(tab_id);
  tab_image_preview_local_files_.erase(tab_id);
  tab_image_preview_file_sizes_.erase(tab_id);
  tab_image_preview_formats_.erase(tab_id);
  tab_image_preview_render_cache_.erase(tab_id);
  tab_image_preview_download_cache_.erase(tab_id);
  tab_image_preview_pages_.erase(tab_id);
  tab_image_preview_page_counts_.erase(tab_id);
  tab_image_preview_decode_nonces_.erase(tab_id);
  otf::UnregisterImageContentForTab(tab_id);
}

void OtfHandler::SetImagePreviewLocalFileForTab(
    int tab_id,
    const std::string& public_url,
    const std::string& file_path) {
  otf::UnregisterImageContentForTab(tab_id);
  tab_image_preview_urls_[tab_id] = public_url;
  tab_image_preview_local_files_[tab_id] = file_path;
  tab_image_preview_file_sizes_.erase(tab_id);
  tab_image_preview_formats_.erase(tab_id);
  tab_image_preview_render_cache_.erase(tab_id);
  tab_image_preview_download_cache_.erase(tab_id);
  tab_image_preview_decode_nonces_.erase(tab_id);
  if (tab_manager_) {
    tab_manager_->SetImagePreviewDimensions(tab_id, 0, 0);
    tab_manager_->SetImagePreviewInfoVisible(tab_id, true);
  }
  tab_image_preview_pages_[tab_id] = 0;
  tab_image_preview_page_counts_[tab_id] = 1;
  if (!public_url.empty() && !file_path.empty()) {
    ScheduleImagePreviewPushForTab(tab_id);
  }
}

void OtfHandler::SetImagePreviewFileSizeForTab(int tab_id,
                                               int64_t file_size_bytes) {
  if (file_size_bytes < 0) {
    tab_image_preview_file_sizes_.erase(tab_id);
  } else {
    tab_image_preview_file_sizes_[tab_id] = file_size_bytes;
  }
}

int64_t OtfHandler::GetImagePreviewFileSizeForTab(int tab_id) const {
  auto it = tab_image_preview_file_sizes_.find(tab_id);
  return it != tab_image_preview_file_sizes_.end() ? it->second : -1;
}

void OtfHandler::SetImagePreviewFormatForTab(int tab_id,
                                             const std::string& format) {
  if (format.empty()) {
    tab_image_preview_formats_.erase(tab_id);
  } else {
    tab_image_preview_formats_[tab_id] = format;
  }
}

std::string OtfHandler::GetImagePreviewFormatForTab(int tab_id) const {
  auto it = tab_image_preview_formats_.find(tab_id);
  return it != tab_image_preview_formats_.end() ? it->second : "";
}

std::string OtfHandler::GetImagePreviewUrlForTab(int tab_id) const {
  auto it = tab_image_preview_urls_.find(tab_id);
  if (it != tab_image_preview_urls_.end()) {
    return it->second;
  }
  return "";
}

std::string OtfHandler::GetImagePreviewLocalFileForTab(int tab_id) const {
  auto it = tab_image_preview_local_files_.find(tab_id);
  if (it != tab_image_preview_local_files_.end()) {
    return it->second;
  }
  return "";
}

uint64_t OtfHandler::BumpImagePreviewDecodeNonceForTab(int tab_id) {
  uint64_t& nonce = tab_image_preview_decode_nonces_[tab_id];
  if (nonce == 0) {
    nonce = 1;
  } else {
    ++nonce;
  }
  return nonce;
}

uint64_t OtfHandler::GetImagePreviewDecodeNonceForTab(int tab_id) const {
  auto it = tab_image_preview_decode_nonces_.find(tab_id);
  return it != tab_image_preview_decode_nonces_.end() ? it->second : 0;
}

void OtfHandler::NotifyImagePreviewDownloadProgress(int tab_id,
                                                    uint64_t decode_nonce,
                                                    int received_bytes,
                                                    int total_bytes) {
  if (tab_id == -1 || GetImagePreviewDecodeNonceForTab(tab_id) != decode_nonce) {
    return;
  }
  CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> sub;
  auto it = tab_image_preview_subscriptions_.find(tab_id);
  if (it != tab_image_preview_subscriptions_.end()) {
    sub = it->second;
  } else {
    sub = image_preview_subscription_;
  }
  if (!sub) {
    return;
  }

  int percent = -1;
  if (total_bytes > 0) {
    percent =
        static_cast<int>(std::clamp((received_bytes * 100) / total_bytes,
                                    0, 100));
  }
  sub->Success(JsonObjectBuilder()
                   .AddString("key", "image-preview-download-progress")
                   .AddString("decodeNonce", std::to_string(decode_nonce))
                   .AddInt("receivedBytes", received_bytes)
                   .AddInt("totalBytes", total_bytes)
                   .AddInt("percent", percent)
                   .Build());
}

void OtfHandler::SetImagePreviewPageForTab(int tab_id, int page) {
  tab_image_preview_pages_[tab_id] = page < 0 ? 0 : page;
}

int OtfHandler::GetImagePreviewPageForTab(int tab_id) const {
  auto it = tab_image_preview_pages_.find(tab_id);
  return it != tab_image_preview_pages_.end() ? it->second : 0;
}

void OtfHandler::SetImagePreviewPageCountForTab(int tab_id, int count) {
  tab_image_preview_page_counts_[tab_id] = count < 1 ? 1 : count;
}

int OtfHandler::GetImagePreviewPageCountForTab(int tab_id) const {
  auto it = tab_image_preview_page_counts_.find(tab_id);
  return it != tab_image_preview_page_counts_.end() ? it->second : 1;
}

std::string OtfHandler::BuildImagePreviewLoadEvent(int tab_id,
                                                   bool bump_decode_nonce) {
  std::string url = GetImagePreviewUrlForTab(tab_id);
  if (url.empty()) return "";

  int page = GetImagePreviewPageForTab(tab_id);
  const uint64_t decode_nonce =
      bump_decode_nonce ? BumpImagePreviewDecodeNonceForTab(tab_id)
                        : GetImagePreviewDecodeNonceForTab(tab_id);
  otf::ImagePreviewPayload payload;
  payload.display_url.clear();
  payload.page_count = 1;
  payload.natural_width =
      tab_manager_ ? tab_manager_->GetImagePreviewWidth(tab_id) : 0;
  payload.natural_height =
      tab_manager_ ? tab_manager_->GetImagePreviewHeight(tab_id) : 0;
  payload.show_info =
      tab_manager_ ? tab_manager_->IsImagePreviewInfoVisible(tab_id) : true;
  const std::string local_path = GetImagePreviewLocalFileForTab(tab_id);
  const bool is_remote_source =
      url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
  if (!local_path.empty()) {
    auto cache_it = tab_image_preview_render_cache_.find(tab_id);
    if (cache_it != tab_image_preview_render_cache_.end() &&
        cache_it->second.file_path == local_path &&
        cache_it->second.page == page &&
        !cache_it->second.display_url.empty()) {
      payload.display_url = cache_it->second.display_url;
      payload.page_count = cache_it->second.page_count;
    } else {
      if (otf::IsTiffUrl(local_path)) {
        std::string png_base64;
        int decoded_page_count = 1;
        std::string error_reason;
        if (DecodeLocalTiffPreview(local_path, page, &png_base64,
                                   &decoded_page_count, &error_reason)) {
          const std::string token =
              BuildImageContentToken(tab_id, "local-tiff", page,
                                     url + ".png");
          std::vector<uint8_t> png_bytes = DecodeDataUrlBytes(png_base64);
          if (!png_bytes.empty()) {
            otf::RegisterImageContentBytes(token, std::move(png_bytes),
                                           "image/png");
            payload.display_url = "browser://image-preview/content/" + token;
          } else {
            payload.display_url.clear();
          }
          payload.page_count = decoded_page_count;
          tab_image_preview_render_cache_[tab_id] = ImagePreviewRenderCache{
              local_path, payload.display_url, page, decoded_page_count};
          SetImagePreviewPageForTab(tab_id, page);
          SetImagePreviewPageCountForTab(tab_id, decoded_page_count);
        } else {
          payload.display_url.clear();
          payload.page_count = 1;
          return JsonObjectBuilder()
              .AddString("key", "load-image")
              .AddString("url", url)
              .AddString("displayUrl", "")
              .AddInt("pageCount", 1)
              .AddInt("currentPage", page)
              .AddInt("tabId", tab_id)
              .AddString("decodeNonce", std::to_string(decode_nonce))
              .AddInt("naturalWidth", payload.natural_width)
              .AddInt("naturalHeight", payload.natural_height)
              .AddBool("showInfo", payload.show_info)
              .AddString("error",
                         error_reason.empty()
                             ? "Failed to decode downloaded TIFF file"
                             : error_reason)
              .Build();
        }
      } else if (otf::IsSupportedImageUrl(local_path)) {
        if (!std::filesystem::exists(local_path)) {
          return JsonObjectBuilder()
              .AddString("key", "load-image")
              .AddString("url", url)
              .AddString("displayUrl", "")
              .AddInt("pageCount", 1)
              .AddInt("currentPage", page)
              .AddInt("tabId", tab_id)
              .AddString("decodeNonce", std::to_string(decode_nonce))
              .AddInt("naturalWidth", payload.natural_width)
              .AddInt("naturalHeight", payload.natural_height)
              .AddBool("showInfo", payload.show_info)
              .AddString("error", "Failed to open downloaded image")
              .Build();
        }
        std::string mime_type = GuessImageMimeType(local_path);
        const std::string token =
            BuildImageContentToken(tab_id, "local", 0, local_path);
        otf::RegisterImageContent(token, local_path, mime_type);
        payload.display_url = "browser://image-preview/content/" + token;
        payload.page_count = 1;
        tab_image_preview_render_cache_[tab_id] =
            ImagePreviewRenderCache{local_path, payload.display_url, 0, 1};
        SetImagePreviewPageForTab(tab_id, 0);
        SetImagePreviewPageCountForTab(tab_id, 1);
        SetImagePreviewFormatForTab(tab_id, GuessPreviewFormat(local_path));
        std::string size_error;
        const int64_t file_size = GetFileSizeBytes(local_path, &size_error);
        if (file_size >= 0) {
          SetImagePreviewFileSizeForTab(tab_id, file_size);
        }
      } else {
        payload.display_url.clear();
        payload.page_count = 1;
        return JsonObjectBuilder()
            .AddString("key", "load-image")
            .AddString("url", url)
            .AddString("displayUrl", "")
            .AddInt("pageCount", 1)
            .AddInt("currentPage", page)
            .AddInt("tabId", tab_id)
            .AddString("decodeNonce", std::to_string(decode_nonce))
            .AddInt("naturalWidth", payload.natural_width)
            .AddInt("naturalHeight", payload.natural_height)
            .AddBool("showInfo", payload.show_info)
            .AddString("error", "Unsupported image format")
            .Build();
      }
    }
  } else if (is_remote_source) {
    auto cache_it = tab_image_preview_download_cache_.find(tab_id);
    if (cache_it != tab_image_preview_download_cache_.end() &&
        cache_it->second.source_url == url) {
      auto& cache = cache_it->second;
      payload.page_count = cache.page_count < 1 ? 1 : cache.page_count;
      if (cache.is_tiff) {
        if (cache.page == page && !cache.display_url.empty()) {
          payload.display_url = cache.display_url;
        } else if (!cache.raw_bytes.empty()) {
          std::string png_base64;
          int decoded_page_count = 1;
          if (otf::DecodeTiffBufferToPngBase64(cache.raw_bytes.data(),
                                               cache.raw_bytes.size(), page,
                                               png_base64,
                                               decoded_page_count)) {
            const std::string token =
                BuildImageContentToken(tab_id, "remote-tiff", page,
                                       url + ".png");
            std::vector<uint8_t> png_bytes = DecodeDataUrlBytes(png_base64);
            if (!png_bytes.empty()) {
              otf::RegisterImageContentBytes(token, std::move(png_bytes),
                                             "image/png");
              cache.display_url = "browser://image-preview/content/" + token;
            } else {
              cache.display_url.clear();
            }
            cache.page = page;
            cache.page_count = decoded_page_count;
            payload.display_url = cache.display_url;
            payload.page_count = decoded_page_count;
            tab_image_preview_render_cache_[tab_id] = ImagePreviewRenderCache{
                url, cache.display_url, page, decoded_page_count};
            SetImagePreviewPageForTab(tab_id, page);
            SetImagePreviewPageCountForTab(tab_id, decoded_page_count);
          } else {
            payload.display_url.clear();
          }
        } else {
          payload.display_url.clear();
        }
      } else {
        payload.display_url = cache.display_url;
      }
      if (cache.file_size_bytes >= 0) {
        SetImagePreviewFileSizeForTab(tab_id, cache.file_size_bytes);
      }
      if (!cache.mime_type.empty() && !cache.is_tiff) {
        SetImagePreviewFormatForTab(tab_id, GuessPreviewFormat(url));
      }
    } else {
      payload.display_url.clear();
      payload.page_count = 1;
    }
  } else if (is_remote_source) {
    payload.display_url = url;
  }

  // Refresh stored page_count from the decoder if it learned more pages.
  int page_count = GetImagePreviewPageCountForTab(tab_id);
  if (payload.page_count > page_count) {
    page_count = payload.page_count;
    SetImagePreviewPageCountForTab(tab_id, page_count);
  }

  int64_t file_size_bytes = GetImagePreviewFileSizeForTab(tab_id);
  if (file_size_bytes < 0 && !local_path.empty()) {
    std::string size_error;
    file_size_bytes = GetFileSizeBytes(local_path, &size_error);
    if (file_size_bytes >= 0) {
      SetImagePreviewFileSizeForTab(tab_id, file_size_bytes);
      SetImagePreviewFormatForTab(
          tab_id,
          otf::IsTiffUrl(local_path) ? "TIFF" : GuessPreviewFormat(local_path));
    }
  }
  if (file_size_bytes < 0) {
    auto cache_it = tab_image_preview_download_cache_.find(tab_id);
    if (cache_it != tab_image_preview_download_cache_.end() &&
        cache_it->second.source_url == url) {
      file_size_bytes = cache_it->second.file_size_bytes;
    }
  }
  std::string format = GetImagePreviewFormatForTab(tab_id);
  if (format.empty()) {
    format = GuessPreviewFormat(url);
  }

  return JsonObjectBuilder()
      .AddString("key", "load-image")
      .AddString("url", url)
      .AddString("displayUrl", payload.display_url)
      .AddInt("pageCount", page_count)
      .AddInt("currentPage", page)
      .AddInt("tabId", tab_id)
      .AddString("decodeNonce", std::to_string(decode_nonce))
      .AddInt("fileSizeBytes",
              file_size_bytes >= 0
                  ? static_cast<int>(std::min<int64_t>(
                        file_size_bytes, std::numeric_limits<int>::max()))
                  : -1)
      .AddString("format", format)
      .AddInt("naturalWidth", payload.natural_width)
      .AddInt("naturalHeight", payload.natural_height)
      .AddBool("showInfo", payload.show_info)
      .Build();
}

}  // namespace otf
