#include "otf_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif

#include "include/cef_parser.h"
#include "include/cef_values.h"

namespace otf {

namespace {

constexpr const char* kBrowserSchemePrefix = "browser://";
constexpr const char* kDefaultSearchEngine = "";
const char* kAllowedSearchEngines[] = {
    "",         "google",   "bing",     "yahoo",   "duckduckgo",
    "baidu",    "yandex",   "ecosia",   "naver",   "startpage"};

bool ExtractBrowserPageName(const std::string& url, std::string* page_name) {
  if (url.rfind(kBrowserSchemePrefix, 0) != 0) {
    return false;
  }

  std::string page = url.substr(std::strlen(kBrowserSchemePrefix));
  const size_t query_pos = page.find_first_of("?#");
  if (query_pos != std::string::npos) {
    page = page.substr(0, query_pos);
  }
  if (!page.empty() && page.back() == '/') {
    page.pop_back();
  }
  if (page == "newtab" || page == "settings" || page == "findbar" ||
      page == "history" || page == "bookmarks" || page == "downloads") {
    if (page_name) {
      *page_name = page;
    }
    return true;
  }
  return false;
}

std::string BuildSettingsJson(const std::string& search_engine_id) {
  return JsonObjectBuilder().AddString("searchEngine", search_engine_id).Build();
}

}  // namespace

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 2);
  for (unsigned char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

std::string JsonString(const std::string& s) {
  return "\"" + JsonEscape(s) + "\"";
}

JsonObjectBuilder& JsonObjectBuilder::AddString(const std::string& key,
                                                const std::string& value) {
  fields_.push_back(JsonString(key) + ":" + JsonString(value));
  return *this;
}

JsonObjectBuilder& JsonObjectBuilder::AddInt(const std::string& key, int value) {
  fields_.push_back(JsonString(key) + ":" + std::to_string(value));
  return *this;
}

JsonObjectBuilder& JsonObjectBuilder::AddBool(const std::string& key, bool value) {
  fields_.push_back(JsonString(key) + ":" + (value ? "true" : "false"));
  return *this;
}

JsonObjectBuilder& JsonObjectBuilder::AddRaw(const std::string& key,
                                             const std::string& raw_json) {
  fields_.push_back(JsonString(key) + ":" + raw_json);
  return *this;
}

std::string JsonObjectBuilder::Build() const {
  std::ostringstream out;
  out << "{";
  for (size_t i = 0; i < fields_.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << fields_[i];
  }
  out << "}";
  return out.str();
}

std::string GetExecutableDir() {
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  if (count != -1) {
    return std::string(dirname(result));
  }
  return "";
}

std::string GetHomeDir() {
  const char* home_dir = getenv("HOME");
  if (home_dir) {
    return std::string(home_dir);
  }
  
  struct passwd* pw = getpwuid(getuid());
  if (pw && pw->pw_dir) {
    return std::string(pw->pw_dir);
  }
  
  return "";
}

std::string GetSettingsFilePath() {
  std::string home = GetHomeDir();
  if (home.empty()) return "";

  std::filesystem::path settings_dir = std::filesystem::path(home) / ".otf-browser";
  if (!std::filesystem::exists(settings_dir)) {
    std::filesystem::create_directories(settings_dir);
  }

  return (settings_dir / "settings.json").string();
}

std::string GetDownloadsDir() {
#if defined(_WIN32)
  PWSTR path = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path))) {
    std::wstring wide_path(path);
    CoTaskMemFree(path);
    if (!wide_path.empty()) {
      int len = WideCharToMultiByte(CP_UTF8, 0, wide_path.c_str(), -1, nullptr, 0,
                                    nullptr, nullptr);
      if (len > 0) {
        std::string utf8_path(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide_path.c_str(), -1, utf8_path.data(),
                            len, nullptr, nullptr);
        return utf8_path;
      }
    }
  }
#endif
  std::string home = GetHomeDir();
  if (home.empty()) {
    return "";
  }
  return (std::filesystem::path(home) / "Downloads").string();
}

std::string SanitizeFilename(const std::string& filename) {
  std::string out = filename.empty() ? "download" : filename;
  for (char& c : out) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|') {
      c = '_';
    }
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
    out.pop_back();
  }
  if (out.empty()) {
    return "download";
  }
  return out;
}

std::string BuildDownloadPath(const std::string& suggested_name) {
  std::filesystem::path downloads_dir = GetDownloadsDir();
  if (downloads_dir.empty()) {
    downloads_dir = std::filesystem::temp_directory_path();
  }
  std::filesystem::create_directories(downloads_dir);

  std::filesystem::path candidate = downloads_dir / SanitizeFilename(suggested_name);
  if (!std::filesystem::exists(candidate)) {
    return candidate.string();
  }

  const std::string stem = candidate.stem().string();
  const std::string ext = candidate.extension().string();
  for (int i = 1; i < 1000; ++i) {
    std::filesystem::path numbered =
        downloads_dir / (stem + " (" + std::to_string(i) + ")" + ext);
    if (!std::filesystem::exists(numbered)) {
      return numbered.string();
    }
  }
  return candidate.string();
}

std::string GetDefaultSettingsJson() {
  return BuildSettingsJson(kDefaultSearchEngine);
}

bool IsAllowedSearchEngineId(const std::string& search_engine_id) {
  for (const char* allowed : kAllowedSearchEngines) {
    if (search_engine_id == allowed) {
      return true;
    }
  }
  return false;
}

bool NormalizeSettingsJson(const std::string& raw_json,
                           std::string* normalized_json) {
  CefRefPtr<CefValue> root = CefParseJSON(raw_json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    return false;
  }

  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict) {
    return false;
  }

  CefDictionaryValue::KeyList keys;
  dict->GetKeys(keys);
  for (const auto& key : keys) {
    if (key != "searchEngine") {
      return false;
    }
  }

  std::string search_engine = kDefaultSearchEngine;
  if (dict->HasKey("searchEngine")) {
    if (dict->GetType("searchEngine") != VTYPE_STRING) {
      return false;
    }
    search_engine = dict->GetString("searchEngine");
    if (!IsAllowedSearchEngineId(search_engine)) {
      return false;
    }
  }

  if (normalized_json) {
    *normalized_json = BuildSettingsJson(search_engine);
  }
  return true;
}

std::string LoadSettingsJson() {
  const std::string path = GetSettingsFilePath();
  if (path.empty()) {
    return GetDefaultSettingsJson();
  }

  std::ifstream input(path);
  std::string normalized = GetDefaultSettingsJson();
  if (!input.is_open()) {
    return normalized;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!NormalizeSettingsJson(buffer.str(), &normalized)) {
    std::ofstream rewrite(path);
    if (rewrite.is_open()) {
      rewrite << normalized;
    }
  }
  return normalized;
}

bool SaveSettingsJson(const std::string& raw_json, std::string* normalized_json) {
  std::string normalized;
  if (!NormalizeSettingsJson(raw_json, &normalized)) {
    return false;
  }

  const std::string path = GetSettingsFilePath();
  if (path.empty()) {
    return false;
  }

  std::ofstream output(path);
  if (!output.is_open()) {
    return false;
  }

  output << normalized;
  if (normalized_json) {
    *normalized_json = normalized;
  }
  return true;
}

bool IsAllowedBrowserPageUrl(const std::string& url) {
  return ExtractBrowserPageName(url, nullptr);
}

std::string GetBrowserPageFilePath(const std::string& url) {
  std::string page_name;
  if (!ExtractBrowserPageName(url, &page_name)) {
    return "";
  }
  return GetExecutableDir() + "/ui/" + page_name + ".html";
}

std::string GetBrowserPageDevUrl(const std::string& dev_ui_url,
                                 const std::string& url) {
  std::string page_name;
  if (dev_ui_url.empty() || !ExtractBrowserPageName(url, &page_name)) {
    return "";
  }
  return dev_ui_url + "/" + page_name + ".html";
}

bool IsPersistableWebUrl(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string NormalizeBookmarkUrl(const std::string& url) {
  if (!IsPersistableWebUrl(url)) {
    return url;
  }

  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return url;
  }

  const std::string scheme = CefString(&parts.scheme).ToString();
  const std::string host = CefString(&parts.host).ToString();
  const std::string port = CefString(&parts.port).ToString();
  std::string path = CefString(&parts.path).ToString();
  const std::string query = CefString(&parts.query).ToString();

  if (path.empty() || path == "/") {
    path = "/";
  } else {
    while (path.size() > 1 && path.back() == '/') {
      path.pop_back();
    }
  }

  std::string fallback = scheme + "://" + host;
  if (!port.empty()) {
    fallback += ":" + port;
  }
  fallback += path;
  if (!query.empty()) {
    fallback += "?" + query;
  }
  return fallback;
}

int SelectNextActiveTabId(const std::vector<int>& tab_ids, int closing_tab_id) {
  if (tab_ids.empty()) {
    return -1;
  }

  const auto it = std::find(tab_ids.begin(), tab_ids.end(), closing_tab_id);
  if (it == tab_ids.end()) {
    return tab_ids.front();
  }

  if (tab_ids.size() == 1) {
    return -1;
  }

  const size_t index = static_cast<size_t>(it - tab_ids.begin());
  const size_t next_index =
      (index + 1 < tab_ids.size()) ? index + 1 : index - 1;
  return tab_ids[next_index];
}

// ── Zoom level utilities ────────────────────────────────────────────
//
// CEF zoom levels are logarithmic: zoom_level = log2(percent / 100).
// We snap to standard browser zoom percentages so the UX is predictable.

namespace {

const double kZoomPercentages[] = {
  25.0, 33.0, 50.0, 67.0, 75.0, 80.0, 90.0, 100.0,
  110.0, 125.0, 150.0, 175.0, 200.0, 250.0, 300.0, 400.0, 500.0
};
constexpr size_t kZoomCount = sizeof(kZoomPercentages) / sizeof(kZoomPercentages[0]);

}  // namespace

const std::vector<double>& GetStandardZoomPercentages() {
  static const std::vector<double> levels(
      kZoomPercentages, kZoomPercentages + kZoomCount);
  return levels;
}

double ZoomLevelToPercent(double zoom_level) {
  return 100.0 * std::pow(2.0, zoom_level);
}

double PercentToZoomLevel(double percent) {
  return std::log2(percent / 100.0);
}

double ZoomIn(double current_zoom_level) {
  double current_percent = ZoomLevelToPercent(current_zoom_level);
  // Add a small epsilon so we don't stay on the same level due to FP precision.
  double threshold = current_percent + 0.1;
  for (size_t i = 0; i < kZoomCount; ++i) {
    if (kZoomPercentages[i] > threshold) {
      return PercentToZoomLevel(kZoomPercentages[i]);
    }
  }
  // Already at maximum – clamp.
  return PercentToZoomLevel(kZoomPercentages[kZoomCount - 1]);
}

double ZoomOut(double current_zoom_level) {
  double current_percent = ZoomLevelToPercent(current_zoom_level);
  double threshold = current_percent - 0.1;
  for (size_t i = kZoomCount; i > 0; --i) {
    if (kZoomPercentages[i - 1] < threshold) {
      return PercentToZoomLevel(kZoomPercentages[i - 1]);
    }
  }
  // Already at minimum – clamp.
  return PercentToZoomLevel(kZoomPercentages[0]);
}

double ZoomReset() {
  return 0.0;  // 0.0 corresponds to 100 %
}

} // namespace otf
