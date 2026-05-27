#ifndef OTF_BROWSER_UTILS_H_
#define OTF_BROWSER_UTILS_H_

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct CustomSearchEngine {
  std::string id;
  std::string name;
  std::string url;  // URL template with %s as the query placeholder
};

namespace otf {

// Convert a UTF-8 encoded std::string to std::filesystem::path.
// On Windows, std::filesystem::path constructed from std::string uses the
// system ANSI code page, silently misinterpreting non-ASCII paths. Going
// through char8_t forces correct UTF-8 interpretation on all platforms.
inline std::filesystem::path Utf8Path(const std::string& s) {
  return std::filesystem::path(reinterpret_cast<const char8_t*>(s.c_str()));
}

// Convert a std::filesystem::path to a UTF-8 encoded std::string.
// Uses the generic (forward-slash) format so the result is consistent across
// platforms and safe to concatenate with "/".
inline std::string PathToUtf8(const std::filesystem::path& p) {
  const std::u8string u8 = p.generic_u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Read entire file as binary bytes. Returns nullopt if the file cannot be
// opened or does not exist.
std::optional<std::vector<uint8_t>> ReadFileBinary(const std::string& utf8_path);

// Read entire file as a UTF-8 text string. Returns empty string on failure.
std::string ReadFileText(const std::string& utf8_path);

// Write text to a file, replacing existing contents. Returns true on success.
bool WriteFileText(const std::string& utf8_path, const std::string& content);

std::string ExtractOrigin(const std::string& url);
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

// Parse one `<byte-length>:<payload>` field from `input`, advancing `cursor`
// past the payload. Used for cefQuery commands that need unambiguous embedded
// string fields.
std::string ParseLengthPrefixedField(const std::string& input,
                                     size_t* cursor,
                                     bool* ok);

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

// Platform-correct app data directory (settings, DB, fingerprint profile).
// Windows: %APPDATA%\OTF Browser[\Dev]\
// macOS:   ~/Library/Application Support/OTF Browser[\Dev]/
// Linux:   $XDG_DATA_HOME/otf-browser[-dev]/ (~/.local/share/ fallback)
// Tests:   $OTF_TEST_HOME/otf-browser[-dev]/
std::filesystem::path GetAppDataDir();

// Platform-correct cache directory (CEF HTTP cache, workspace contexts).
// Windows: %LOCALAPPDATA%\OTF Browser[\Dev]\Cache\
// macOS:   ~/Library/Caches/OTF Browser[\Dev]/
// Linux:   $XDG_CACHE_HOME/otf-browser[-dev]/ (~/.cache/ fallback)
// Tests:   $OTF_TEST_HOME/otf-browser[-dev]/cache/
std::filesystem::path GetAppCacheDir();

std::string GetSettingsFilePath();
std::string GetDownloadsDir();
std::string SanitizeFilename(const std::string& filename);
std::string BuildDownloadPath(const std::string& suggested_name);
std::string GetDefaultSettingsJson();
std::optional<std::string> GetCurrentSearchEngineId();
bool IsHistoryEnabled();
bool IsDownloadsEnabled();
bool IsAllowedSearchEngineId(const std::string& search_engine_id);
bool IsAllowedSearchEngineId(const std::string& search_engine_id,
                             const std::vector<CustomSearchEngine>& custom_engines);
std::vector<CustomSearchEngine> GetCustomSearchEngines();
std::string BuildCustomEnginesJson(const std::vector<CustomSearchEngine>& engines);
std::string BuildSearchUrl(const std::string& search_engine_id,
                           const std::string& query);
std::string BuildSearchUrl(const std::string& search_engine_id,
                           const std::string& query,
                           const std::vector<CustomSearchEngine>& custom_engines);
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
