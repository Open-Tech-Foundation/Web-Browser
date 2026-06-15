#include "otf_doc_preview_runtime.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_task.h"
#include "include/cef_urlrequest.h"
#include "include/views/cef_browser_view.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_native_rpc.h"
#include "otf_utils.h"

namespace otf {
namespace {

std::string GetDataURI(const std::string& data, const std::string& mime_type) {
  return "data:" + mime_type + ";base64," +
         CefBase64Encode(data.data(), data.size()).ToString();
}

class DeferredDocPreviewPushTask : public CefTask {
 public:
  explicit DeferredDocPreviewPushTask(int tab_id) : tab_id_(tab_id) {}

  void Execute() override {
    OtfApp* app = OtfApp::GetInstance();
    OtfHandler* handler = OtfHandler::GetInstance();
    if (!app || !handler || tab_id_ < 0) {
      return;
    }

    std::string event = handler->BuildDocPreviewLoadEvent(tab_id_);
    if (event.empty()) {
      return;
    }

    auto tab_sub = handler->tab_doc_preview_subscriptions_.find(tab_id_);
    if (tab_sub != handler->tab_doc_preview_subscriptions_.end() &&
        tab_sub->second) {
      tab_sub->second->Success(event);
    } else if (handler->doc_preview_subscription_) {
      handler->doc_preview_subscription_->Success(event);
    }

    if (app->doc_preview_overlay_) {
      CefRefPtr<CefBrowserView> preview_view =
          app->doc_preview_overlay_->GetContentsView()->AsBrowserView();
      CefRefPtr<CefBrowser> browser =
          preview_view ? preview_view->GetBrowser() : nullptr;
      CefRefPtr<CefFrame> frame = browser ? browser->GetMainFrame() : nullptr;
      if (frame) {
        std::string js =
            "if(window.__otfApplyDocPreview)window.__otfApplyDocPreview(" +
            event + ");";
        frame->ExecuteJavaScript(js, frame->GetURL(), 0);
      }
    }
  }

 private:
  int tab_id_;
  IMPLEMENT_REFCOUNTING(DeferredDocPreviewPushTask);
};

class DeferredDocFetchTask : public CefTask {
 public:
  DeferredDocFetchTask(const std::string& url, int tab_id)
      : url_(url), tab_id_(tab_id) {}

  void Execute() override {
    if (url_.empty() || tab_id_ < 0) return;

    CefRefPtr<CefRequest> request = CefRequest::Create();
    request->SetURL(url_);
    request->SetMethod("GET");

    CefRefPtr<FetchDocCallback> callback = new FetchDocCallback(url_, tab_id_);
    CefURLRequest::Create(request, callback, nullptr);
  }

 private:
  class FetchDocCallback : public CefURLRequestClient {
   public:
    FetchDocCallback(const std::string& url, int tab_id)
        : url_(url), tab_id_(tab_id) {}

    void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
      OtfHandler* handler = OtfHandler::GetInstance();
      if (!handler || tab_id_ < 0) return;

      if (response_bytes_.empty()) {
        handler->SetDocPreviewUrlForTab(tab_id_, url_);
      } else {
        // Keep fetched preview bytes in memory. The renderer receives only a
        // browser://doc-preview/content token, never a temporary file path.
        const std::string name = url_.substr(url_.find_last_of('/') + 1);
        const std::string safe_name = name.empty() ? "document.txt" : name;
        std::string mime;
        CefRefPtr<CefResponse> resp = request->GetResponse();
        if (resp) mime = resp->GetMimeType().ToString();
        if (mime.empty()) mime = otf::GuessDocumentMimeType(safe_name);
        const std::string content_token =
            "fetch/" + std::to_string(tab_id_) + "/" + safe_name;
        const std::string content_url =
            "browser://doc-preview/content/" + content_token;
        otf::RegisterDocContentBytes(
            content_token,
            std::vector<uint8_t>(response_bytes_.begin(),
                                 response_bytes_.end()),
            mime);
        handler->SetDocPreviewContentUrlForTab(tab_id_, content_url);
      }

      handler->ScheduleDocPreviewPushForTab(tab_id_);
    }

    void OnUploadProgress(CefRefPtr<CefURLRequest> request,
                          int64_t current,
                          int64_t total) override {}

    void OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                            int64_t current,
                            int64_t total) override {}

    void OnDownloadData(CefRefPtr<CefURLRequest> request,
                        const void* data,
                        size_t data_length) override {
      const char* p = static_cast<const char*>(data);
      response_bytes_.insert(response_bytes_.end(), p, p + data_length);
    }

    bool GetAuthCredentials(bool isProxy,
                            const CefString& host,
                            int port,
                            const CefString& realm,
                            const CefString& scheme,
                            CefRefPtr<CefAuthCallback> callback) override {
      return false;
    }

    std::string url_;
    int tab_id_;
    std::vector<char> response_bytes_;

    IMPLEMENT_REFCOUNTING(FetchDocCallback);
  };

  std::string url_;
  int tab_id_;
  IMPLEMENT_REFCOUNTING(DeferredDocFetchTask);
};

}  // namespace

std::string GuessDocPreviewFormat(const std::string& url) {
  const size_t end = url.find_first_of("?#");
  std::string clean_url = end == std::string::npos ? url : url.substr(0, end);
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
  return ext;
}

void OtfHandler::ScheduleDocPreviewPushForTab(int tab_id) {
  CefPostTask(TID_UI, new DeferredDocPreviewPushTask(tab_id));
}

void OtfHandler::ScheduleDocPreviewFetchForTab(int tab_id,
                                               const std::string& url) {
  CefPostTask(TID_IO, new DeferredDocFetchTask(url, tab_id));
}

void OtfHandler::ResetDocPreviewFetchStateForTab(int tab_id) {
  const std::string prev_content = GetDocPreviewContentUrlForTab(tab_id);
  static const std::string kContentPrefix = "browser://doc-preview/content/";
  if (prev_content.rfind(kContentPrefix, 0) == 0) {
    otf::UnregisterDocContent(prev_content.substr(kContentPrefix.size()));
  }
  SetDocPreviewContentUrlForTab(tab_id, "");
  SetDocPreviewFileSizeForTab(tab_id, -1);
  SetDocPreviewFormatForTab(tab_id, "");
  tab_doc_preview_render_cache_.erase(tab_id);
}

void OtfHandler::SetDocPreviewUrlForTab(int tab_id, const std::string& url) {
  tab_doc_preview_urls_[tab_id] = url;
  tab_doc_preview_local_files_.erase(tab_id);
  tab_doc_preview_file_sizes_.erase(tab_id);
  tab_doc_preview_formats_.erase(tab_id);
  tab_doc_preview_render_cache_.erase(tab_id);
}

void OtfHandler::SetDocPreviewLocalFileForTab(
    int tab_id,
    const std::string& public_url,
    const std::string& file_path) {
  tab_doc_preview_urls_[tab_id] = public_url;
  tab_doc_preview_local_files_[tab_id] = file_path;
  tab_doc_preview_file_sizes_.erase(tab_id);
  tab_doc_preview_formats_.erase(tab_id);
  tab_doc_preview_render_cache_.erase(tab_id);
}

std::string OtfHandler::GetDocPreviewUrlForTab(int tab_id) const {
  auto it = tab_doc_preview_urls_.find(tab_id);
  return it != tab_doc_preview_urls_.end() ? it->second : "";
}

std::string OtfHandler::GetDocPreviewLocalFileForTab(int tab_id) const {
  auto it = tab_doc_preview_local_files_.find(tab_id);
  return it != tab_doc_preview_local_files_.end() ? it->second : "";
}

void OtfHandler::SetDocPreviewContentUrlForTab(
    int tab_id,
    const std::string& content_url) {
  tab_doc_preview_content_urls_[tab_id] = content_url;
}

std::string OtfHandler::GetDocPreviewContentUrlForTab(int tab_id) const {
  auto it = tab_doc_preview_content_urls_.find(tab_id);
  return it != tab_doc_preview_content_urls_.end() ? it->second : "";
}

void OtfHandler::SetDocPreviewFileSizeForTab(int tab_id,
                                             int64_t file_size_bytes) {
  if (file_size_bytes < 0) {
    tab_doc_preview_file_sizes_.erase(tab_id);
  } else {
    tab_doc_preview_file_sizes_[tab_id] = file_size_bytes;
  }
}

int64_t OtfHandler::GetDocPreviewFileSizeForTab(int tab_id) const {
  auto it = tab_doc_preview_file_sizes_.find(tab_id);
  return it != tab_doc_preview_file_sizes_.end() ? it->second : -1;
}

void OtfHandler::SetDocPreviewFormatForTab(int tab_id,
                                           const std::string& format) {
  if (format.empty()) {
    tab_doc_preview_formats_.erase(tab_id);
  } else {
    tab_doc_preview_formats_[tab_id] = format;
  }
}

std::string OtfHandler::GetDocPreviewFormatForTab(int tab_id) const {
  auto it = tab_doc_preview_formats_.find(tab_id);
  return it != tab_doc_preview_formats_.end() ? it->second : "";
}

void OtfHandler::ClearDocPreviewStateForTab(int tab_id) {
  if (tab_id < 0) {
    return;
  }
  if (tab_manager_) {
    tab_manager_->SetDocPreviewMode(tab_id, DocPreviewMode::kNone);
    if (tab_manager_->GetUrl(tab_id) != "browser://docpreview") {
      tab_manager_->SetSchemeUrl(tab_id, "");
    }
  }
  tab_doc_preview_subscriptions_.erase(tab_id);
  ResetDocPreviewFetchStateForTab(tab_id);
  SetDocPreviewUrlForTab(tab_id, "");
  SetDocPreviewContentUrlForTab(tab_id, "");
  tab_doc_preview_render_cache_.erase(tab_id);
}

std::string OtfHandler::BuildDocPreviewLoadEvent(int tab_id) {
  std::string url = GetDocPreviewUrlForTab(tab_id);
  if (url.empty()) {
    return "";
  }

  const std::string local_path = GetDocPreviewLocalFileForTab(tab_id);
  const std::string content_url = GetDocPreviewContentUrlForTab(tab_id);
  std::string display_url;
  std::string mime_type;
  int64_t file_size_bytes = GetDocPreviewFileSizeForTab(tab_id);
  std::string format = GetDocPreviewFormatForTab(tab_id);

  if (!local_path.empty()) {
    mime_type = otf::GuessDocumentMimeType(local_path);
    auto cache_it = tab_doc_preview_render_cache_.find(tab_id);
    if (cache_it != tab_doc_preview_render_cache_.end() &&
        cache_it->second.file_path == local_path &&
        !cache_it->second.display_url.empty()) {
      display_url = cache_it->second.display_url;
    } else {
      auto file_bytes = otf::ReadFileBinary(local_path);
      if (file_bytes) {
        std::string content(file_bytes->begin(), file_bytes->end());
        if (mime_type == "application/pdf") {
          display_url = GetDataURI(content, "application/pdf");
        } else {
          display_url = "data:text/plain;base64," +
                        CefBase64Encode(content.data(), content.size())
                            .ToString();
        }
        file_size_bytes = static_cast<int64_t>(content.size());
        tab_doc_preview_render_cache_[tab_id] =
            DocPreviewRenderCache{local_path, display_url};
      }
      if (format.empty()) {
        format = GuessDocPreviewFormat(local_path);
        if (format.empty()) {
          std::string ext = mime_type;
          SetDocPreviewFormatForTab(tab_id, ext.empty() ? "TXT" : ext);
        } else {
          SetDocPreviewFormatForTab(tab_id, format);
        }
      }
      if (file_size_bytes >= 0) {
        SetDocPreviewFileSizeForTab(tab_id, file_size_bytes);
      }
    }
  } else if (!content_url.empty()) {
    // Fetched doc served from memory. PDFs render by navigating the tab to
    // content_url; text gets a data URI for the editor.
    static const std::string kContentPrefix = "browser://doc-preview/content/";
    const std::string token =
        content_url.rfind(kContentPrefix, 0) == 0
            ? content_url.substr(kContentPrefix.size())
            : std::string();
    std::vector<uint8_t> bytes;
    std::string mem_mime;
    if (!token.empty() && otf::GetDocContentBytes(token, &bytes, &mem_mime)) {
      mime_type = mem_mime.empty() ? otf::GuessDocumentMimeType(url) : mem_mime;
      file_size_bytes = static_cast<int64_t>(bytes.size());
      auto cache_it = tab_doc_preview_render_cache_.find(tab_id);
      if (cache_it != tab_doc_preview_render_cache_.end() &&
          cache_it->second.file_path == content_url &&
          !cache_it->second.display_url.empty()) {
        display_url = cache_it->second.display_url;
      } else if (mime_type != "application/pdf") {
        std::string content(bytes.begin(), bytes.end());
        display_url = "data:text/plain;base64," +
                      CefBase64Encode(content.data(), content.size())
                          .ToString();
        tab_doc_preview_render_cache_[tab_id] =
            DocPreviewRenderCache{content_url, display_url};
      }
      if (format.empty()) {
        format = GuessDocPreviewFormat(url);
        if (format.empty()) format = mime_type;
        SetDocPreviewFormatForTab(tab_id, format.empty() ? "TXT" : format);
      }
      if (file_size_bytes >= 0) {
        SetDocPreviewFileSizeForTab(tab_id, file_size_bytes);
      }
    }
  }

  if (format.empty()) {
    format = GuessDocPreviewFormat(url);
  }

  return JsonObjectBuilder()
      .AddString("key", "load-doc")
      .AddString("url", url)
      .AddString("displayUrl", display_url)
      .AddString("contentUrl", content_url)
      .AddString("mimeType", mime_type.empty() ? "text/plain" : mime_type)
      .AddInt("tabId", tab_id)
      .AddInt("fileSizeBytes",
              file_size_bytes >= 0
                  ? static_cast<int>(std::min<int64_t>(
                        file_size_bytes, std::numeric_limits<int>::max()))
                  : -1)
      .AddString("format", format)
      .Build();
}

}  // namespace otf
