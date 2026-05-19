#ifndef OTF_BROWSER_UTILS_H_
#define OTF_BROWSER_UTILS_H_

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace otf {

std::string JsonEscape(const std::string& s);
std::string JsonString(const std::string& s);

// Escape a string for safe inclusion inside an HTML attribute value
// (escapes & < > " ' as named entities).
std::string HtmlAttrEscape(const std::string& s);

// Strict integer parsers — reject empty input, leading/trailing junk, and
// out-of-range values. Unlike std::stoi/std::stoul these never throw, which
// matters when parsing untrusted strings from the cefQuery bridge.
std::optional<int> ParseIntStrict(std::string_view s);
std::optional<uint32_t> ParseUint32Strict(std::string_view s);
std::optional<uint64_t> ParseUint64Strict(std::string_view s);

class JsonObjectBuilder {
 public:
  JsonObjectBuilder& AddString(const std::string& key, const std::string& value);
  JsonObjectBuilder& AddInt(const std::string& key, int value);
  JsonObjectBuilder& AddBool(const std::string& key, bool value);
  JsonObjectBuilder& AddRaw(const std::string& key, const std::string& raw_json);
  JsonObjectBuilder& AddNull(const std::string& key);
  std::string Build() const;

 private:
  std::vector<std::string> fields_;
};

std::string GetExecutableDir();
std::string GetExecutablePath();
std::string GetHomeDir();

// Name of the user-data directory under $HOME. Returns ".otf-browser-dev"
// when the OTF_DEV_MODE env var is set (main.cc sets this on seeing
// --dev-ui-url), so a developer running 'bun run dev' on the same machine
// as a packaged install gets isolated settings and SQLite data. Production
// runs always get ".otf-browser".
std::string GetUserDataDirName();

std::string GetSettingsFilePath();
std::string GetDownloadsDir();
std::string SanitizeFilename(const std::string& filename);
std::string BuildDownloadPath(const std::string& suggested_name);
std::string GetDefaultSettingsJson();
std::optional<std::string> GetCurrentSearchEngineId();
bool IsHistoryEnabled();
bool IsDownloadsEnabled();
bool IsAllowedSearchEngineId(const std::string& search_engine_id);
std::string BuildSearchUrl(const std::string& search_engine_id,
                           const std::string& query);
bool NormalizeSettingsJson(const std::string& raw_json, std::string* normalized_json);
std::string LoadSettingsJson();
bool SaveSettingsJson(const std::string& raw_json, std::string* normalized_json);

bool IsAllowedBrowserPageUrl(const std::string& url);
std::string GetBrowserPageFilePath(const std::string& url);
std::string GetBrowserPageDevUrl(const std::string& dev_ui_url,
                                 const std::string& url);
bool IsPersistableWebUrl(const std::string& url);
bool IsLocalFilesystemPathLike(const std::string& url);
bool IsAllowedHttpUrl(const std::string& url);
bool IsAllowedStartupUrl(const std::string& url);
// True when the URL is one of the bundled UI pages (security-critical):
// requires browser:// scheme. Web origins (http/https) are NEVER trusted by
// this check — dev-mode callers must validate dev-ui-url separately.
// file:// is never trusted; the app UI is served via browser://.
bool IsInternalBrowserUiUrl(const std::string& url);

// Path-suffix match against the same allowlist, ignoring scheme. Use this
// from callers that have already validated the URL's origin (e.g. checked
// the dev-ui-url prefix) and just need to know whether the path points at
// a bundled UI page.
bool IsInternalUiPagePath(const std::string& url);

// True for any URL that points at an internal UI page in *any* browser-owned
// environment (browser:// scheme or http(s):// dev-server URL).
// Use this for history/bookmark filtering — internal pages shouldn't be
// recorded regardless of how they were loaded. Do NOT use for security
// gates: those need the strict IsInternalBrowserUiUrl above.
bool IsInternalUiUrl(const std::string& url);
std::string NormalizeBookmarkUrl(const std::string& url);

int SelectNextActiveTabId(const std::vector<int>& tab_ids, int closing_tab_id);

// Zoom level utilities
// CEF uses a logarithmic zoom level where each increment of 1.0 doubles/halves the size.
// These helpers snap to standard browser zoom percentages for a predictable UX.

// Returns the standard zoom percentages supported by the browser.
const std::vector<double>& GetStandardZoomPercentages();

// Convert CEF zoom level to percentage (e.g., 0.0 → 100.0, 0.5 → ~141.0).
double ZoomLevelToPercent(double zoom_level);

// Convert percentage to CEF zoom level (e.g., 100.0 → 0.0, 150.0 → ~0.585).
double PercentToZoomLevel(double percent);

// Returns the next zoom level when zooming in, snapped to the nearest standard percentage.
double ZoomIn(double current_zoom_level);

// Returns the next zoom level when zooming out, snapped to the nearest standard percentage.
double ZoomOut(double current_zoom_level);

// Returns the default zoom level (100%).
double ZoomReset();

// True if `url`/`path` ends with .tif or .tiff (case-insensitive),
// ignoring query string and fragment.
bool IsTiffUrl(const std::string& url);
bool IsSupportedImageUrl(const std::string& url);

// Decode a local TIFF file using libvips (supporting page index) and convert it into a PNG Base64 data URL.
bool DecodeTiffToPngBase64(const std::string& tiff_path, int page, std::string& out_png_base64, int& out_page_count);

// Decode an in-memory TIFF buffer to a PNG Base64 data URL.
bool DecodeTiffBufferToPngBase64(const void* data, size_t size, int page, std::string& out_png_base64, int& out_page_count);

// Payload for the renderer's `load-image` event.
struct ImagePreviewPayload {
  std::string display_url;  // Backend-produced preview URL, usually a data: URL.
  int page_count = 1;       // total pages (>=1; 1 for non-multipage formats).
  int natural_width = 0;    // intrinsic image width in pixels, if known.
  int natural_height = 0;   // intrinsic image height in pixels, if known.
  bool show_info = true;    // per-tab visibility of the information panel.
};

// Build the renderer payload for `url` at `page` (0-based). The renderer uses
// this as a snapshot of backend-owned preview state. Remote images may come
// back with an empty display_url until the backend download/encode step
// completes. TIFFs are decoded page-by-page by the backend.
ImagePreviewPayload BuildImagePreviewPayload(const std::string& url, int page);

} // namespace otf

#endif // OTF_BROWSER_UTILS_H_
