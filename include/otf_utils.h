#ifndef OTF_BROWSER_UTILS_H_
#define OTF_BROWSER_UTILS_H_

#include <initializer_list>
#include <string>
#include <vector>

namespace otf {

std::string JsonEscape(const std::string& s);
std::string JsonString(const std::string& s);

class JsonObjectBuilder {
 public:
  JsonObjectBuilder& AddString(const std::string& key, const std::string& value);
  JsonObjectBuilder& AddInt(const std::string& key, int value);
  JsonObjectBuilder& AddBool(const std::string& key, bool value);
  JsonObjectBuilder& AddRaw(const std::string& key, const std::string& raw_json);
  std::string Build() const;

 private:
  std::vector<std::string> fields_;
};

std::string GetExecutableDir();
std::string GetHomeDir();
std::string GetSettingsFilePath();
std::string GetDownloadsDir();
std::string SanitizeFilename(const std::string& filename);
std::string BuildDownloadPath(const std::string& suggested_name);
std::string GetDefaultSettingsJson();
bool IsAllowedSearchEngineId(const std::string& search_engine_id);
bool NormalizeSettingsJson(const std::string& raw_json, std::string* normalized_json);
std::string LoadSettingsJson();
bool SaveSettingsJson(const std::string& raw_json, std::string* normalized_json);

bool IsAllowedBrowserPageUrl(const std::string& url);
std::string GetBrowserPageFilePath(const std::string& url);
std::string GetBrowserPageDevUrl(const std::string& dev_ui_url,
                                 const std::string& url);
bool IsPersistableWebUrl(const std::string& url);
bool IsAllowedHttpUrl(const std::string& url);
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

} // namespace otf

#endif // OTF_BROWSER_UTILS_H_
