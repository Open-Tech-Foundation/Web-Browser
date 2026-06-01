#include "otf_app.h"

#include <set>
#include <string>
#include "otf_utils.h"
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "include/cef_browser.h"
#include "include/cef_image.h"
#include "include/internal/cef_types.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_helpers.h"
#include "include/cef_scheme.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/cef_urlrequest.h"
#include "include/wrapper/cef_byte_read_handler.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "otf_handler.h"
#include "otf_page_policy.h"
#include "otf_utils.h"

#include <cctype>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <utility>

namespace otf {

namespace {

OtfApp* g_app_instance = nullptr;

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string TrimTrailingSlash(std::string value) {
  while (value.size() > 1 && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

bool IsDevUiUrl(const std::string& url) {
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::GetGlobalCommandLine();
  if (!command_line || !command_line->HasSwitch("dev-ui-url")) {
    return false;
  }

  const std::string dev_ui_url =
      TrimTrailingSlash(command_line->GetSwitchValue("dev-ui-url").ToString());
  return url == dev_ui_url || url == dev_ui_url + "/" ||
         (StartsWith(url, dev_ui_url + "/") && otf::IsInternalUiPagePath(url));
}

bool IsInheritedFrameUrl(const std::string& url) {
  // about:srcdoc frames have their own V8 realm with separate built-in
  // prototypes — the parent's prototype patches do NOT carry over, so they
  // must receive policy injection like any other frame.
  return url.empty() || url == "about:blank" ||
         StartsWith(url, "data:") || StartsWith(url, "blob:");
}

bool IsRestorableWorkspaceTab(const WorkspaceTab& tab) {
  if (tab.is_image_preview) {
    return otf::IsPersistableWebUrl(tab.url);
  }
  return otf::IsPersistableWebUrl(tab.url) &&
         !StartsWith(tab.url, "browser://") &&
         !IsDevUiUrl(tab.url);
}

// Lazily load the OTF window icon from the PNG files shipped alongside the
// binary at ui/assets/icons/otf-browser-{N}.png. Cached after the first call
// so subsequent windows reuse the same CefImage. Multiple scale factors are
// registered so the WM/Views can pick an appropriate size for the display
// DPR without an extra resize step.
CefRefPtr<CefImage> LoadOtfWindowIcon() {
  static CefRefPtr<CefImage> icon = []() -> CefRefPtr<CefImage> {
    CefRefPtr<CefImage> image = CefImage::CreateImage();
    if (!image) {
      return nullptr;
    }
    const std::string base =
        otf::GetExecutableDir() + "/ui/assets/icons/otf-browser-";
    struct Variant { float scale; int size; };
    // 1x, 2x, 4x — enough to cover standard and hidpi displays. The window
    // manager picks the closest match.
    const Variant kVariants[] = {{1.0f, 64}, {2.0f, 128}, {4.0f, 256}};
    bool any_loaded = false;
    for (const auto& v : kVariants) {
      const std::string path = base + std::to_string(v.size) + ".png";
      auto buf = otf::ReadFileBinary(path);
      if (!buf || buf->empty()) continue;
      if (image->AddPNG(v.scale, buf->data(), buf->size())) {
        any_loaded = true;
      }
    }
    return any_loaded ? image : nullptr;
  }();
  return icon;
}

bool ShouldInjectPagePolicyForFrame(CefRefPtr<CefFrame> frame) {
  const std::string url = frame->GetURL().ToString();
  // Inherited-URL frames (about:blank, empty, data:, blob:) each get their own
  // V8 realm — parent prototype patches do NOT carry over, so a fingerprinter
  // can read pristine APIs straight out of such a child (e.g. create an
  // about:blank iframe and read iframe.contentWindow.navigator/screen/canvas).
  // Walk ancestors; if any is an http(s) document we'd otherwise protect,
  // inject here too so the antifingerprint policy reaches these realms.
  // Top-level inherited frames (no protected ancestor) are skipped — those are
  // transient placeholders the parent writes into, not real content realms.
  if (IsInheritedFrameUrl(url)) {
    CefRefPtr<CefFrame> ancestor = frame->GetParent();
    while (ancestor) {
      const std::string a_url = ancestor->GetURL().ToString();
      if (StartsWith(a_url, "http://") || StartsWith(a_url, "https://")) {
        return true;
      }
      if (!IsInheritedFrameUrl(a_url)) {
        return false;
      }
      ancestor = ancestor->GetParent();
    }
    return false;
  }
  // about:srcdoc frames have their own V8 realm (not shared with the parent)
  // and host real script content, so they need the policy even though they
  // are not an http/https URL.
  if (url == "about:srcdoc") {
    return true;
  }
  if (IsDevUiUrl(url) || StartsWith(url, "browser://") ||
      StartsWith(url, "file://") || StartsWith(url, "devtools://")) {
    return false;
  }
  return otf::ShouldInjectPagePolicy(url);
}

class OtfWindowDelegate : public CefWindowDelegate {
 public:
  OtfWindowDelegate(CefRefPtr<CefBrowserView> ui_view,
                    CefRefPtr<CefBrowserView> content_view,
                    cef_runtime_style_t runtime_style,
                    cef_show_state_t initial_show_state)
      : ui_view_(ui_view),
        content_view_(content_view),
        runtime_style_(runtime_style),
        initial_show_state_(initial_show_state) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    LOG(INFO) << "[otf] win 1: OnWindowCreated begin";
    otf::DiagLog("win 1: OnWindowCreated begin");
    // Replace the default Chromium-derived icons with the OTF logo. Done
    // before any layout so the window is born with the right branding in
    // its title bar and taskbar entry.
    if (auto icon = LoadOtfWindowIcon()) {
      window->SetWindowIcon(icon);
      window->SetWindowAppIcon(icon);
    }

    CefBoxLayoutSettings layout_settings;
    layout_settings.horizontal = false;
    layout_settings.between_child_spacing = 0;
    layout_settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
    layout_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
    CefRefPtr<CefBoxLayout> layout = window->SetToBoxLayout(layout_settings);
    OtfApp* app = OtfApp::GetInstance();
    app->window_ = window;
    app->ui_view_ = ui_view_;

    if (ui_view_) {
      window->AddChildView(ui_view_);
      layout->SetFlexForView(ui_view_.get(), 0);
      LOG(INFO) << "[otf] win 2: UI shell view added to window";
    } else {
      LOG(ERROR) << "[otf] win 2 FAILED: ui_view_ is NULL";
    }

    // Horizontal split: tab content (flex=1) | console panel (flex=0, hidden by default)
    app->content_area_panel_ = CefPanel::CreatePanel(nullptr);
    CefBoxLayoutSettings h_settings;
    h_settings.horizontal = true;
    h_settings.between_child_spacing = 0;
    h_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
    app->content_area_layout_ = app->content_area_panel_->SetToBoxLayout(h_settings);

    app->content_panel_ = CefPanel::CreatePanel(nullptr);
    app->content_panel_->SetToFillLayout();
    app->content_area_panel_->AddChildView(app->content_panel_);
    app->content_area_layout_->SetFlexForView(app->content_panel_.get(), 1);

    window->AddChildView(app->content_area_panel_);
    layout->SetFlexForView(app->content_area_panel_.get(), 1);

    window->Layout();
    LOG(INFO) << "[otf] win 3: layout applied, creating overlays";

    app->CreateFindBarOverlay();
    app->CreateZoomBarOverlay();
    app->CreateDownloadsOverlay();
    app->CreateCertificateOverlay();
    app->CreateAppMenuOverlay();
    app->CreateBookmarkOverlay();
    app->CreateImagePreviewOverlay();
    app->CreateDocPreviewOverlay();
    app->CreateLinkPreviewOverlay();
    app->CreateToastOverlay();
    app->CreateConsoleOverlay();
    app->CreateSnipPreviewOverlay();
    // Build any popup registered against the PopupOverlay framework.
    app->CreateAllPopups(window);

    if (content_view_) {
      app->SwitchTab(content_view_->GetID());
    }

    app->OpenPendingStartupTabs();

    if (initial_show_state_ != CEF_SHOW_STATE_HIDDEN) {
      otf::WindowGeometry geo;
      if (otf::LoadWindowGeometry(&geo) && geo.width > 0 && geo.height > 0) {
        window->SetBounds(CefRect(geo.x, geo.y, geo.width, geo.height));
        if (geo.maximized) window->Maximize();
      } else {
        window->CenterWindow(CefSize(1280, 800));
      }
      window->Show();
      LOG(INFO) << "[otf] win 4: window shown";
      otf::DiagLog("win 4: window shown");
    }
    LOG(INFO) << "[otf] win 5: OnWindowCreated end";
    otf::DiagLog("win 5: OnWindowCreated end");
  }

  void OnLayoutChanged(CefRefPtr<CefView> view, const CefRect& new_bounds) override {
    OtfApp* app = OtfApp::GetInstance();
    if (app) {
      app->PositionFindBarOverlay();
      app->PositionZoomBarOverlay();
      app->PositionDownloadsOverlay();
      app->PositionCertificateOverlay();
      app->PositionAppMenuOverlay();
      app->PositionBookmarkOverlay();
      app->PositionImagePreviewOverlay();
      app->PositionLinkPreviewOverlay();
      app->PositionSnipPreviewOverlay();
      app->RepositionAllPopups();
    }
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    ui_view_ = nullptr;
    content_view_ = nullptr;
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    // Save geometry while the window is still alive — OnWindowDestroyed
    // fires too late (bounds may already be invalid).
    CefRect bounds = window->GetBounds();
    otf::WindowGeometry geo;
    geo.x = bounds.x;
    geo.y = bounds.y;
    geo.width = bounds.width;
    geo.height = bounds.height;
    geo.maximized = window->IsMaximized();
    otf::SaveWindowGeometry(geo);

    CefRefPtr<CefBrowser> browser = ui_view_->GetBrowser();
    if (browser) {
      return browser->GetHost()->TryCloseBrowser();
    }
    return true;
  }

  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return runtime_style_;
  }

 private:
  CefRefPtr<CefBrowserView> ui_view_;
  CefRefPtr<CefBrowserView> content_view_;
  const cef_runtime_style_t runtime_style_;
  const cef_show_state_t initial_show_state_;

  IMPLEMENT_REFCOUNTING(OtfWindowDelegate);
};

class OtfViewDelegate : public CefBrowserViewDelegate {
 public:
  // height=0 → unconstrained; fixed_width=0 → unconstrained
  explicit OtfViewDelegate(cef_runtime_style_t runtime_style,
                           int height = 0,
                           int fixed_width = 0)
      : runtime_style_(runtime_style), height_(height), fixed_width_(fixed_width) {}

  void SetFixedWidth(int w) { fixed_width_ = w; }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    if (fixed_width_ > 0) return CefSize(fixed_width_, height_ > 0 ? height_ : 600);
    if (height_ > 0) return CefSize(800, height_);
    return CefSize(800, 600);
  }

  CefSize GetMinimumSize(CefRefPtr<CefView> view) override {
    if (fixed_width_ > 0) return CefSize(fixed_width_, 0);
    if (height_ > 0) return CefSize(800, height_);
    return CefSize(0, 0);
  }

  CefSize GetMaximumSize(CefRefPtr<CefView> view) override {
    if (fixed_width_ > 0) return CefSize(fixed_width_, 0); // 0 = unconstrained height
    if (height_ > 0) return CefSize(0, height_);
    return CefSize(0, 0);
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return runtime_style_;
  }

  cef_color_t GetBackgroundColor(CefRefPtr<CefView> view) {
    return CefColorSetARGB(0, 0, 0, 0);
  }

 private:
  const cef_runtime_style_t runtime_style_;
  const int height_;
  int fixed_width_;

  IMPLEMENT_REFCOUNTING(OtfViewDelegate);
};

using ::otf::HtmlAttrEscape;

// Owns a byte buffer and keeps it alive via CEF refcounting so a
// CefByteReadHandler can safely read from it after the factory returns.
// CefByteReadHandler takes a raw pointer + size and a "source" ref-counted
// holder; the holder's destruction is what frees the bytes.
class CefBytesHolder : public CefBaseRefCounted {
 public:
  explicit CefBytesHolder(std::vector<uint8_t> bytes)
      : bytes_(std::move(bytes)) {}
  const unsigned char* data() const { return bytes_.data(); }
  size_t size() const { return bytes_.size(); }

 private:
  std::vector<uint8_t> bytes_;
  IMPLEMENT_REFCOUNTING(CefBytesHolder);
};

std::string GuessMimeType(const std::string& path) {
  auto ends_with = [&](std::string_view suffix) {
    return path.size() >= suffix.size() &&
           path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  if (ends_with(".html") || ends_with(".htm")) return "text/html";
  if (ends_with(".js") || ends_with(".mjs")) return "application/javascript";
  if (ends_with(".css")) return "text/css";
  if (ends_with(".json") || ends_with(".map")) return "application/json";
  if (ends_with(".svg")) return "image/svg+xml";
  if (ends_with(".png")) return "image/png";
  if (ends_with(".ico")) return "image/x-icon";
  if (ends_with(".gif")) return "image/gif";
  if (ends_with(".jpg") || ends_with(".jpeg")) return "image/jpeg";
  if (ends_with(".webp")) return "image/webp";
  if (ends_with(".woff2")) return "font/woff2";
  if (ends_with(".woff")) return "font/woff";
  if (ends_with(".ttf")) return "font/ttf";
  if (ends_with(".otf")) return "font/otf";
  if (ends_with(".txt")) return "text/plain";
  if (ends_with(".pdf")) return "application/pdf";
  if (ends_with(".xml")) return "application/xml";
  if (ends_with(".csv")) return "text/csv";
  if (ends_with(".md")) return "text/markdown";
  if (ends_with(".yaml") || ends_with(".yml")) return "text/yaml";
  if (ends_with(".toml")) return "text/plain";
  return "application/octet-stream";
}

// Validate a relative path (no leading slash) before resolving it inside
// ui/. Rejects "..", embedded NULs, backslashes, and anything weird so a
// request like browser://x/../../etc/passwd can't escape.
bool IsSafeUiRelativePath(const std::string& path) {
  if (path.empty()) return false;
  for (char c : path) {
    if (c == '\\' || c == '\0') return false;
  }
  size_t pos = 0;
  while (pos <= path.size()) {
    size_t next = path.find('/', pos);
    if (next == std::string::npos) next = path.size();
    if (path.compare(pos, next - pos, "..") == 0) return false;
    if (next == path.size()) break;
    pos = next + 1;
  }
  return true;
}

bool IsKnownUiPageAuthority(const std::string& authority) {
  static const std::set<std::string> kKnownAuthorities = {
      "shell",           "appmenu",       "newtab",
      "settings",        "findbar",       "downloads",
      "downloadsbar",    "zoombar",       "history",
      "bookmarks",       "bookmarkbar",   "security",
      "insecure-blocked", "pdfviewer",     "certificate",
      "imagepreview",    "docpreview",    "cleardata",
      "sitedata",        "workspace",     "qr",
      "linkpreview",     "console",       "blockedpopup",
      "downloadrequest", "toast",         "snipperview",
  };
  return kKnownAuthorities.count(authority) > 0;
}

CefRefPtr<CefResourceHandler> MakeBytesResponse(
    const std::string& mime, std::vector<uint8_t> bytes) {
  CefRefPtr<CefBytesHolder> holder = new CefBytesHolder(std::move(bytes));
  CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForHandler(
      new CefByteReadHandler(holder->data(), holder->size(), holder));
  return new CefStreamResourceHandler(mime, stream);
}

CefRefPtr<CefResourceHandler> MakeStringResponse(const std::string& mime,
                                                 const std::string& body) {
  return MakeBytesResponse(mime, {body.begin(), body.end()});
}

CefRefPtr<CefResourceHandler> MakeNotFound() {
  return MakeStringResponse(
      "text/html",
      "<!DOCTYPE html><html><body><h1>404</h1><p>Not found.</p></body></html>");
}

CefRefPtr<CefResourceHandler> MakeFileResponse(const std::string& disk_path) {
  auto bytes = otf::ReadFileBinary(disk_path);
  if (!bytes) {
    LOG(ERROR) << "[otf] scheme: file NOT FOUND on disk: " << disk_path;
    otf::DiagLog("scheme: file NOT FOUND on disk: " + disk_path);
    return MakeNotFound();
  }
  LOG(INFO) << "[otf] scheme: served " << bytes->size() << " bytes <- " << disk_path;
  otf::DiagLog("scheme: served " + std::to_string(bytes->size()) +
               " bytes <- " + disk_path);
  return MakeBytesResponse(GuessMimeType(disk_path), std::move(*bytes));
}

std::string LoadTextFile(const std::string& disk_path) {
  return otf::ReadFileText(disk_path);
}

std::string InjectBaseHref(const std::string& html, const std::string& base_href) {
  if (html.empty() || base_href.empty()) {
    return html;
  }
  const std::string needle = "<head>";
  const size_t pos = html.find(needle);
  if (pos == std::string::npos) {
    return html;
  }
  std::string updated = html;
  updated.insert(pos + needle.size(),
                 "<base href=\"" + HtmlAttrEscape(base_href) + "\">");
  return updated;
}

// browser:// scheme handler.
//
// Production mode (no --dev-ui-url):
//   browser://<page>             → ui/<page>.html
//     (special: browser://shell  → ui/index.html — the toolbar shell)
//   browser://<page>/<sub-path>  → ui/<sub-path>
//     (e.g. browser://newtab/assets/main-XYZ.js → ui/assets/main-XYZ.js)
//
//   This replaces the previous file:// loading approach. file:// pages are
//   each their own opaque origin, which means Chromium refuses to fetch
//   ES module scripts across them — the bundled <script type="module">
//   silently failed and the UI rendered empty. The browser:// scheme is
//   registered as STANDARD | SECURE | CORS_ENABLED so modules, fetch, and
//   storage all behave like an http(s) origin, and we don't need to set
//   --allow-file-access-from-files to compensate.
//
// Dev mode (--dev-ui-url set):
//   The same URLs meta-refresh to the vite dev server, which serves the
//   pre-bundle React source via HMR.

// Static map for serving doc-preview content through the scheme handler.
// PDFs and other binary docs need a real URL (CEF's PDF viewer can't load a
// data: URI). An entry serves EITHER a file on disk (`path`, for already-
// persisted download/restore docs) OR bytes held in memory (`bytes`+`mime`,
// for fetched remote docs — so live preview never touches the disk).
struct DocContentEntry {
  std::string path;
  std::vector<uint8_t> bytes;
  std::string mime;
};
std::map<std::string, DocContentEntry>& GetDocContentMap() {
  static std::map<std::string, DocContentEntry> instance;
  return instance;
}

class BrowserSchemeHandlerFactory : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    const std::string url = request->GetURL().ToString();
    LOG(INFO) << "[otf] scheme request: " << url;
    otf::DiagLog("scheme request: " + url);

    CefURLParts parts;
    if (!CefParseURL(url, parts)) {
      return MakeNotFound();
    }
    const std::string authority = CefString(&parts.host).ToString();
    std::string path = CefString(&parts.path).ToString();
    if (!path.empty() && path[0] == '/') path = path.substr(1);
    const std::string query = CefString(&parts.query).ToString();

    // Authority is the page name. Restrict to a safe character set so a
    // request like browser://../etc/foo can't slip through path resolution.
    if (authority.empty()) return MakeNotFound();
    for (char c : authority) {
      if (!(std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_')) {
        return MakeNotFound();
      }
    }

    // doc-content: serve file bytes for document preview (PDFs, etc.)
    // This is now handled under the doc-preview authority via content/ prefix
    // to avoid cross-origin issues with CEF's PDF viewer.

    CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
    const std::string dev_ui_url =
        (cmd && cmd->HasSwitch("dev-ui-url"))
            ? cmd->GetSwitchValue("dev-ui-url").ToString()
            : std::string();
    const bool is_image_preview_route = authority == "image-preview";
    const bool is_image_preview_asset =
        is_image_preview_route && path.rfind("assets/", 0) == 0;
    const bool is_doc_preview_route = authority == "doc-preview";
    const bool is_doc_preview_content =
        is_doc_preview_route && path.rfind("content/", 0) == 0;
    const bool is_doc_preview_asset =
        is_doc_preview_route && path.rfind("assets/", 0) == 0;

    // doc-preview content: serve file bytes for PDFs, etc. from same origin
    // Must be checked before dev mode redirect to avoid loop.
    if (is_doc_preview_content) {
      const std::string token = path.substr(8); // skip "content/"
      auto& content_map = GetDocContentMap();
      auto it = content_map.find(token);
      if (it != content_map.end()) {
        if (!it->second.path.empty()) {
          return MakeFileResponse(it->second.path);
        }
        // Served straight from memory — no disk round-trip. Copy so the map
        // keeps its bytes for repeat/range requests from the PDF viewer.
        return MakeBytesResponse(
            it->second.mime.empty() ? "application/octet-stream"
                                    : it->second.mime,
            it->second.bytes);
      }
      return MakeNotFound();
    }

    if (!dev_ui_url.empty()) {
      // Dev mode: hand off to the vite dev server. After the redirect the
      // document URL becomes http://localhost:3000/<page>.html, and all
      // sub-resource loads go through HTTP — they never come back here.
      std::string target;
      if (is_image_preview_asset) {
        target = dev_ui_url + "/" + path;
      } else if (is_image_preview_route) {
        target = dev_ui_url + "/imagepreview.html";
      } else if (is_doc_preview_asset) {
        target = dev_ui_url + "/" + path;
      } else if (is_doc_preview_route) {
        target = dev_ui_url + "/docpreview.html";
      } else if (path.empty()) {
        const std::string page = (authority == "shell") ? "index" : authority;
        target = dev_ui_url + "/" + page + ".html";
      } else {
        target = dev_ui_url + "/" + path;
      }
      if (!query.empty()) target += "?" + query;
      const std::string body =
          "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='0;url=" +
          HtmlAttrEscape(target) +
          "'></head><body>Redirecting...</body></html>";
      return MakeStringResponse("text/html", body);
    }

    // Production: serve from ui/. "shell" is the toolbar (ui/index.html).
    const std::string exe_dir = otf::GetExecutableDir();
    if (exe_dir.empty()) {
      LOG(ERROR) << "[otf] scheme: GetExecutableDir() returned EMPTY for " << url;
      return MakeNotFound();
    }
    const std::string ui_dir = exe_dir + "/ui";
    LOG(INFO) << "[otf] scheme prod: url=" << url << " ui_dir=" << ui_dir;
    std::string disk_path;
    const bool is_known_ui_page_authority = IsKnownUiPageAuthority(authority);
    if (is_image_preview_asset) {
      if (!IsSafeUiRelativePath(path)) return MakeNotFound();
      disk_path = ui_dir + "/" + path;
    } else if (is_image_preview_route) {
      disk_path = ui_dir + "/imagepreview.html";
    } else if (is_doc_preview_asset) {
      if (!IsSafeUiRelativePath(path)) return MakeNotFound();
      disk_path = ui_dir + "/" + path;
    } else if (is_doc_preview_route) {
      disk_path = ui_dir + "/docpreview.html";
    } else if (path.empty()) {
      if (!is_known_ui_page_authority) return MakeNotFound();
      const std::string page = (authority == "shell") ? "index" : authority;
      disk_path = ui_dir + "/" + page + ".html";
    } else {
      if (!is_known_ui_page_authority) return MakeNotFound();
      if (!IsSafeUiRelativePath(path)) return MakeNotFound();
      disk_path = ui_dir + "/" + path;
    }
    if (is_image_preview_route && !is_image_preview_asset) {
      const std::string html = LoadTextFile(disk_path);
      if (html.empty()) {
        return MakeNotFound();
      }
      return MakeStringResponse(
          "text/html",
          InjectBaseHref(html, "browser://image-preview/"));
    }
    if (is_doc_preview_route && !is_doc_preview_asset && !is_doc_preview_content) {
      const std::string html = LoadTextFile(disk_path);
      if (html.empty()) {
        return MakeNotFound();
      }
      return MakeStringResponse(
          "text/html",
          InjectBaseHref(html, "browser://doc-preview/"));
    }
    return MakeFileResponse(disk_path);
  }

  IMPLEMENT_REFCOUNTING(BrowserSchemeHandlerFactory);
};

}  // namespace

void RegisterDocContent(const std::string& token,
                         const std::string& file_path) {
  GetDocContentMap()[token] = DocContentEntry{file_path, {}, {}};
}

void RegisterDocContentBytes(const std::string& token,
                             std::vector<uint8_t> bytes,
                             const std::string& mime) {
  GetDocContentMap()[token] = DocContentEntry{{}, std::move(bytes), mime};
}

bool GetDocContentBytes(const std::string& token,
                        std::vector<uint8_t>* out_bytes,
                        std::string* out_mime) {
  auto& m = GetDocContentMap();
  auto it = m.find(token);
  if (it == m.end() || it->second.bytes.empty()) return false;
  if (out_bytes) *out_bytes = it->second.bytes;
  if (out_mime) *out_mime = it->second.mime;
  return true;
}

void UnregisterDocContent(const std::string& token) {
  GetDocContentMap().erase(token);
}

OtfApp::OtfApp() {
  DCHECK(!g_app_instance);
  g_app_instance = this;
}

void OtfApp::OnBeforeCommandLineProcessing(const CefString& process_type,
                                           CefRefPtr<CefCommandLine> command_line) {
  if (!process_type.empty()) {
    return;
  }

  // Dev mode: if --dev-ui-url is present, skip all switch filtering so the
  // dev server and other dev flags (--no-sandbox etc.) work without changes.
  if (command_line->HasSwitch("dev-ui-url")) {
    return;
  }

  // Production: hard-block switches that open security surfaces.
  // Sandbox-related switches are stripped by default. Linux keeps --no-sandbox
  // as an explicit release fallback for tarball users who cannot set the
  // chrome-sandbox SUID bit.
  // Remote-debugging switches are blocked (fatal) because they open a network
  // attack surface that cannot be safely stripped after CEF processes them.
  static const std::set<std::string> kBlockedSwitches = {
    "remote-debugging-port", "remote-debugging-pipe", "remote-allow-origins",
  };
  {
    CefCommandLine::SwitchMap all_sw;
    command_line->GetSwitches(all_sw);
    for (const auto& kv : all_sw) {
      if (kBlockedSwitches.count(kv.first.ToString())) {
        LOG(FATAL) << "Blocked security-sensitive switch: --" << kv.first.ToString();
        return;
      }
    }
  }

  // Whitelist of switches permitted in production.
  static const std::set<std::string> kAllowedSwitches = {
#if defined(__linux__)
    "no-sandbox",
#endif
  };

  CefCommandLine::SwitchMap all_switches;
  command_line->GetSwitches(all_switches);

  CefCommandLine::ArgumentList all_args;
  command_line->GetArguments(all_args);

  const CefString program = command_line->GetProgram();
  command_line->Reset();
  command_line->SetProgram(program);

  for (const auto& kv : all_switches) {
    if (kAllowedSwitches.count(kv.first.ToString())) {
      if (kv.second.empty())
        command_line->AppendSwitch(kv.first);
      else
        command_line->AppendSwitchWithValue(kv.first, kv.second);
    }
  }
  for (const auto& arg : all_args)
    command_line->AppendArgument(arg);

  // Engine-level hardening switches (feature disables, UA client-hint spoof,
  // Linux display config). Extracted to otf_utils so the exact switches are
  // unit-testable without launching the browser.
  otf::ApplyProductionCommandLineSwitches(command_line);
}

OtfApp* OtfApp::GetInstance() {
  return g_app_instance;
}

void OtfApp::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) {
#if defined(_WIN32)
  if (!command_line) return;
  command_line->AppendSwitch("no-sandbox");
  std::string child_type = command_line->GetSwitchValue("type").ToString();
  if (child_type.empty()) child_type = "unknown";
  otf::DiagLog("child launch: type=" + child_type + " no-sandbox=YES");
#endif
}

void OtfApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(
      "browser",
      CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
          CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
}

int OtfApp::CreateTab(const std::string& url, int parent_id, bool is_private, bool is_pinned) {
  CEF_REQUIRE_UI_THREAD();

  CefBrowserSettings browser_settings;
  bool js_disabled = false;
  OtfHandler* handler = OtfHandler::GetInstance();
  if (handler && !handler->IsGuestSessionActive()) {
    OtfStore* store = handler->GetStore();
    std::string origin = ExtractOrigin(url);
    if (!origin.empty() && store &&
        store->GetSitePermission(origin, "javascript") == "block") {
      browser_settings.javascript = STATE_DISABLED;
      js_disabled = true;
    }
  }
  CefRefPtr<CefRequestContext> request_context =
      handler ? (is_private ? handler->GetPrivateRequestContext()
                            : handler->GetActiveWorkspaceRequestContext())
              : nullptr;
  CefRefPtr<CefBrowserView> content_view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, browser_settings, MakeBrowserExtraInfo(), request_context,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY));
  if (!content_view) return -1;

  int tab_id = tab_manager_.AddTab(content_view, parent_id);
  content_view->SetID(tab_id);
  if (is_private) {
    tab_manager_.SetPrivate(tab_id, true);
  }
  if (js_disabled && handler) {
    handler->MarkTabJsDisabled(tab_id);
  }
  tab_manager_.SetUrl(tab_id, url);
  if (url.rfind("browser://", 0) == 0) {
    tab_manager_.SetSchemeUrl(tab_id, url);
  }
  if (url == "browser://imagepreview" ||
      url.rfind("browser://image-preview/", 0) == 0 ||
      url.find("/imagepreview.html") != std::string::npos) {
    tab_manager_.SetSchemeUrl(tab_id, "browser://imagepreview");
    tab_manager_.SetImagePreviewMode(tab_id, ImagePreviewMode::kDedicated);
  }
  if (url == "browser://docpreview" ||
      url.rfind("browser://doc-preview/", 0) == 0 ||
      url.find("/docpreview.html") != std::string::npos) {
    tab_manager_.SetSchemeUrl(tab_id, "browser://docpreview");
    tab_manager_.SetDocPreviewMode(tab_id, DocPreviewMode::kDedicated);
  }
  if (handler) {
    tab_manager_.SetWorkspaceId(
        tab_id, handler->IsGuestSessionActive() ? 0 : handler->active_workspace_id_);
  }
  if (is_pinned) {
    tab_manager_.SetPinned(tab_id, true);
  }

  if (content_panel_) {
    content_panel_->AddChildView(content_view);
    content_view->SetVisible(false);
  }

  return tab_id;
}

int OtfApp::CreateRestoredTab(const WorkspaceTab& tab, int parent_id) {
  CEF_REQUIRE_UI_THREAD();
  if (tab.is_image_preview) {
    if (!IsRestorableWorkspaceTab(tab)) {
      return CreateTab("browser://newtab", parent_id);
    }
    const std::string preview_start_url =
        tab.preview_local_path.empty() ? "browser://imagepreview" : tab.url;
    const int tab_id = CreateTab(preview_start_url, parent_id, false, tab.pinned);
    RestoreImagePreviewStateForTab(tab_id, tab);
    if (!tab.favicon.empty()) {
      tab_manager_.SetFaviconUrl(tab_id, tab.favicon);
    }
    return tab_id;
  }
  if (tab.is_doc_preview) {
    if (!IsRestorableWorkspaceTab(tab)) {
      return CreateTab("browser://newtab", parent_id);
    }
    const std::string preview_start_url =
        tab.preview_local_path.empty() ? "browser://docpreview" : tab.url;
    const int tab_id = CreateTab(preview_start_url, parent_id, false, tab.pinned);
    RestoreDocPreviewStateForTab(tab_id, tab);
    if (!tab.favicon.empty()) {
      tab_manager_.SetFaviconUrl(tab_id, tab.favicon);
    }
    return tab_id;
  }
  if (!IsRestorableWorkspaceTab(tab)) {
    return CreateTab("browser://newtab", parent_id);
  }
  const int tab_id = CreateTab(tab.url, parent_id, false, tab.pinned);
  if (!tab.favicon.empty()) {
    tab_manager_.SetFaviconUrl(tab_id, tab.favicon);
  }
  return tab_id;
}

void OtfApp::RestoreImagePreviewStateForTab(int tab_id, const WorkspaceTab& tab) {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || tab_id < 0 || !tab.is_image_preview) {
    return;
  }

  const std::string source = tab.url;
  if (source.empty()) {
    return;
  }

  tab_manager_.SetSchemeUrl(tab_id, "browser://imagepreview");
  tab_manager_.SetImagePreviewMode(tab_id, ImagePreviewMode::kDedicated);
  tab_manager_.SetTitle(tab_id, tab.title.empty() ? source : tab.title);
  tab_manager_.SetFindText(tab_id, "");

  if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0) {
    tab_manager_.SetUrl(tab_id, source);
    handler->SetImagePreviewUrlForTab(tab_id, source);
  } else {
    if (tab.preview_local_path.empty()) {
      return;
    }
    const std::string local_path =
        !tab.preview_local_path.empty() ? tab.preview_local_path : source;
    const std::string file_name = std::filesystem::path(local_path).filename().string();
    const std::string public_url = "browser://image-preview/restore/" +
                                   std::to_string(tab_id) + "/" +
                                   otf::SanitizeFilename(file_name.empty() ? "preview.tiff" : file_name);
    tab_manager_.SetUrl(tab_id, public_url);
    handler->SetImagePreviewLocalFileForTab(tab_id, public_url, local_path);
  }
  handler->SetImagePreviewPageForTab(tab_id, tab.preview_page);
}

void OtfApp::SwitchTab(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  
  if (current_tab_id_ == tab_id) return;
  
  // Clear any active find on the old tab so highlights don't linger
  if (findbar_overlay_ && findbar_overlay_->IsVisible()) {
    CefRefPtr<CefBrowser> old_browser = tab_manager_.GetBrowser(current_tab_id_);
    if (old_browser) old_browser->GetHost()->StopFinding(true);
  }

  CefRefPtr<CefBrowserView> old_view = tab_manager_.GetView(current_tab_id_);
  CefRefPtr<CefBrowserView> new_view = tab_manager_.GetView(tab_id);

  if (new_view && window_ && content_panel_) {
    if (!new_view->GetParentView()) {
      content_panel_->AddChildView(new_view);
    }
    if (old_view) old_view->SetVisible(false);
    new_view->SetVisible(true);
    new_view->RequestFocus();
    content_panel_->InvalidateLayout();
    window_->Layout();
    current_tab_id_ = tab_id;

    UpdateWindowTitle(tab_id);

    OtfHandler* handler = OtfHandler::GetInstance();
    if (handler) {
      // Apply per-origin zoom for this workspace when activating a non-private
      // tab; the browser may have a stale zoom from a different origin.
      const std::string tab_url = tab_manager_.GetUrl(tab_id);
      CefRefPtr<CefBrowser> tab_browser = tab_manager_.GetBrowser(tab_id);
      if (tab_manager_.IsPrivate(tab_id)) {
        int zoom = tab_manager_.GetZoomPercent(tab_id);
        if (otf::IsPersistableWebUrl(tab_url) &&
            !otf::IsInternalUiUrl(tab_url)) {
          const int ws = tab_manager_.GetWorkspaceId(tab_id);
          const std::string origin = otf::ExtractOrigin(tab_url);
          if (ws > 0 && !origin.empty()) {
            zoom = tab_manager_.GetPrivateOriginZoom(ws, origin);
            tab_manager_.SetZoomPercent(tab_id, zoom);
          }
        }
        if (tab_browser) {
          tab_browser->GetHost()->SetZoomLevel(otf::PercentToZoomLevel(zoom));
        }
        if (handler->zoombar_subscription_) {
          handler->zoombar_subscription_->Success(
              JsonObjectBuilder()
                  .AddString("key", "zoom-restore")
                  .AddInt("tabId", tab_id)
                  .AddInt("zoomPercent", zoom)
                  .Build());
        }
      } else if (otf::IsPersistableWebUrl(tab_url) &&
                 !otf::IsInternalUiUrl(tab_url)) {
        const int ws = tab_manager_.GetWorkspaceId(tab_id);
        if (ws > 0) {
          if (tab_browser) {
            std::string origin = otf::ExtractOrigin(tab_url);
            if (!origin.empty()) {
              int zoom = tab_manager_.GetOriginZoom(ws, origin);
              tab_browser->GetHost()->SetZoomLevel(otf::PercentToZoomLevel(zoom));
              tab_manager_.SetZoomPercent(tab_id, zoom);
              if (handler->zoombar_subscription_) {
                handler->zoombar_subscription_->Success(
                    JsonObjectBuilder()
                        .AddString("key", "zoom-restore")
                        .AddInt("tabId", tab_id)
                        .AddInt("zoomPercent", zoom)
                        .Build());
              }
            }
          }
        }
      }
      std::string tab_img_url = handler->GetImagePreviewUrlForTab(tab_id);
      const ImagePreviewMode preview_mode = tab_manager_.GetImagePreviewMode(tab_id);
      const bool is_dedicated_preview_tab =
          preview_mode == ImagePreviewMode::kDedicated;
      if (is_dedicated_preview_tab) {
        // Dedicated preview tab manages its own BrowserView; the floating
        // overlay must not double-render on top of it. Re-fire load-image to
        // that tab's per-tab subscription so its JSX restores from C++ state
        // (page index, page count) instead of whatever stale state the
        // renderer happens to hold.
        HideImagePreviewOverlay();
        std::string event = handler->BuildImagePreviewLoadEvent(tab_id);
        if (!event.empty()) {
          auto it = handler->tab_image_preview_subscriptions_.find(tab_id);
          if (it != handler->tab_image_preview_subscriptions_.end() && it->second) {
            it->second->Success(event);
          }
          // Also push directly via ExecuteJavaScript. The persistent cefQuery
          // subscription can be silently cancelled by CEF when the BrowserView
          // is hidden, so a Success() call on switch-back may land on a dead
          // callback. A direct JS push survives that — applyLoadImage is
          // exposed on window by the renderer.
          CefRefPtr<CefBrowser> b = new_view->GetBrowser();
          CefRefPtr<CefFrame> frame = b ? b->GetMainFrame() : nullptr;
          if (frame) {
            std::string js =
                "if(window.__otfApplyImagePreview)window.__otfApplyImagePreview("
                + event + ");";
            frame->ExecuteJavaScript(js, frame->GetURL(), 0);
          }
        }
      } else {
        if (preview_mode != ImagePreviewMode::kInline || tab_img_url.empty()) {
          handler->ClearInlineImagePreviewForTab(tab_id);
          HideImagePreviewOverlay();
          if (handler->image_preview_subscription_) {
            std::string event = JsonObjectBuilder()
              .AddString("key", "load-image")
              .AddString("url", "")
              .Build();
            handler->image_preview_subscription_->Success(event);
          }
          CefRefPtr<CefBrowserView> preview_view =
              image_preview_overlay_ ? image_preview_overlay_->GetContentsView()->AsBrowserView() : nullptr;
          CefRefPtr<CefBrowser> b = preview_view ? preview_view->GetBrowser() : nullptr;
          CefRefPtr<CefFrame> frame = b ? b->GetMainFrame() : nullptr;
          if (frame) {
            std::string js =
                "if(window.__otfApplyImagePreview)window.__otfApplyImagePreview("
                + JsonObjectBuilder().AddString("key", "load-image").AddString("url", "").Build()
                + ");";
            frame->ExecuteJavaScript(js, frame->GetURL(), 0);
          }
        } else {
          ShowImagePreviewOverlay();
          std::string event = handler->BuildImagePreviewLoadEvent(tab_id);
          if (!event.empty()) {
            if (handler->image_preview_subscription_) {
              handler->image_preview_subscription_->Success(event);
            }
            // Also push directly to the image preview overlay via ExecuteJavaScript.
            // This guarantees the UI recovers and renders the TIFF even if the persistent
            // cefQuery subscription was cancelled while the overlay was hidden.
            CefRefPtr<CefBrowserView> preview_view =
                image_preview_overlay_ ? image_preview_overlay_->GetContentsView()->AsBrowserView() : nullptr;
            CefRefPtr<CefBrowser> b = preview_view ? preview_view->GetBrowser() : nullptr;
            CefRefPtr<CefFrame> frame = b ? b->GetMainFrame() : nullptr;
            if (frame) {
              std::string js =
                  "if(window.__otfApplyImagePreview)window.__otfApplyImagePreview("
                  + event + ");";
              frame->ExecuteJavaScript(js, frame->GetURL(), 0);
            }
          }
        }
      }

      std::string tab_doc_url = handler->GetDocPreviewUrlForTab(tab_id);
      const DocPreviewMode doc_preview_mode = tab_manager_.GetDocPreviewMode(tab_id);
      const bool is_dedicated_doc_preview_tab =
          doc_preview_mode == DocPreviewMode::kDedicated;
      if (is_dedicated_doc_preview_tab) {
        HideDocPreviewOverlay();
        std::string event = handler->BuildDocPreviewLoadEvent(tab_id);
        if (!event.empty()) {
          auto it = handler->tab_doc_preview_subscriptions_.find(tab_id);
          if (it != handler->tab_doc_preview_subscriptions_.end() && it->second) {
            it->second->Success(event);
          }
          CefRefPtr<CefBrowser> b = new_view->GetBrowser();
          CefRefPtr<CefFrame> frame = b ? b->GetMainFrame() : nullptr;
          if (frame) {
            std::string js =
                "if(window.__otfApplyDocPreview)window.__otfApplyDocPreview("
                + event + ");";
            frame->ExecuteJavaScript(js, frame->GetURL(), 0);
          }
        }
      } else {
        if (doc_preview_mode != DocPreviewMode::kInline || tab_doc_url.empty()) {
          handler->ClearDocPreviewStateForTab(tab_id);
          HideDocPreviewOverlay();
          if (handler->doc_preview_subscription_) {
            std::string event = JsonObjectBuilder()
                .AddString("key", "load-doc")
                .AddString("url", "")
                .Build();
            handler->doc_preview_subscription_->Success(event);
          }
          CefRefPtr<CefBrowserView> preview_view =
              doc_preview_overlay_ ? doc_preview_overlay_->GetContentsView()->AsBrowserView() : nullptr;
          CefRefPtr<CefBrowser> b = preview_view ? preview_view->GetBrowser() : nullptr;
          CefRefPtr<CefFrame> frame = b ? b->GetMainFrame() : nullptr;
          if (frame) {
            std::string js =
                "if(window.__otfApplyDocPreview)window.__otfApplyDocPreview("
                + JsonObjectBuilder().AddString("key", "load-doc").AddString("url", "").Build()
                + ");";
            frame->ExecuteJavaScript(js, frame->GetURL(), 0);
          }
        } else {
          ShowDocPreviewOverlay();
          std::string event = handler->BuildDocPreviewLoadEvent(tab_id);
          if (!event.empty()) {
            if (handler->doc_preview_subscription_) {
              handler->doc_preview_subscription_->Success(event);
            }
            CefRefPtr<CefBrowserView> preview_view =
                doc_preview_overlay_ ? doc_preview_overlay_->GetContentsView()->AsBrowserView() : nullptr;
            CefRefPtr<CefBrowser> b = preview_view ? preview_view->GetBrowser() : nullptr;
            CefRefPtr<CefFrame> frame = b ? b->GetMainFrame() : nullptr;
            if (frame) {
              std::string js =
                  "if(window.__otfApplyDocPreview)window.__otfApplyDocPreview("
                  + event + ");";
              frame->ExecuteJavaScript(js, frame->GetURL(), 0);
            }
          }
        }
      }
    }

    if (certificate_overlay_ && certificate_overlay_->IsVisible()) {
      DestroyCertificateOverlay();
    }

    // Hide any PopupOverlay-managed popups so they don't bleed into the
    // new tab's context. As more popups migrate to PopupOverlay, the
    // bespoke "if (X_overlay_ && IsVisible) Hide()" calls above can be
    // removed.
    HideAllPopups();

    if (handler) {
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "active-tab-changed")
                             .AddInt("id", tab_id)
                             .Build());
      handler->NotifyBookmarkStateForTab(tab_id);
      if (handler->console_subscription_) {
        handler->console_subscription_->Success(
            JsonObjectBuilder()
                .AddString("key", "console-tab-changed")
                .AddInt("tabId", tab_id)
                .Build());
      }
      // Restore per-tab console visibility
      if (console_view_) {
        const bool should_show = tab_manager_.IsConsoleVisible(tab_id);
        const bool is_shown = console_view_->IsVisible();
        if (should_show && !is_shown) {
          console_view_->SetVisible(true);
          content_area_panel_->Layout();
        } else if (!should_show && is_shown) {
          console_view_->SetVisible(false);
          content_area_panel_->Layout();
        }
      }
    }

    if (tab_manager_.IsFindVisible(tab_id)) {
      RestoreFindSessionForTab(tab_id, false);
      HideZoomBarOverlay();
    } else if (findbar_overlay_ && findbar_overlay_->IsVisible()) {
      findbar_overlay_->SetVisible(false);
      if (handler && handler->findbar_subscription_) {
        handler->findbar_subscription_->Success(
            JsonObjectBuilder()
                .AddString("key", "findbar-closed")
                .AddInt("tabId", tab_id)
                .Build());
        handler->findbar_subscription_->Success(
            JsonObjectBuilder()
                .AddString("key", "find-result")
                .AddInt("count", 0)
                .AddInt("active", 0)
                .AddInt("tabId", -1)
                .AddString("text", "")
                .AddBool("final", true)
                .Build());
      }
    }
    HideZoomBarOverlay();
    return;
  }
}

int OtfApp::CloseTab(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  if (tab_manager_.IsPinned(tab_id)) return -1;
  OtfHandler* handler = OtfHandler::GetInstance();
  const int ws_id = tab_manager_.GetWorkspaceId(tab_id);
  const std::vector<int> ws_tab_ids = tab_manager_.GetTabIdsForWorkspace(ws_id);
  int next_active_tab_id = -1;
  if (tab_id == current_tab_id_) {
    next_active_tab_id = SelectNextActiveTabId(ws_tab_ids, tab_id);
  }

  CefRefPtr<CefBrowserView> view = tab_manager_.GetView(tab_id);
  if (view && content_panel_) {
    content_panel_->RemoveChildView(view);
    content_panel_->Layout();
  }
  tab_manager_.RemoveTab(tab_id);
  if (handler) {
    handler->tab_image_preview_subscriptions_.erase(tab_id);
    handler->SetImagePreviewUrlForTab(tab_id, "");
    handler->tab_doc_preview_subscriptions_.erase(tab_id);
    handler->ClearDocPreviewStateForTab(tab_id);
    handler->UnmarkTabJsDisabled(tab_id);
    handler->MaybeReleasePrivateContext();
  }
  if (current_tab_id_ == tab_id) {
    current_tab_id_ = -1;
  }

  if (handler && handler->IsGuestSessionActive() && ws_id == 0 &&
      tab_manager_.GetTabIdsForWorkspace(0).empty()) {
    handler->EndGuestSession(/*restore_normal_tabs=*/false);
    if (window_) {
      window_->Close();
    }
    return -1;
  }

  if (next_active_tab_id >= 0) {
    SwitchTab(next_active_tab_id);
  }

  // Close the window if no tabs are left
  if (tab_manager_.GetAllTabIds().empty() && window_) {
    window_->Close();
  }
  return next_active_tab_id;
}

void OtfApp::UpdateWindowTitle(int tab_id) {
  if (tab_id == current_tab_id_ && window_) {
    window_->SetTitle(tab_manager_.GetTitle(tab_id));
  }
}

void OtfApp::CreateFindBarOverlay() {
  if (!window_) return;
  std::string url = "browser://findbar";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/findbar.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 84));
  view->SetID(kFindBarBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  findbar_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionFindBarOverlay();
}

void OtfApp::CreateZoomBarOverlay() {
  if (!window_) return;
  std::string url = "browser://zoombar";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/zoombar.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 40));
  view->SetID(kZoomBarBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  zoombar_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionZoomBarOverlay();
}

void OtfApp::CreateToastOverlay() {
  if (!window_) return;
  std::string url = "browser://toast";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/toast.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 32));
  view->SetID(kToastNotificationBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  toast_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, false);
  toast_overlay_->SetVisible(false);
  PositionToastOverlay();
}

namespace {

class HideToastTask : public CefTask {
 public:
  HideToastTask(OtfApp* app, int gen) : app_(app), gen_(gen) {}
  void Execute() override {
    // Only hide if no newer toast was requested (gen changed).
    if (app_ && app_->toast_gen_ == gen_) {
      app_->HideToastOverlay();
    }
  }
 private:
  OtfApp* app_;
  int gen_;
  IMPLEMENT_REFCOUNTING(HideToastTask);
};

}  // namespace

void OtfApp::ShowToast(const std::string& icon, const std::string& message) {
  CEF_REQUIRE_UI_THREAD();
  if (!toast_overlay_) return;
  PositionToastOverlay();
  toast_overlay_->SetVisible(true);
  CefRefPtr<CefBrowser> browser = OtfHandler::GetInstance()->toast_browser_;
  if (browser) {
    std::string safe_icon = icon;
    std::string safe_msg = message;
    for (size_t i = 0; (i = safe_icon.find('\'', i)) != std::string::npos; i += 2)
      safe_icon.replace(i, 1, "\\'");
    for (size_t i = 0; (i = safe_msg.find('\\', i)) != std::string::npos; i += 2)
      safe_msg.replace(i, 1, "\\\\");
    for (size_t i = 0; (i = safe_msg.find('\'', i)) != std::string::npos; i += 2)
      safe_msg.replace(i, 1, "\\'");
    const std::string js = "window.__otfSetToastMessage('" + safe_icon + "', '" + safe_msg + "');";
    browser->GetMainFrame()->ExecuteJavaScript(js, "", 0);
  }
  ++toast_gen_;
  CefPostDelayedTask(TID_UI, new HideToastTask(this, toast_gen_), 2200);
}

void OtfApp::HideToastOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (toast_overlay_) {
    toast_overlay_->SetVisible(false);
  }
}

void OtfApp::PositionToastOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !toast_overlay_ || !content_panel_ || !content_area_panel_) return;
  constexpr int kOverlayHeight = 32;
  CefRect area = content_area_panel_->GetBounds();
  CefRect panel = content_panel_->GetBounds();
  int x = area.x + panel.x;
  int y = area.y + panel.y + 4;
  int width = panel.width;
  toast_overlay_->SetBounds(CefRect(x, y, width, kOverlayHeight));
}

void OtfApp::CreateLinkPreviewOverlay() {
  if (!window_) return;
  std::string url = "browser://linkpreview";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/linkpreview.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 28));
  view->SetID(kLinkPreviewBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  link_preview_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, false);
  link_preview_overlay_->SetVisible(false);
  PositionLinkPreviewOverlay();
}

void OtfApp::SetLinkPreviewVisible(bool visible) {
  CEF_REQUIRE_UI_THREAD();
  if (link_preview_overlay_)
    link_preview_overlay_->SetVisible(visible);
}

void OtfApp::PositionLinkPreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !link_preview_overlay_ || !content_panel_ || !content_area_panel_) return;

  constexpr int kOverlayHeight = 28;
  // content_panel_ is inside content_area_panel_; GetBounds() is parent-relative.
  // Add content_area_panel_'s window-relative offset to get window-relative bounds.
  CefRect area = content_area_panel_->GetBounds();
  CefRect panel = content_panel_->GetBounds();
  CefRect bounds = {area.x + panel.x, area.y + panel.y, panel.width, panel.height};
  const int overlayWidth = bounds.width * 3 / 4;
  link_preview_overlay_->SetBounds(
      CefRect(bounds.x, bounds.y + bounds.height - kOverlayHeight, overlayWidth, kOverlayHeight));
}

void OtfApp::CreateDownloadsOverlay() {
  if (!window_) return;
  std::string url = "browser://downloadsbar";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/downloadsbar.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 380));
  view->SetID(kDownloadsBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  downloads_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionDownloadsOverlay();
}

void OtfApp::CreateCertificateOverlay() {
  if (!window_) return;
  std::string url = "browser://certificate";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/certificate.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 420));
  view->SetID(kCertificateBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  certificate_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionCertificateOverlay();
}

void OtfApp::CreateAppMenuOverlay() {
  if (!window_) return;
  std::string url = "browser://appmenu";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/appmenu.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 140));
  view->SetID(kAppMenuBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  appmenu_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionAppMenuOverlay();
}

void OtfApp::FocusCurrentTabContent() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser = tab_manager_.GetBrowser(current_tab_id_);
  if (browser) {
    browser->GetHost()->SetFocus(true);
  }
}

void OtfApp::RestoreFindSessionForTab(int tab_id, bool focus_findbar) {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !findbar_overlay_) return;

  PositionFindBarOverlay();
  findbar_overlay_->SetVisible(true);
  if (handler->findbar_subscription_) {
    handler->findbar_subscription_->Success(
        JsonObjectBuilder()
            .AddString("key", "find-restore")
            .AddInt("tabId", tab_id)
            .AddString("text", tab_manager_.GetFindText(tab_id))
            .AddBool("matchCase", tab_manager_.GetFindCase(tab_id))
            .Build());
  }

  if (focus_findbar && handler->findbar_browser_) {
    handler->findbar_browser_->GetMainFrame()->ExecuteJavaScript(
        "(function(){try{var e=document.querySelector('input[type=text]');if(e){e.focus();e.select();}}catch(_){}})();",
        CefString(), 0);
    handler->findbar_browser_->GetHost()->SetFocus(true);
  }

  std::string text = tab_manager_.GetFindText(tab_id);
  if (text.empty()) return;

  CefRefPtr<CefBrowser> browser = tab_manager_.GetBrowser(tab_id);
  if (!browser) return;

  handler->pending_find_tab_ = tab_id;
  handler->pending_find_text_ = text;
  handler->restore_find_target_ordinal_ = tab_manager_.GetFindActive(tab_id);
  handler->restore_find_in_progress_ =
      handler->restore_find_target_ordinal_ > 1;
  browser->GetHost()->Find(text, true, tab_manager_.GetFindCase(tab_id), false);
}

void OtfApp::PositionFindBarOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !findbar_overlay_) return;

  constexpr int kOverlayWidth = 380;
  constexpr int kOverlayHeight = 84;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayRightMargin = 20;

  CefRect bounds = window_->GetBounds();
  int x = std::max(0, bounds.width - kOverlayWidth - kOverlayRightMargin);
  findbar_overlay_->SetBounds(
      CefRect(x, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::PositionZoomBarOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !zoombar_overlay_) return;

  constexpr int kOverlayWidth = 186;
  constexpr int kOverlayHeight = 40;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayRightMargin = 54;

  CefRect bounds = window_->GetBounds();
  int x = std::max(0, bounds.width - kOverlayWidth - kOverlayRightMargin);
  zoombar_overlay_->SetBounds(
      CefRect(x, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::ShowZoomBarOverlay() {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !zoombar_overlay_) return;

  PositionZoomBarOverlay();
  zoombar_overlay_->SetVisible(true);
  if (handler->zoombar_subscription_) {
    handler->zoombar_subscription_->Success(
        JsonObjectBuilder()
            .AddString("key", "zoom-restore")
            .AddInt("tabId", current_tab_id_)
            .AddInt("zoomPercent", tab_manager_.GetZoomPercent(current_tab_id_))
            .Build());
  }
  if (zoombar_overlay_->GetContentsView()) {
    zoombar_overlay_->GetContentsView()->RequestFocus();
  }
}

void OtfApp::HideZoomBarOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (zoombar_overlay_) {
    zoombar_overlay_->SetVisible(false);
  }
  FocusCurrentTabContent();
}

void OtfApp::PositionDownloadsOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !downloads_overlay_) return;

  constexpr int kOverlayWidth = 420;
  constexpr int kOverlayHeight = 360;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayRightMargin = 18;

  CefRect bounds = window_->GetBounds();
  int x = std::max(0, bounds.width - kOverlayWidth - kOverlayRightMargin);
  downloads_overlay_->SetBounds(
      CefRect(x, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::PositionCertificateOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !certificate_overlay_) return;

  constexpr int kOverlayWidth = 420;
  constexpr int kOverlayHeight = 420;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayLeftMargin = 120;

  certificate_overlay_->SetBounds(
      CefRect(kOverlayLeftMargin, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::ShowDownloadsOverlay() {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !downloads_overlay_) return;

  PositionDownloadsOverlay();
  downloads_overlay_->SetVisible(true);
  if (handler->downloads_subscription_) {
    handler->downloads_subscription_->Success(
        JsonObjectBuilder().AddString("key", "downloads-refresh").Build());
  }
  if (downloads_overlay_->GetContentsView()) {
    downloads_overlay_->GetContentsView()->RequestFocus();
  }
}

void OtfApp::HideDownloadsOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (downloads_overlay_) {
    downloads_overlay_->SetVisible(false);
  }
  FocusCurrentTabContent();
}

void OtfApp::ShowCertificateOverlay() {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler) return;

  if (!certificate_overlay_) {
    CreateCertificateOverlay();
  }
  if (!certificate_overlay_) return;

  PositionCertificateOverlay();
  RefreshCertificateOverlay();
  certificate_overlay_->SetVisible(true);
  if (certificate_overlay_->GetContentsView()) {
    certificate_overlay_->GetContentsView()->RequestFocus();
  }
  if (handler->certificate_browser_) {
    handler->certificate_browser_->GetHost()->SetFocus(true);
  }
}

void OtfApp::HideCertificateOverlay() {
  CEF_REQUIRE_UI_THREAD();
  DestroyCertificateOverlay();
  FocusCurrentTabContent();
}

void OtfApp::RefreshCertificateOverlay() {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !handler->certificate_subscription_ || current_tab_id_ < 0) {
    return;
  }

  handler->certificate_subscription_->Success(
      JsonObjectBuilder()
          .AddString("key", "certificate-restore")
          .AddInt("tabId", current_tab_id_)
          .Build());
}

void OtfApp::DestroyCertificateOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (certificate_overlay_) {
    certificate_overlay_->Destroy();
    certificate_overlay_ = nullptr;
  }
}

void OtfApp::PositionAppMenuOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !appmenu_overlay_) return;

  constexpr int kOverlayWidth = 320;
  constexpr int kOverlayHeight = 480;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayRightMargin = 16;

  CefRect bounds = window_->GetBounds();
  int x = std::max(0, bounds.width - kOverlayWidth - kOverlayRightMargin);
  appmenu_overlay_->SetBounds(
      CefRect(x, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::ShowAppMenuOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!appmenu_overlay_) return;

  PositionAppMenuOverlay();
  appmenu_overlay_->SetVisible(true);
  if (appmenu_overlay_->GetContentsView()) {
    appmenu_overlay_->GetContentsView()->RequestFocus();
  }
}

void OtfApp::OpenPendingStartupTabs() {
  CEF_REQUIRE_UI_THREAD();
  if (startup_tabs_opened_) {
    return;
  }
  startup_tabs_opened_ = true;

  // Restore background tabs. For "continue" this is all saved tabs except
  // the was-active one (already the content view). For "newtab" this is ALL
  // saved tabs — they open behind the fresh newtab.
  if (!pending_workspace_restore_.empty()) {
    OtfHandler* h = OtfHandler::GetInstance();
    // Map each DB WorkspaceTab position to the newly created tab_id so we can
    // sort tab_order_ back into the original DB order after all tabs are created.
    // Key: DB position value, Value: live tab_id.
    std::map<int, int> db_pos_to_tab_id;

    // For "continue" mode, the was-active tab is already the content view. Record
    // its DB position so it sorts into the right slot alongside the background tabs.
    if (pending_workspace_restore_first_.id != 0) {
      db_pos_to_tab_id[pending_workspace_restore_first_.position] = GetCurrentTabId();
    }

    for (const auto& t : pending_workspace_restore_) {
      if (!IsRestorableWorkspaceTab(t)) continue;
      const int id = CreateRestoredTab(t);
      if (id >= 0) db_pos_to_tab_id[t.position] = id;
    }
    pending_workspace_restore_.clear();

    // Re-sort the workspace's slice of tab_order_ to match DB positions so the
    // tab strip preserves the original order regardless of which tab was active.
    if (h && db_pos_to_tab_id.size() > 1) {
      std::vector<int> sorted_ids;
      sorted_ids.reserve(db_pos_to_tab_id.size());
      for (auto& [_, tid] : db_pos_to_tab_id) sorted_ids.push_back(tid);
      tab_manager_.SetWorkspaceTabOrder(h->active_workspace_id_, sorted_ids);
    }

    // Guard is no longer needed — background tabs are now in the live list
    // so the next PersistWorkspaceTabs call will capture the correct state.
    if (h) h->startup_session_guard_ = false;
    return;
  }
  // "newtab" with no saved tabs: clear guard so normal persistence resumes.
  if (OtfHandler* h = OtfHandler::GetInstance()) {
    h->startup_session_guard_ = false;
  }

  if (startup_behavior_ != "specific" || startup_urls_.size() < 2) {
    return;
  }

  for (size_t i = 1; i < startup_urls_.size(); ++i) {
    CreateTab(startup_urls_[i]);
  }
}

void OtfApp::HideAppMenuOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (appmenu_overlay_) {
    appmenu_overlay_->SetVisible(false);
  }
  FocusCurrentTabContent();
}

void OtfApp::CreateBookmarkOverlay() {
  if (!window_) return;
  std::string url = "browser://bookmarkbar";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/bookmarkbar.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 200));
  view->SetID(kBookmarkBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  bookmark_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionBookmarkOverlay();
}

void OtfApp::PositionBookmarkOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !bookmark_overlay_) return;

  constexpr int kOverlayWidth = 340;
  constexpr int kOverlayHeight = 200;
  constexpr int kOverlayTop = 60;
  constexpr int kOverlayRightMargin = 80;

  CefRect bounds = window_->GetBounds();
  int x = std::max(0, bounds.width - kOverlayWidth - kOverlayRightMargin);
  bookmark_overlay_->SetBounds(
      CefRect(x, kOverlayTop, kOverlayWidth, kOverlayHeight));
}

void OtfApp::ShowBookmarkOverlay() {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !bookmark_overlay_) return;

  PositionBookmarkOverlay();
  bookmark_overlay_->SetVisible(true);
  if (handler->bookmark_subscription_) {
    handler->bookmark_subscription_->Success(
        JsonObjectBuilder().AddString("key", "bookmark-refresh").Build());
  }
  if (bookmark_overlay_->GetContentsView()) {
    bookmark_overlay_->GetContentsView()->RequestFocus();
  }
}

void OtfApp::HideBookmarkOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (bookmark_overlay_) {
    bookmark_overlay_->SetVisible(false);
  }
  FocusCurrentTabContent();
}

void OtfApp::CreateImagePreviewOverlay() {
  if (!window_) return;
  std::string url = "browser://imagepreview";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/imagepreview.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 0));
  view->SetID(kImagePreviewBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  image_preview_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionImagePreviewOverlay();
}

void OtfApp::PositionImagePreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !image_preview_overlay_ || !content_panel_ || !content_area_panel_) return;
  // content_panel_ is inside content_area_panel_; compute window-relative bounds.
  CefRect area = content_area_panel_->GetBounds();
  CefRect panel = content_panel_->GetBounds();
  CefRect bounds = {area.x + panel.x, area.y + panel.y, panel.width, panel.height};
  image_preview_overlay_->SetBounds(bounds);
}

void OtfApp::ShowImagePreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !image_preview_overlay_) return;
  
  HideAppMenuOverlay();
  HideDownloadsOverlay();
  HideCertificateOverlay();
  HideBookmarkOverlay();
  PositionImagePreviewOverlay();
  image_preview_overlay_->SetVisible(true);
  
  if (image_preview_overlay_->GetContentsView()) {
    image_preview_overlay_->GetContentsView()->RequestFocus();
  }
}

void OtfApp::HideImagePreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (image_preview_overlay_) {
    image_preview_overlay_->SetVisible(false);
    FocusCurrentTabContent();
  }
}

void OtfApp::CreateDocPreviewOverlay() {
  if (!window_) return;
  std::string url = "browser://docpreview";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/docpreview.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 0));
  view->SetID(kDocPreviewBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  doc_preview_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  PositionDocPreviewOverlay();
}

void OtfApp::PositionDocPreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !doc_preview_overlay_ || !content_panel_ || !content_area_panel_) return;
  CefRect area = content_area_panel_->GetBounds();
  CefRect panel = content_panel_->GetBounds();
  CefRect bounds = {area.x + panel.x, area.y + panel.y, panel.width, panel.height};
  doc_preview_overlay_->SetBounds(bounds);
}

void OtfApp::ShowDocPreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || !doc_preview_overlay_) return;

  HideAppMenuOverlay();
  HideDownloadsOverlay();
  HideCertificateOverlay();
  HideBookmarkOverlay();
  PositionDocPreviewOverlay();

  // If the overlay previously navigated to a PDF content URL, reload it
  // back to the doc preview UI so React can mount and receive events.
  if (doc_preview_overlay_->GetContentsView()) {
    CefRefPtr<CefBrowserView> preview_view =
        doc_preview_overlay_->GetContentsView()->AsBrowserView();
    if (preview_view) {
      CefRefPtr<CefBrowser> browser = preview_view->GetBrowser();
      if (browser) {
        std::string url = browser->GetMainFrame()->GetURL().ToString();
        if (url != "browser://docpreview" &&
            url.find("/docpreview.html") == std::string::npos &&
            url.rfind("browser://doc-preview/", 0) != 0) {
          browser->GetMainFrame()->LoadURL("browser://docpreview");
        }
      }
    }
  }

  doc_preview_overlay_->SetVisible(true);

  if (doc_preview_overlay_->GetContentsView()) {
    doc_preview_overlay_->GetContentsView()->RequestFocus();
  }
}

void OtfApp::HideDocPreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (doc_preview_overlay_) {
    doc_preview_overlay_->SetVisible(false);
    FocusCurrentTabContent();
  }
}

void OtfApp::RestoreDocPreviewStateForTab(int tab_id, const WorkspaceTab& tab) {
  CEF_REQUIRE_UI_THREAD();
  OtfHandler* handler = OtfHandler::GetInstance();
  if (!handler || tab_id < 0 || !tab.is_doc_preview) {
    return;
  }

  const std::string source = tab.url;
  if (source.empty()) {
    return;
  }

  tab_manager_.SetSchemeUrl(tab_id, "browser://docpreview");
  tab_manager_.SetDocPreviewMode(tab_id, DocPreviewMode::kDedicated);
  tab_manager_.SetTitle(tab_id, tab.title.empty() ? source : tab.title);
  tab_manager_.SetFindText(tab_id, "");

  if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0) {
    tab_manager_.SetUrl(tab_id, source);
    handler->SetDocPreviewUrlForTab(tab_id, source);
  } else {
    if (tab.preview_local_path.empty()) {
      return;
    }
    const std::string local_path =
        !tab.preview_local_path.empty() ? tab.preview_local_path : source;
    const std::string file_name = std::filesystem::path(local_path).filename().string();
    const std::string safe_name = otf::SanitizeFilename(file_name.empty() ? "document.txt" : file_name);
    const std::string content_token = "restore/" + std::to_string(tab_id) + "/" + safe_name;
    const std::string content_url = "browser://doc-preview/content/" + content_token;
    const std::string public_url = "browser://doc-preview/restore/" + content_token;
    otf::RegisterDocContent(content_token, local_path);
    tab_manager_.SetUrl(tab_id, public_url);
    handler->SetDocPreviewLocalFileForTab(tab_id, public_url, local_path);
    handler->SetDocPreviewContentUrlForTab(tab_id, content_url);
  }
}

void OtfApp::CreateConsoleOverlay() {
  if (!content_area_panel_ || !content_area_layout_) return;
  std::string url = "browser://console";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/console.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 15, 23, 42);
  auto* delegate = new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 0, console_width_);
  console_delegate_ = static_cast<void*>(delegate);
  console_view_ = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      delegate);
  console_view_->SetID(kConsoleBrowserViewId);
  content_area_panel_->AddChildView(console_view_);
  content_area_layout_->SetFlexForView(console_view_.get(), 0);
  console_view_->SetVisible(false);
}

void OtfApp::SetConsoleWidth(int w) {
  CEF_REQUIRE_UI_THREAD();
  w = std::max(240, std::min(w, 900));
  console_width_ = w;
  if (console_delegate_)
    static_cast<OtfViewDelegate*>(console_delegate_)->SetFixedWidth(w);
  if (!content_area_panel_) return;
  // InvalidateLayout() marks the panel dirty so the subsequent Layout() call
  // is not a no-op. Layout() then recomputes bounds for both children in one
  // atomic pass via the BoxLayout, which schedules a single repaint for the
  // whole panel — avoiding the frame-split flicker that two SetBounds() calls
  // produce (each triggers its own OS-level repaint event).
  content_area_panel_->InvalidateLayout();
  content_area_panel_->Layout();
}

void OtfApp::PositionConsoleOverlay() {
  // Layout is managed by content_area_layout_ automatically on resize.
}

void OtfApp::ShowConsoleOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!console_view_ || !content_area_panel_) return;
  if (current_tab_id_ >= 0) tab_manager_.SetConsoleVisible(current_tab_id_, true);
  console_view_->SetVisible(true);
  content_area_panel_->Layout();
  console_view_->RequestFocus();
}

void OtfApp::HideConsoleOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!console_view_ || !content_area_panel_) return;
  if (current_tab_id_ >= 0) tab_manager_.SetConsoleVisible(current_tab_id_, false);
  console_view_->SetVisible(false);
  content_area_panel_->Layout();
  FocusCurrentTabContent();
}

void OtfApp::ToggleConsoleOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!console_view_) return;
  if (console_view_->IsVisible()) {
    HideConsoleOverlay();
  } else {
    ShowConsoleOverlay();
  }
}

void OtfApp::CreateSnipPreviewOverlay() {
  if (!window_) return;
  std::string url = "browser://snipperview";
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd->HasSwitch("dev-ui-url")) {
    url = cmd->GetSwitchValue("dev-ui-url").ToString() + "/snipperview.html";
  }
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 0));
  view->SetID(kSnipPreviewBrowserViewId);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  snip_preview_overlay_ = window_->AddOverlayView(
      view, CEF_DOCKING_MODE_CUSTOM, true);
  snip_preview_overlay_->SetVisible(false);
  PositionSnipPreviewOverlay();
}

void OtfApp::PositionSnipPreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !snip_preview_overlay_) return;
  CefRect window_bounds = window_->GetBounds();
  snip_preview_overlay_->SetBounds(CefRect(0, 0, window_bounds.width, window_bounds.height));
}

void OtfApp::ShowSnipPreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (!snip_preview_overlay_) return;
  PositionSnipPreviewOverlay();
  snip_preview_overlay_->SetVisible(true);
  OtfHandler* handler = OtfHandler::GetInstance();
  if (handler && handler->snip_preview_browser_) {
    handler->snip_preview_browser_->GetHost()->SetFocus(true);
  }
}

void OtfApp::HideSnipPreviewOverlay() {
  CEF_REQUIRE_UI_THREAD();
  if (snip_preview_overlay_) {
    snip_preview_overlay_->SetVisible(false);
  }
}

void OtfApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  LOG(INFO) << "[otf] ctx 1: OnContextInitialized begin";
  otf::DiagLog("ctx 1: OnContextInitialized begin");

  // Resolve the screen-fingerprint profile once, here in the main process,
  // where filesystem access and CefDisplay are both available. The value
  // is attached to every CefBrowserView via extra_info (see
  // MakeBrowserExtraInfo) so the sandboxed renderer can read it through
  // OnBrowserCreated rather than touching the filesystem itself — that
  // path used to throw std::filesystem_error EPERM in the renderer and
  // abort it on startup.
  screen_profile_json_ = otf::ResolveScreenProfileJson();
  LOG(INFO) << "[otf] ctx 2: screen profile resolved ("
            << screen_profile_json_.size() << " bytes)";

  // Register popups backed by the shared PopupOverlay framework. Each
  // entry: name + browser view id + size. The popup's content URL is
  // derived from the name (browser://<name> in prod, dev server in dev).
  // RestoreProducer fields are wired by OtfHandler when it has the
  // payload to ship — e.g. show-clear-site-data:<origin> stores the
  // origin in handler state, and cleardata's producer reads it back.
  popups_["cleardata"] = std::make_unique<PopupOverlay>(
      "cleardata", kClearSiteDataBrowserViewId, /*width=*/340, /*height=*/460);
  popups_["workspace"] = std::make_unique<PopupOverlay>(
      "workspace", kWorkspaceBrowserViewId,
      /*width=*/240, /*height=*/300,
      /*top_margin=*/32, /*right_margin=*/0, /*left_margin=*/8);
  popups_["qr"] = std::make_unique<PopupOverlay>(
      "qr", kQrBrowserViewId, /*width=*/300, /*height=*/490,
      /*top_margin=*/60, /*right_margin=*/150);
  popups_["blockedpopup"] = std::make_unique<PopupOverlay>(
      "blockedpopup", kBlockedPopupBrowserViewId, /*width=*/380, /*height=*/210,
      /*top_margin=*/66, /*right_margin=*/18);
  popups_["downloadrequest"] = std::make_unique<PopupOverlay>(
      "downloadrequest", kDownloadRequestBrowserViewId, /*width=*/400, /*height=*/240,
      /*top_margin=*/66, /*right_margin=*/18);

  LOG(INFO) << "[otf] ctx 3: " << popups_.size() << " popups registered";

  CefRegisterSchemeHandlerFactory("browser", "", new BrowserSchemeHandlerFactory());
  LOG(INFO) << "[otf] ctx 4: browser:// scheme handler factory registered";

  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();

  cef_runtime_style_t runtime_style = CEF_RUNTIME_STYLE_ALLOY;

  CefRefPtr<OtfHandler> handler(new OtfHandler(true));
  handler->tab_manager_ = &tab_manager_;
  LOG(INFO) << "[otf] ctx 5: OtfHandler created, store="
            << (handler->store_ ? "present" : "NULL");

  // Pre-populate per-origin zoom maps from the persisted store so tabs are
  // created at the correct zoom for their workspace and origin.
  if (handler->store_) {
    for (const auto& w : handler->store_->GetWorkspaces()) {
      auto zooms = handler->store_->GetWorkspaceOriginZooms(w.id);
      if (!zooms.empty()) tab_manager_.LoadOriginZooms(w.id, zooms);
    }
  }

  CefBrowserSettings browser_settings;

  startup_behavior_ = "newtab";
  startup_urls_.clear();
  startup_tabs_opened_ = false;
  pending_workspace_restore_.clear();
  pending_workspace_restore_first_ = WorkspaceTab{};
  pending_workspace_restore_first_is_preview_ = false;
  {
    CefRefPtr<CefValue> settings_value =
        CefParseJSON(otf::LoadSettingsJson(), JSON_PARSER_ALLOW_TRAILING_COMMAS);
    if (settings_value && settings_value->GetType() == VTYPE_DICTIONARY) {
      CefRefPtr<CefDictionaryValue> dict = settings_value->GetDictionary();
      if (dict) {
        if (dict->HasKey("startupBehavior") &&
            dict->GetType("startupBehavior") == VTYPE_STRING) {
          startup_behavior_ = dict->GetString("startupBehavior");
        }
        if (dict->HasKey("startupUrls") &&
            dict->GetType("startupUrls") == VTYPE_LIST) {
          CefRefPtr<CefListValue> list = dict->GetList("startupUrls");
          for (size_t i = 0; list && i < list->GetSize(); ++i) {
            if (list->GetType(i) == VTYPE_STRING) {
              const std::string url = list->GetString(i);
              if (IsAllowedStartupUrl(url)) {
                startup_urls_.push_back(url);
              }
            }
          }
        }
      }
    }
  }

  // Dynamic UI Path: Loads from the executable's directory
  std::string ui_url = "browser://shell";
  std::string start_url = "browser://newtab";
  if (startup_behavior_ == "specific" && !startup_urls_.empty()) {
    start_url = startup_urls_.front();
  }

  if (command_line->HasSwitch("dev-ui-url")) {
    ui_url = command_line->GetSwitchValue("dev-ui-url");
  }

  // Load saved workspace tabs for "newtab" and "continue" modes.
  // "specific" always opens the configured URLs — no restore.
  if (startup_behavior_ != "specific" && handler->store_) {
    pending_workspace_restore_ =
        handler->store_->GetWorkspaceTabs(handler->active_workspace_id_);

    if (startup_behavior_ == "continue" && !pending_workspace_restore_.empty()) {
      // "continue": surface the was-active tab as the first visible tab.
      // The rest open in the background via OpenPendingStartupTabs.
      auto it = std::find_if(
          pending_workspace_restore_.begin(),
          pending_workspace_restore_.end(),
          [](const WorkspaceTab& t) {
            return t.was_active && IsRestorableWorkspaceTab(t);
          });
      if (it == pending_workspace_restore_.end()) {
        it = std::find_if(
            pending_workspace_restore_.begin(),
            pending_workspace_restore_.end(),
            [](const WorkspaceTab& t) {
              return IsRestorableWorkspaceTab(t);
            });
      }
      if (it != pending_workspace_restore_.end()) {
        pending_workspace_restore_first_ = *it;
        pending_workspace_restore_first_is_preview_ = it->is_image_preview || it->is_doc_preview;
        if (it->is_image_preview) {
          start_url =
              it->preview_local_path.empty()
                  ? "browser://imagepreview"
                  : it->url;
        } else if (it->is_doc_preview) {
          start_url =
              it->preview_local_path.empty()
                  ? "browser://docpreview"
                  : it->url;
        } else {
          start_url = it->url;
        }
        pending_workspace_restore_.erase(it);
      }
    }
    // "newtab": start_url stays as browser://newtab (the fresh active tab).
    // pending_workspace_restore_ holds ALL saved tabs — they open in the
    // background via OpenPendingStartupTabs so previous work is not lost.
    // Guard PersistWorkspaceTabs until those background tabs are created,
    // so the startup newtab cannot wipe the DB before they land.
    if (startup_behavior_ == "newtab") {
      handler->startup_session_guard_ = true;
    }
  }

  LOG(INFO) << "[otf] ctx 6: settings parsed — startup_behavior="
            << startup_behavior_ << " ui_url=" << ui_url
            << " start_url=" << start_url;
  otf::DiagLog("ctx 6: startup_behavior=" + startup_behavior_ +
               " ui_url=" + ui_url + " start_url=" + start_url);

  LOG(INFO) << "[otf] ctx 7: creating UI shell browser view (" << ui_url << ")";
  CefRefPtr<CefBrowserView> ui_view = CefBrowserView::CreateBrowserView(
      handler, ui_url, browser_settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(runtime_style, 65));
  ui_view->SetID(kUiBrowserViewId);
  LOG(INFO) << "[otf] ctx 8: UI shell browser view created="
            << (ui_view ? "OK" : "NULL") << " id=" << kUiBrowserViewId;
  otf::DiagLog(std::string("ctx 8: UI shell browser view created=") +
               (ui_view ? "OK" : "NULL"));

  bool startup_js_disabled = false;
  {
    OtfStore* store = handler->GetStore();
    std::string origin = ExtractOrigin(start_url);
    if (!handler->IsGuestSessionActive() && !origin.empty() && store &&
        store->GetSitePermission(origin, "javascript") == "block") {
      browser_settings.javascript = STATE_DISABLED;
      startup_js_disabled = true;
    }
  }
  CefRefPtr<CefRequestContext> startup_request_context =
      handler->GetActiveWorkspaceRequestContext();
  LOG(INFO) << "[otf] ctx 9: creating content browser view (" << start_url
            << "), request_context="
            << (startup_request_context ? "workspace" : "global");
  CefRefPtr<CefBrowserView> content_view = CefBrowserView::CreateBrowserView(
      handler, start_url, browser_settings, MakeBrowserExtraInfo(), startup_request_context,
      new OtfViewDelegate(runtime_style));
  LOG(INFO) << "[otf] ctx 10: content browser view created="
            << (content_view ? "OK" : "NULL");

  int tab_id = tab_manager_.AddTab(content_view);
  content_view->SetID(tab_id);
  tab_manager_.SetUrl(tab_id, start_url);
  if (start_url.rfind("browser://", 0) == 0) {
    tab_manager_.SetSchemeUrl(tab_id, start_url);
  }
  if (startup_js_disabled) {
    handler->MarkTabJsDisabled(tab_id);
  }
  tab_manager_.SetWorkspaceId(tab_id, handler->active_workspace_id_);
  if (pending_workspace_restore_first_.id != 0) {
    tab_manager_.SetPinned(tab_id, pending_workspace_restore_first_.pinned);
  }
  if (pending_workspace_restore_first_is_preview_) {
    if (pending_workspace_restore_first_.is_image_preview) {
      RestoreImagePreviewStateForTab(tab_id, pending_workspace_restore_first_);
    } else if (pending_workspace_restore_first_.is_doc_preview) {
      RestoreDocPreviewStateForTab(tab_id, pending_workspace_restore_first_);
    }
    pending_workspace_restore_first_is_preview_ = false;
  }

  if (ui_view && content_view) {
    LOG(INFO) << "[otf] ctx 11: creating top-level window";
    otf::DiagLog("ctx 11: creating top-level window");
    CefWindow::CreateTopLevelWindow(new OtfWindowDelegate(
        ui_view, content_view, runtime_style, CEF_SHOW_STATE_NORMAL));
    LOG(INFO) << "[otf] ctx 12: OnContextInitialized end "
                 "(top-level window requested)";
    otf::DiagLog("ctx 12: OnContextInitialized end (window requested)");
  } else {
    LOG(ERROR) << "[otf] ctx 11 FAILED: ui_view or content_view is NULL — "
                  "no window created (ui_view="
               << (ui_view ? "OK" : "NULL")
               << " content_view=" << (content_view ? "OK" : "NULL") << ")";
    otf::DiagLog(std::string("ctx 11 FAILED: no window created (ui_view=") +
                 (ui_view ? "OK" : "NULL") + " content_view=" +
                 (content_view ? "OK" : "NULL") + ")");
  }
}

CefRefPtr<CefClient> OtfApp::GetDefaultClient() {
  return OtfHandler::GetInstance();
}

void OtfApp::ApplyFullscreenState() {
  if (!window_) return;
  window_->SetFullscreen(fullscreen_);
  if (ui_view_) {
    ui_view_->SetVisible(!fullscreen_);
  }
  window_->Layout();
}

void OtfApp::ToggleFullscreen() {
  fullscreen_ = !fullscreen_;
  content_fullscreen_ = false;
  ApplyFullscreenState();
  if (fullscreen_) {
    ShowToast("fullscreen", "Fullscreen \xc2\xb7 Esc / F11 to exit");
  } else {
    ShowToast("fullscreen", "Exited fullscreen");
  }
}

void OtfApp::SetContentFullscreen(bool fullscreen) {
  content_fullscreen_ = fullscreen;
  if (fullscreen_ == fullscreen) return;
  fullscreen_ = fullscreen;
  ApplyFullscreenState();
  if (fullscreen) {
    ShowToast("fullscreen", "Fullscreen \xc2\xb7 Esc / F11 to exit");
  } else {
    ShowToast("fullscreen", "Exited fullscreen");
  }
}

PopupOverlay* OtfApp::GetPopup(const std::string& name) {
  auto it = popups_.find(name);
  return it == popups_.end() ? nullptr : it->second.get();
}

void OtfApp::HideAllPopups() {
  for (auto& [_, popup] : popups_) {
    if (popup && popup->IsVisible()) popup->Hide();
  }
}

void OtfApp::RegisterBrowserSchemeForContext(CefRefPtr<CefRequestContext> ctx) {
  if (ctx) {
    ctx->RegisterSchemeHandlerFactory("browser", "", new BrowserSchemeHandlerFactory());
  }
}

void OtfApp::CreateAllPopups(CefRefPtr<CefWindow> window) {
  for (auto& [_, popup] : popups_) {
    if (popup) popup->Create(window, this);
  }

  auto* ws_popup = GetPopup("workspace");
  OtfHandler* h = OtfHandler::GetInstance();
  if (ws_popup && h) {
    ws_popup->SetRestoreProducer([h]() -> std::string {
      if (!h->store_) return "[]";
      if (h->IsGuestSessionActive()) return "[]";
      const auto ws = h->store_->GetWorkspaces();
      const int active = h->active_workspace_id_;
      std::string out = "[";
      bool first = true;
      for (size_t i = 0; i < ws.size(); ++i) {
        if (!first) out += ",";
        first = false;
        out += JsonObjectBuilder()
                   .AddInt("id", ws[i].id)
                   .AddString("name", ws[i].name)
                   .AddBool("active", ws[i].id == active)
                   .AddBool("guest", false)
                   .Build();
      }
      return out + "]";
    });
  }
}

void OtfApp::RepositionAllPopups() {
  for (auto& [_, popup] : popups_) {
    if (popup) popup->Reposition();
  }
}

bool OtfApp::DispatchPopupBrowserCreated(int view_id,
                                         CefRefPtr<CefBrowser> browser) {
  for (auto& [_, popup] : popups_) {
    if (popup && popup->view_id() == view_id) {
      popup->OnBrowserCreated(browser);
      return true;
    }
  }
  return false;
}

CefRefPtr<CefBrowserView> OtfApp::BuildOverlayBrowserView(
    const std::string& url, int view_id, int height_hint) {
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      OtfHandler::GetInstance(), url, settings, MakeBrowserExtraInfo(), nullptr,
      new OtfViewDelegate(CEF_RUNTIME_STYLE_ALLOY, height_hint));
  view->SetID(view_id);
  view->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
  return view;
}

CefRefPtr<CefDictionaryValue> OtfApp::MakeBrowserExtraInfo() const {
  CefRefPtr<CefDictionaryValue> info = CefDictionaryValue::Create();
  if (!screen_profile_json_.empty()) {
    info->SetString("otf_screen_profile", screen_profile_json_);
  }
  return info;
}

void OtfApp::OnWebKitInitialized() {
  otf::DiagLog("[render] OnWebKitInitialized");
}

void OtfApp::OnBrowserCreated(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefDictionaryValue> extra_info) {
  // Runs in the renderer. Cache the screen-profile JSON the main process
  // sent so OnContextCreated can substitute it into the page-policy script
  // without doing any filesystem I/O of its own.
  otf::DiagLog(std::string("[render] OnBrowserCreated: id=") +
               (browser ? std::to_string(browser->GetIdentifier()) : "null") +
               " extra_info screen_profile=" +
               (extra_info && extra_info->HasKey("otf_screen_profile") ? "present" : "absent"));
  if (extra_info && extra_info->HasKey("otf_screen_profile")) {
    screen_profile_json_ = extra_info->GetString("otf_screen_profile").ToString();
  }
}

void OtfApp::OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) {
  otf::DiagLog(std::string("[render] OnBrowserDestroyed: id=") +
               (browser ? std::to_string(browser->GetIdentifier()) : "null"));
}

void OtfApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context) {
  otf::DiagLog(std::string("[render] OnContextCreated: url=") +
               frame->GetURL().ToString() +
               (frame->IsMain() ? " [main frame]" : " [subframe]") +
               " inject=" + (ShouldInjectPagePolicyForFrame(frame) ? "yes" : "no"));
  if (!renderer_side_router_) {
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    renderer_side_router_ = CefMessageRouterRendererSide::Create(config);
  }
  if (ShouldInjectPagePolicyForFrame(frame)) {
    CefRefPtr<CefV8Value> retval;
    CefRefPtr<CefV8Exception> exception;
    // screen_profile_json_ was populated by OnBrowserCreated above; if it's
    // somehow empty (extra_info missing — shouldn't happen given the main
    // process always sends it), BuildPagePolicyScript will substitute a
    // hard-coded fallback profile so the JS remains valid.
    const std::string policy_script =
        otf::BuildPagePolicyScript(screen_profile_json_);
    const std::string frame_url = frame->GetURL().ToString();
    if (context && context->Enter()) {
      context->Eval(policy_script, frame_url, 0, retval, exception);
      context->Exit();
      if (exception) {
        otf::DiagLog(std::string("[render] page-policy injection EXCEPTION: ") +
                     exception->GetMessage().ToString() + " @ " + frame_url);
      } else {
        otf::DiagLog("[render] page-policy injected OK @ " + frame_url);
      }
    } else {
      otf::DiagLog("[render] context->Enter() FAILED @ " + frame_url +
                   " — falling back to ExecuteJavaScript");
      frame->ExecuteJavaScript(policy_script, frame_url, 0);
    }
  }
  // CefMessageRouterRendererSide injects the `cefQuery` JS function into
  // every frame's V8 context. In a sandboxed iframe without 'allow-scripts'
  // that injection is blocked by Chromium and logged as a CSP/sandbox
  // violation — a detectable signal for anti-bot systems (e.g. Cloudflare
  // Turnstile's pristine-realm probe iframe). cefQuery is only used by our
  // browser:// UI surfaces, never by content frames at about:blank / data:
  // / blob:, so skipping the router setup for inherited-URL frames is safe.
  const std::string frame_url = frame->GetURL().ToString();
  if (!IsInheritedFrameUrl(frame_url)) {
    renderer_side_router_->OnContextCreated(browser, frame, context);
  }
}

void OtfApp::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefV8Context> context) {
  // Mirror the skip in OnContextCreated — if we never registered the router
  // for this frame, releasing it would be a no-op at best and a bookkeeping
  // mismatch at worst.
  const std::string frame_url = frame->GetURL().ToString();
  if (!IsInheritedFrameUrl(frame_url)) {
    renderer_side_router_->OnContextReleased(browser, frame, context);
  }
}

void OtfApp::OnUncaughtException(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefV8Context> context,
                                 CefRefPtr<CefV8Exception> exception,
                                 CefRefPtr<CefV8StackTrace> stackTrace) {
  const std::string frame_url =
      frame ? frame->GetURL().ToString() : std::string();
  std::string message = exception ? exception->GetMessage().ToString()
                                  : std::string("unknown exception");
  int line = exception ? exception->GetLineNumber() : 0;
  otf::DiagLog("[render] OnUncaughtException: " + message + " @ " +
               frame_url + ":" + std::to_string(line));
}

bool OtfApp::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  return renderer_side_router_->OnProcessMessageReceived(browser, frame,
                                                        source_process, message);
}

} // namespace otf
