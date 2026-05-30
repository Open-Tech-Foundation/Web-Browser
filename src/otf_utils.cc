#include "otf_utils.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <mutex>
#include <set>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#else
#include <limits.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/statvfs.h>
#endif

#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include <vips/vips.h>

namespace otf {

namespace {

constexpr const char* kBrowserSchemePrefix = "browser://";
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
      page == "history" || page == "bookmarks" || page == "downloads" ||
      page == "security" || page == "insecure-blocked" ||
      page == "linkpreview") {
    if (page_name) {
      *page_name = page;
    }
    return true;
  }
  return false;
}

std::string ExtractBrowserPageSuffix(const std::string& url) {
  if (url.rfind(kBrowserSchemePrefix, 0) != 0) {
    return "";
  }

  const size_t start = std::strlen(kBrowserSchemePrefix);
  const size_t query_pos = url.find_first_of("?#", start);
  if (query_pos == std::string::npos) {
    return "";
  }
  return url.substr(query_pos);
}

std::string BrowserPageHtmlName(const std::string& page_name) {
  return page_name + ".html";
}

std::string ToLowerCopy(const std::string& value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

bool IsLoopbackHost(const std::string& host) {
  std::string lower_host = ToLowerCopy(host);
  // CEF reports IPv6 hosts wrapped in brackets ("[::1]"). Strip them so the
  // bare-address comparison matches.
  if (lower_host.size() >= 2 && lower_host.front() == '[' &&
      lower_host.back() == ']') {
    lower_host = lower_host.substr(1, lower_host.size() - 2);
  }
  return lower_host == "localhost" || lower_host == "127.0.0.1" ||
         lower_host == "::1";
}

std::string BuildSettingsJson(const std::optional<std::string>& search_engine_id, bool history_enabled, bool downloads_enabled, const std::string& startup_behavior, const std::vector<std::string>& startup_urls, bool https_only, bool block_insecure, const std::string& appearance_mode, const std::vector<CustomSearchEngine>& custom_engines, const std::string& cache_dir = {}, const std::string& download_dir = {}) {
  std::string urls_json = "[";
  for (size_t i = 0; i < startup_urls.size(); ++i) {
    if (i > 0) urls_json += ",";
    urls_json += JsonString(startup_urls[i]);
  }
  urls_json += "]";

  JsonObjectBuilder builder;
  if (search_engine_id.has_value()) {
    builder.AddString("searchEngine", *search_engine_id);
  } else {
    builder.AddNull("searchEngine");
  }

  auto result = builder
      .AddBool("historyEnabled", history_enabled)
      .AddBool("downloadsEnabled", downloads_enabled)
      .AddString("startupBehavior", startup_behavior)
      .AddRaw("startupUrls", urls_json)
      .AddBool("httpsOnly", https_only)
      .AddBool("blockInsecure", block_insecure)
      .AddString("appearanceMode", appearance_mode)
      .AddRaw("customSearchEngines", BuildCustomEnginesJson(custom_engines));
  if (!cache_dir.empty()) {
    result.AddString("cacheDir", cache_dir);
  }
  if (!download_dir.empty()) {
    result.AddString("downloadDir", download_dir);
  }
  return result.Build();
}

bool IsAllowedStartupBehavior(const std::string& startup_behavior) {
  return startup_behavior == "newtab" || startup_behavior == "continue" ||
         startup_behavior == "specific";
}

bool IsAllowedAppearanceMode(const std::string& mode) {
  return mode == "auto" || mode == "light" || mode == "dark";
}

}  // namespace

std::string BuildCustomEnginesJson(const std::vector<CustomSearchEngine>& engines) {
  std::string json = "[";
  for (size_t i = 0; i < engines.size(); ++i) {
    if (i > 0) json += ",";
    json += "{";
    json += JsonString("id") + ":" + JsonString(engines[i].id) + ",";
    json += JsonString("name") + ":" + JsonString(engines[i].name) + ",";
    json += JsonString("url") + ":" + JsonString(engines[i].url);
    json += "}";
  }
  json += "]";
  return json;
}

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

std::string HtmlAttrEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c; break;
    }
  }
  return out;
}

std::optional<int> ParseIntStrict(std::string_view s) {
  if (s.empty()) return std::nullopt;
  int value = 0;
  const auto* end = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(s.data(), end, value);
  if (ec != std::errc{} || ptr != end) return std::nullopt;
  return value;
}

std::optional<uint32_t> ParseUint32Strict(std::string_view s) {
  if (s.empty()) return std::nullopt;
  uint32_t value = 0;
  const auto* end = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(s.data(), end, value);
  if (ec != std::errc{} || ptr != end) return std::nullopt;
  return value;
}

std::optional<uint64_t> ParseUint64Strict(std::string_view s) {
  if (s.empty()) return std::nullopt;
  uint64_t value = 0;
  const auto* end = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(s.data(), end, value);
  if (ec != std::errc{} || ptr != end) return std::nullopt;
  return value;
}

std::string ParseLengthPrefixedField(const std::string& input,
                                     size_t* cursor,
                                     bool* ok) {
  if (!cursor || !ok || *cursor > input.size()) {
    if (ok) {
      *ok = false;
    }
    return "";
  }

  const size_t colon = input.find(':', *cursor);
  if (colon == std::string::npos || colon == *cursor) {
    *ok = false;
    return "";
  }

  const auto length_opt = ParseUint64Strict(
      std::string_view(input).substr(*cursor, colon - *cursor));
  if (!length_opt || *length_opt > input.size() - colon - 1) {
    *ok = false;
    return "";
  }

  const size_t length = static_cast<size_t>(*length_opt);
  const size_t start = colon + 1;
  *cursor = start + length;
  *ok = true;
  return input.substr(start, length);
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

JsonObjectBuilder& JsonObjectBuilder::AddNull(const std::string& key) {
  fields_.push_back(JsonString(key) + ":null");
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

std::optional<std::vector<uint8_t>> ReadFileBinary(const std::string& utf8_path) {
  std::ifstream f(Utf8Path(utf8_path), std::ios::binary);
  if (!f) return std::nullopt;
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
}

std::string ReadFileText(const std::string& utf8_path) {
  std::ifstream f(Utf8Path(utf8_path), std::ios::binary);
  if (!f) return {};
  return std::string((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
}

bool WriteFileText(const std::string& utf8_path, const std::string& content) {
  std::ofstream f(Utf8Path(utf8_path), std::ios::trunc);
  if (!f) return false;
  f << content;
  return f.good();
}

bool WriteFileBinary(const std::string& utf8_path, const void* data, size_t size) {
  std::ofstream f(Utf8Path(utf8_path), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  if (size > 0) {
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  }
  return f.good();
}

std::string GetTempFilePath(const std::string& prefix) {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
  if (ec) dir = std::filesystem::path(".");
  // Unique within this process: monotonic clock + an atomic counter so two
  // calls in the same tick still differ. Caller is responsible for writing/
  // cleaning up the file; this only computes a collision-free path.
  static std::atomic<uint64_t> counter{0};
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string unique =
      prefix + "_" + std::to_string(static_cast<unsigned long long>(stamp)) +
      "_" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
  return PathToUtf8(dir / unique);
}

void DiagLog(const std::string& line) {
  static std::mutex m;
  // Relative-to-first-call milliseconds: shows the order of startup steps and
  // exactly where a hang or failure occurs, with no platform-specific
  // localtime/timezone code.
  static const auto t0 = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(m);
  const std::string dir = GetExecutableDir();
  if (dir.empty()) return;
  std::ofstream f(Utf8Path(dir + "/otf-diag.log"), std::ios::app);
  if (!f) return;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  f << "[+" << ms << "ms] " << line << "\n";
  f.flush();
}

void ApplyProductionCommandLineSwitches(CefRefPtr<CefCommandLine> command_line) {
  if (!command_line) return;

#if defined(_WIN32)
  // CRITICAL on Windows: we run WITHOUT a sandbox (CefSettings.no_sandbox=true,
  // no cef_sandbox.lib linked, nullptr sandbox_info). The browser process's
  // command line is Reset() just before this call, which strips the --no-sandbox
  // switch CEF would otherwise propagate to subprocesses. Without --no-sandbox on
  // the command line, each spawned renderer/GPU/utility process tries to bring up
  // a sandbox that doesn't exist and crashes on launch — before any of our code
  // runs — so the process crash-loops and the window stays blank. Re-add it here
  // (after Reset) so every subprocess inherits it. Linux is NOT affected: it uses
  // the real SUID chrome-sandbox helper, so we must NOT disable the sandbox there.
  command_line->AppendSwitch("no-sandbox");
#endif

  command_line->AppendSwitch("enable-unsafe-webgpu");

  // Disable Service Workers entirely. They run in a separate realm outside
  // the page policy injection window and can persist state across origins
  // (caching, background sync, push) in ways that conflict with our privacy
  // and security model. Must be one --disable-features switch with a value:
  // AppendSwitch("disable-features=ServiceWorker") would create a switch whose
  // *name* is "disable-features=ServiceWorker" and Chromium would ignore it,
  // leaving the Service Worker subsystem live. If more features need disabling,
  // join them into a single comma-separated value (Chromium does not merge
  // repeated --disable-features switches).
  command_line->AppendSwitchWithValue("disable-features", "ServiceWorker");
  // (Previously we set --allow-file-access-from-files here so the file://-
  // loaded UI shell could fetch its ES module bundles. That's no longer
  // needed — the shell is now served via our browser:// custom scheme
  // which is registered as STANDARD|SECURE|CORS_ENABLED and behaves like
  // a proper HTTP origin.)

#if defined(__linux__)
  // Force X11 Ozone. CEF's Alloy runtime (which we use for the embedded UI
  // surface) does not support Wayland — only the Chrome runtime does. On a
  // Wayland session, XWayland transparently bridges X11 clients, so this is
  // the correct, documented configuration for Alloy, not a workaround.
  // Do not "fix" this to auto/wayland without first switching to
  // CEF_RUNTIME_STYLE_CHROME everywhere.
  command_line->AppendSwitchWithValue("ozone-platform", "x11");
  // Set the window class and desktop name so that X11/Wayland task monitors
  // map the process to our .desktop file instead of falling back to Chromium.
  command_line->AppendSwitchWithValue("class", "otf-browser");
  command_line->AppendSwitchWithValue("desktop-name", "otf-browser.desktop");
  command_line->AppendSwitch("enable-transparent-visuals");
#endif

  // Override the User-Agent Client Hint platform to "Linux" regardless of
  // the actual OS, so websites cannot fingerprint the OS via
  // Sec-CH-UA-Platform (which bypasses our custom user-agent string).
  command_line->AppendSwitchWithValue("user-agent-client-hints-platform", "Linux");
}

std::string GetExecutableDir() {
#if defined(_WIN32)
  wchar_t module_path[MAX_PATH];
  const DWORD len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return "";
  return PathToUtf8(std::filesystem::path(module_path).parent_path());
#else
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  if (count > 0 && count < PATH_MAX) {
    result[count] = '\0';
    return PathToUtf8(std::filesystem::path(result).parent_path());
  }
  return "";
#endif
}

std::string GetExecutablePath() {
#if defined(_WIN32)
  wchar_t module_path[MAX_PATH];
  const DWORD len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return "";
  return PathToUtf8(std::filesystem::path(module_path));
#else
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  if (count > 0 && count < PATH_MAX) {
    result[count] = '\0';
    return PathToUtf8(std::filesystem::path(result));
  }
  const std::string dir = GetExecutableDir();
  if (!dir.empty()) return dir + "/otf-browser";
  return "";
#endif
}

std::string GetHomeDir() {
  // Tests set OTF_TEST_HOME to redirect storage without touching platform APIs.
  const char* test_home = std::getenv("OTF_TEST_HOME");
  if (test_home && test_home[0] != '\0') {
    return std::string(test_home);
  }
#if defined(_WIN32)
  PWSTR wide_profile = nullptr;
  std::string home_dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &wide_profile))) {
    std::string converted = PathToUtf8(std::filesystem::path(wide_profile));
    CoTaskMemFree(wide_profile);
    wide_profile = nullptr;
    if (!converted.empty()) {
      return converted;
    }
  }
  const char* user_profile = std::getenv("USERPROFILE");
  if (user_profile && user_profile[0] != '\0') {
    return std::string(user_profile);
  }
  return "";
#else
  const char* home_dir = getenv("HOME");
  if (home_dir) {
    return std::string(home_dir);
  }
  
  struct passwd* pw = getpwuid(getuid());
  if (pw && pw->pw_dir) {
    return std::string(pw->pw_dir);
  }
  
  return "";
#endif
}

static bool IsDevMode() {
  const char* v = std::getenv("OTF_DEV_MODE");
  return v && v[0] != '\0' && v[0] != '0';
}

// Ensures the directory exists using non-throwing error_code overloads.
// Multiple processes (including the sandboxed renderer) may call this
// concurrently; exceptions from EPERM in confined processes would crash them.
static std::filesystem::path EnsureDir(std::filesystem::path dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::filesystem::path GetAppDataDir() {
  const bool dev = IsDevMode();

  // Test redirect: everything lands under OTF_TEST_HOME so production
  // platform paths are never touched and test runs are fully isolated.
  const char* test_home = std::getenv("OTF_TEST_HOME");
  if (test_home && test_home[0] != '\0') {
    return EnsureDir(std::filesystem::path(test_home) /
                     (dev ? "otf-browser-dev" : "otf-browser"));
  }

#if defined(_WIN32)
  // %APPDATA% (roaming) is the canonical location for persistent per-user app
  // data on Windows. Wide API avoids ANSI/UTF-8 mismatch on non-ASCII usernames.
  PWSTR wide = nullptr;
  std::filesystem::path base;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &wide)) && wide) {
    base = std::filesystem::path(std::wstring(wide));
    CoTaskMemFree(wide);
  } else {
    if (wide) CoTaskMemFree(wide);
    const char* appdata = std::getenv("APPDATA");
    if (!appdata || !appdata[0]) return {};
    base = std::filesystem::path(std::string(appdata));
  }
  return EnsureDir(base / (dev ? L"OTF Browser Dev" : L"OTF Browser"));
#elif defined(__APPLE__)
  const char* home = std::getenv("HOME");
  if (!home || !home[0]) {
    struct passwd* pw = getpwuid(getuid());
    if (!pw || !pw->pw_dir) return {};
    home = pw->pw_dir;
  }
  return EnsureDir(std::filesystem::path(home) / "Library" / "Application Support" /
                   (dev ? "OTF Browser Dev" : "OTF Browser"));
#else
  // Linux: $XDG_DATA_HOME/otf-browser[-dev]/ (fallback: ~/.local/share/)
  const char* xdg = std::getenv("XDG_DATA_HOME");
  std::filesystem::path base;
  if (xdg && xdg[0]) {
    base = std::filesystem::path(xdg);
  } else {
    const char* home = std::getenv("HOME");
    if (!home || !home[0]) {
      struct passwd* pw = getpwuid(getuid());
      if (!pw || !pw->pw_dir) return {};
      home = pw->pw_dir;
    }
    base = std::filesystem::path(home) / ".local" / "share";
  }
  return EnsureDir(base / (dev ? "otf-browser-dev" : "otf-browser"));
#endif
}

std::filesystem::path GetAppCacheDir() {
  const bool dev = IsDevMode();

  // Test redirect: cache lands under OTF_TEST_HOME/otf-browser[-dev]/cache/
  const char* test_home = std::getenv("OTF_TEST_HOME");
  if (test_home && test_home[0] != '\0') {
    return EnsureDir(std::filesystem::path(test_home) /
                     (dev ? "otf-browser-dev" : "otf-browser") / "cache");
  }

#if defined(_WIN32)
  // %LOCALAPPDATA% (non-roaming) is the canonical location for caches on
  // Windows — not synced across machines, suitable for large/regenerable data.
  PWSTR wide = nullptr;
  std::filesystem::path base;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &wide)) && wide) {
    base = std::filesystem::path(std::wstring(wide));
    CoTaskMemFree(wide);
  } else {
    if (wide) CoTaskMemFree(wide);
    const char* localappdata = std::getenv("LOCALAPPDATA");
    if (!localappdata || !localappdata[0]) return {};
    base = std::filesystem::path(std::string(localappdata));
  }
  return EnsureDir(base / (dev ? L"OTF Browser Dev" : L"OTF Browser") / L"Cache");
#elif defined(__APPLE__)
  const char* home = std::getenv("HOME");
  if (!home || !home[0]) {
    struct passwd* pw = getpwuid(getuid());
    if (!pw || !pw->pw_dir) return {};
    home = pw->pw_dir;
  }
  return EnsureDir(std::filesystem::path(home) / "Library" / "Caches" /
                   (dev ? "OTF Browser Dev" : "OTF Browser"));
#else
  // Linux: $XDG_CACHE_HOME/otf-browser[-dev]/ (fallback: ~/.cache/)
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  std::filesystem::path base;
  if (xdg && xdg[0]) {
    base = std::filesystem::path(xdg);
  } else {
    const char* home = std::getenv("HOME");
    if (!home || !home[0]) {
      struct passwd* pw = getpwuid(getuid());
      if (!pw || !pw->pw_dir) return {};
      home = pw->pw_dir;
    }
    base = std::filesystem::path(home) / ".cache";
  }
  return EnsureDir(base / (dev ? "otf-browser-dev" : "otf-browser"));
#endif
}

std::string GetSettingsFilePath() {
  const std::filesystem::path dir = GetAppDataDir();
  if (dir.empty()) return "";
  return PathToUtf8(dir / "settings.json");
}

std::string GetDownloadsDir() {
#if defined(_WIN32)
  PWSTR path = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path))) {
    const std::string result = path ? PathToUtf8(std::filesystem::path(path)) : std::string{};
    CoTaskMemFree(path);
    if (!result.empty()) return result;
  }
#endif
  const std::string home = GetHomeDir();
  if (home.empty()) return "";
  return PathToUtf8(Utf8Path(home) / "Downloads");
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
  std::filesystem::path downloads_dir = GetActiveDownloadsDir();
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
  return BuildSettingsJson(std::nullopt, false, false, "newtab", {}, false, true, "auto", {});
}

std::optional<std::string> GetCurrentSearchEngineId() {
  CefRefPtr<CefValue> root =
      CefParseJSON(LoadSettingsJson(), JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    return std::nullopt;
  }

  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict || !dict->HasKey("searchEngine")) {
    return std::nullopt;
  }

  const auto search_engine_type = dict->GetType("searchEngine");
  if (search_engine_type == VTYPE_NULL) {
    return std::nullopt;
  }
  if (search_engine_type != VTYPE_STRING) {
    return std::nullopt;
  }

  const std::string value = dict->GetString("searchEngine");
  if (value.empty()) {
    return std::nullopt;
  }
  // Check built-in engines first
  if (IsAllowedSearchEngineId(value)) {
    return value;
  }
  // Check custom engines
  auto custom = GetCustomSearchEngines();
  for (const auto& engine : custom) {
    if (engine.id == value) {
      return value;
    }
  }
  return std::nullopt;
}

bool IsHistoryEnabled() {
  CefRefPtr<CefValue> root =
      CefParseJSON(LoadSettingsJson(), JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    return false;
  }

  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict || !dict->HasKey("historyEnabled") ||
      dict->GetType("historyEnabled") != VTYPE_BOOL) {
    return false;
  }
  return dict->GetBool("historyEnabled");
}

bool IsDownloadsEnabled() {
  CefRefPtr<CefValue> root =
      CefParseJSON(LoadSettingsJson(), JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    return false;
  }

  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict || !dict->HasKey("downloadsEnabled") ||
      dict->GetType("downloadsEnabled") != VTYPE_BOOL) {
    return false;
  }
  return dict->GetBool("downloadsEnabled");
}

bool IsAllowedSearchEngineId(const std::string& search_engine_id) {
  for (const char* allowed : kAllowedSearchEngines) {
    if (search_engine_id == allowed) {
      return true;
    }
  }
  return false;
}

bool IsAllowedSearchEngineId(const std::string& search_engine_id,
                             const std::vector<CustomSearchEngine>& custom_engines) {
  if (IsAllowedSearchEngineId(search_engine_id)) return true;
  for (const auto& engine : custom_engines) {
    if (engine.id == search_engine_id) return true;
  }
  return false;
}

std::string EncodeSearchQuery(const std::string& query) {
  std::string encoded_query = CefURIEncode(query, false).ToString();
  size_t pos = 0;
  while ((pos = encoded_query.find("%20", pos)) != std::string::npos) {
    encoded_query.replace(pos, 3, "+");
    ++pos;
  }
  return encoded_query;
}

namespace {

std::string BuildSearchUrlBuiltin(const std::string& search_engine_id,
                                  const std::string& query) {
  std::string base_url;
  if (search_engine_id == "google") {
    base_url = "https://www.google.com/search?q=";
  } else if (search_engine_id == "bing") {
    base_url = "https://www.bing.com/search?q=";
  } else if (search_engine_id == "yahoo") {
    base_url = "https://search.yahoo.com/search?p=";
  } else if (search_engine_id == "duckduckgo") {
    base_url = "https://duckduckgo.com/?q=";
  } else if (search_engine_id == "baidu") {
    base_url = "https://www.baidu.com/s?wd=";
  } else if (search_engine_id == "yandex") {
    base_url = "https://yandex.com/search/?text=";
  } else if (search_engine_id == "ecosia") {
    base_url = "https://www.ecosia.org/search?q=";
  } else if (search_engine_id == "naver") {
    base_url = "https://search.naver.com/search.naver?query=";
  } else if (search_engine_id == "startpage") {
    base_url = "https://www.startpage.com/search?q=";
  }

  if (base_url.empty()) {
    return "";
  }
  return base_url + EncodeSearchQuery(query);
}

}  // namespace

std::string BuildSearchUrl(const std::string& search_engine_id,
                           const std::string& query) {
  std::string result = BuildSearchUrlBuiltin(search_engine_id, query);
  if (!result.empty()) return result;

  auto custom = GetCustomSearchEngines();
  return BuildSearchUrl(search_engine_id, query, custom);
}

std::string BuildSearchUrl(const std::string& search_engine_id,
                           const std::string& query,
                           const std::vector<CustomSearchEngine>& custom_engines) {
  std::string result = BuildSearchUrlBuiltin(search_engine_id, query);
  if (!result.empty()) return result;

  for (const auto& engine : custom_engines) {
    if (engine.id == search_engine_id) {
      std::string url = engine.url;
      std::string encoded = EncodeSearchQuery(query);
      size_t pos = url.find("%s");
      if (pos != std::string::npos) {
        url.replace(pos, 2, encoded);
      } else {
        url += encoded;
      }
      return url;
    }
  }
  return "";
}

std::vector<CustomSearchEngine> GetCustomSearchEngines() {
  std::vector<CustomSearchEngine> engines;
  const std::string json = LoadSettingsJson();
  // Skip CEF parsing entirely when the array is empty — avoids calling
  // CefParseJSON (which requires CEF to be initialized) in test binaries.
  const auto key_pos = json.find("\"customSearchEngines\"");
  if (key_pos != std::string::npos) {
    const auto bracket = json.find('[', key_pos);
    if (bracket == std::string::npos) return engines;
    const auto first = json.find_first_not_of(" \t\r\n", bracket + 1);
    if (first == std::string::npos || json[first] == ']') return engines;
  }
  CefRefPtr<CefValue> root =
      CefParseJSON(json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    return engines;
  }
  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict || !dict->HasKey("customSearchEngines") ||
      dict->GetType("customSearchEngines") != VTYPE_LIST) {
    return engines;
  }
  CefRefPtr<CefListValue> list = dict->GetList("customSearchEngines");
  for (size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_DICTIONARY) continue;
    CefRefPtr<CefDictionaryValue> entry = list->GetDictionary(i);
    if (!entry->HasKey("id") || entry->GetType("id") != VTYPE_STRING) continue;
    if (!entry->HasKey("name") || entry->GetType("name") != VTYPE_STRING) continue;
    if (!entry->HasKey("url") || entry->GetType("url") != VTYPE_STRING) continue;
    std::string id = entry->GetString("id");
    std::string name = entry->GetString("name");
    std::string url = entry->GetString("url");
    if (id.empty() || name.empty() || url.empty()) continue;
    engines.push_back({std::move(id), std::move(name), std::move(url)});
  }
  return engines;
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
    if (key != "searchEngine" && key != "historyEnabled" && key != "downloadsEnabled" && 
        key != "startupBehavior" && key != "startupUrls" && key != "httpsOnly" && 
        key != "blockInsecure" && key != "appearanceMode" &&
        key != "customSearchEngines" && key != "cacheDir" && key != "downloadDir") {
      return false;
    }
  }

  // Parse custom search engines first (needed for searchEngine validation)
  std::vector<CustomSearchEngine> custom_engines;
  if (dict->HasKey("customSearchEngines")) {
    if (dict->GetType("customSearchEngines") != VTYPE_LIST) {
      return false;
    }
    CefRefPtr<CefListValue> list = dict->GetList("customSearchEngines");
    for (size_t i = 0; i < list->GetSize(); ++i) {
      if (list->GetType(i) != VTYPE_DICTIONARY) return false;
      CefRefPtr<CefDictionaryValue> entry = list->GetDictionary(i);
      if (!entry->HasKey("id") || entry->GetType("id") != VTYPE_STRING) return false;
      if (!entry->HasKey("name") || entry->GetType("name") != VTYPE_STRING) return false;
      if (!entry->HasKey("url") || entry->GetType("url") != VTYPE_STRING) return false;
      std::string id = entry->GetString("id");
      std::string name = entry->GetString("name");
      std::string url = entry->GetString("url");
      if (id.empty() || name.empty() || url.empty()) return false;
      // Custom engine IDs must not conflict with built-in engines
      if (IsAllowedSearchEngineId(id)) return false;
      custom_engines.push_back({std::move(id), std::move(name), std::move(url)});
    }
  }

  std::optional<std::string> search_engine;
  if (dict->HasKey("searchEngine")) {
    const auto search_engine_type = dict->GetType("searchEngine");
    if (search_engine_type == VTYPE_NULL) {
      search_engine.reset();
    } else if (search_engine_type == VTYPE_STRING) {
      const std::string value = dict->GetString("searchEngine");
      if (value.empty()) {
        search_engine.reset();
      } else {
        if (!IsAllowedSearchEngineId(value, custom_engines)) {
          return false;
        }
        search_engine = value;
      }
    } else {
      return false;
    }
  }

  bool history_enabled = false;
  if (dict->HasKey("historyEnabled")) {
    if (dict->GetType("historyEnabled") != VTYPE_BOOL) {
      return false;
    }
    history_enabled = dict->GetBool("historyEnabled");
  }

  bool downloads_enabled = false;
  if (dict->HasKey("downloadsEnabled")) {
    if (dict->GetType("downloadsEnabled") != VTYPE_BOOL) {
      return false;
    }
    downloads_enabled = dict->GetBool("downloadsEnabled");
  }

  std::string startup_behavior = "newtab";
  if (dict->HasKey("startupBehavior")) {
    if (dict->GetType("startupBehavior") != VTYPE_STRING) {
      return false;
    }
    startup_behavior = dict->GetString("startupBehavior");
    if (!IsAllowedStartupBehavior(startup_behavior)) {
      return false;
    }
  }

  std::vector<std::string> startup_urls;
  if (dict->HasKey("startupUrls")) {
    if (dict->GetType("startupUrls") != VTYPE_LIST) {
      return false;
    }
    CefRefPtr<CefListValue> list = dict->GetList("startupUrls");
    for (size_t i = 0; i < list->GetSize(); ++i) {
      if (list->GetType(i) == VTYPE_STRING) {
        std::string url = list->GetString(i);
        if (IsAllowedStartupUrl(url)) {
          startup_urls.push_back(url);
        }
      }
    }
  }

  bool https_only = false;
  if (dict->HasKey("httpsOnly")) {
    if (dict->GetType("httpsOnly") != VTYPE_BOOL) {
      return false;
    }
    https_only = dict->GetBool("httpsOnly");
  }

  bool block_insecure = true;
  if (dict->HasKey("blockInsecure")) {
    if (dict->GetType("blockInsecure") != VTYPE_BOOL) {
      return false;
    }
    block_insecure = dict->GetBool("blockInsecure");
  }

  std::string appearance_mode = "auto";
  if (dict->HasKey("appearanceMode")) {
    if (dict->GetType("appearanceMode") != VTYPE_STRING) {
      return false;
    }
    appearance_mode = dict->GetString("appearanceMode");
    if (!IsAllowedAppearanceMode(appearance_mode)) {
      return false;
    }
  }

  std::string cache_dir, download_dir;
  if (dict->HasKey("cacheDir") && dict->GetType("cacheDir") == VTYPE_STRING)
    cache_dir = dict->GetString("cacheDir");
  if (dict->HasKey("downloadDir") && dict->GetType("downloadDir") == VTYPE_STRING)
    download_dir = dict->GetString("downloadDir");

  if (normalized_json) {
    *normalized_json = BuildSettingsJson(search_engine, history_enabled,
                                        downloads_enabled, startup_behavior,
                                        startup_urls, https_only,
                                        block_insecure, appearance_mode,
                                        custom_engines, cache_dir, download_dir);
  }
  return true;
}

std::string LoadSettingsJson() {
  const std::filesystem::path fspath = GetAppDataDir() / "settings.json";
  if (fspath.empty()) {
    return GetDefaultSettingsJson();
  }

  std::ifstream input(fspath);
  std::string normalized = GetDefaultSettingsJson();
  if (!input.is_open()) {
    return normalized;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!NormalizeSettingsJson(buffer.str(), &normalized)) {
    std::ofstream rewrite(fspath);
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

  const std::filesystem::path fspath = GetAppDataDir() / "settings.json";
  if (fspath.empty()) {
    return false;
  }

  std::ofstream output(fspath);
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
  return GetExecutableDir() + "/ui/" + BrowserPageHtmlName(page_name);
}

std::string GetBrowserPageDevUrl(const std::string& dev_ui_url,
                                 const std::string& url) {
  std::string page_name;
  if (dev_ui_url.empty() || !ExtractBrowserPageName(url, &page_name)) {
    return "";
  }
  return dev_ui_url + "/" + BrowserPageHtmlName(page_name) +
         ExtractBrowserPageSuffix(url);
}

bool IsPersistableWebUrl(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

bool IsLocalFilesystemPathLike(const std::string& url) {
  if (url.empty()) {
    return false;
  }
  if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 ||
      url.rfind("browser://", 0) == 0 || url.rfind("file://", 0) == 0 ||
      url.rfind("data:", 0) == 0 || url.rfind("blob:", 0) == 0 ||
      url.rfind("about:", 0) == 0 || url.rfind("chrome://", 0) == 0 ||
      url.rfind("chrome-devtools://", 0) == 0 ||
      url.rfind("chrome-extension://", 0) == 0 ||
      url.rfind("chrome-search://", 0) == 0 ||
      url.rfind("chrome-untrusted://", 0) == 0 ||
      url.rfind("devtools://", 0) == 0) {
    return false;
  }
  if (url[0] == '/' || url[0] == '~' || url.rfind("\\\\", 0) == 0) {
    return true;
  }
  if (url.size() >= 3 && std::isalpha(static_cast<unsigned char>(url[0])) &&
      url[1] == ':' && (url[2] == '/' || url[2] == '\\')) {
    return true;
  }
  return url.find('/') != std::string::npos || url.find('\\') != std::string::npos;
}

bool IsAllowedHttpUrl(const std::string& url) {
  if (url.rfind("https://", 0) == 0) {
    return true;
  }
  if (url.rfind("http://", 0) != 0) {
    return false;
  }

  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return false;
  }

  const std::string host = CefString(&parts.host).ToString();
  return IsLoopbackHost(host);
}

bool IsAllowedStartupUrl(const std::string& url) {
  if (IsAllowedBrowserPageUrl(url)) {
    return true;
  }
  if (!IsPersistableWebUrl(url)) {
    return false;
  }

  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return false;
  }

  return !CefString(&parts.host).ToString().empty();
}

namespace {
// Canonical list of UI page filenames bundled with the browser. Used by
// IsInternalUiPagePath (scheme-agnostic suffix match), IsInternalBrowserUiUrl
// (security gate — adds scheme restriction), and the history filter. Keep in
// sync with ui/vite.config.js rollupOptions.input.
const char* const kInternalUiPages[] = {
    "/index.html",         "/appmenu.html",        "/newtab.html",
    "/settings.html",      "/findbar.html",        "/downloads.html",
    "/downloadsbar.html",  "/zoombar.html",        "/history.html",
    "/bookmarks.html",     "/bookmarkbar.html",    "/security.html",
    "/insecure-blocked.html",
    "/pdfviewer.html",     "/certificate.html",    "/imagepreview.html",
    "/cleardata.html",     "/sitedata.html",       "/workspace.html",
    "/qr.html",            "/linkpreview.html",   "/console.html",
    "/blockedpopup.html",  "/downloadrequest.html"};
}  // namespace

bool IsInternalUiPagePath(const std::string& url) {
  // Strip query/fragment so we suffix-match the path only. Without this,
  // `…/newtab.html?x=1` would fail the suffix check.
  std::string base = url;
  const size_t q = base.find_first_of("?#");
  if (q != std::string::npos) {
    base = base.substr(0, q);
  }
  for (const char* suffix : kInternalUiPages) {
    const size_t slen = std::strlen(suffix);
    if (base.size() >= slen &&
        base.compare(base.size() - slen, slen, suffix) == 0) {
      return true;
    }
  }
  return false;
}

bool IsInternalBrowserUiUrl(const std::string& url) {
  if (url.rfind("browser://", 0) == 0) {
    return true;
  }
  // Web origins (http/https) match separately via the dev-ui-url gate at the
  // call site — never here. file:// is also never trusted; production UI is
  // served through browser:// so local files cannot gain bridge privileges by
  // using an allowlisted filename like /tmp/index.html.
  return false;
}

bool IsInternalUiUrl(const std::string& url) {
  if (url.rfind("browser://", 0) == 0) {
    return true;
  }
  if (url.rfind("file://", 0) == 0) {
    return false;
  }
  return IsInternalUiPagePath(url);
}

namespace {

const char* kTrackingParams[] = {
  "utm_source", "utm_medium", "utm_campaign", "utm_term", "utm_content",
  "fbclid", "gclid", "gbraid", "wbraid", "msclkid", "twclid", "igshid",
  "mc_cid", "mc_eid",
  "_ga", "_gl",
  "yclid", "dclid",
};

bool IsTrackingParam(const std::string& key) {
  for (auto* param : kTrackingParams) {
    if (key == param) return true;
  }
  return false;
}

std::string StripTrackingParams(const std::string& query) {
  if (query.empty()) return query;

  std::istringstream stream(query);
  std::string segment;
  std::string cleaned;
  bool first = true;

  while (std::getline(stream, segment, '&')) {
    std::string key = segment;
    auto eq = segment.find('=');
    if (eq != std::string::npos) {
      key = segment.substr(0, eq);
    }

    if (IsTrackingParam(key)) continue;

    if (!first) cleaned += "&";
    cleaned += segment;
    first = false;
  }

  return cleaned;
}

}  // namespace

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
  std::string query = CefString(&parts.query).ToString();

  if (path.empty() || path == "/") {
    path = "/";
  } else {
    while (path.size() > 1 && path.back() == '/') {
      path.pop_back();
    }
  }

  query = StripTrackingParams(query);

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

std::string ExtractOrigin(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) return {};
  std::string scheme = CefString(&parts.scheme).ToString();
  std::string host = CefString(&parts.host).ToString();
  std::string port = CefString(&parts.port).ToString();
  if (scheme.empty() || host.empty()) {
    if (url.rfind("file://", 0) == 0) return "file://";
    return {};
  }
  std::string origin = scheme + "://" + host;
  if (!port.empty()) {
    if ((scheme == "http" && port != "80") ||
        (scheme == "https" && port != "443") ||
        (scheme != "http" && scheme != "https")) {
      origin += ":" + port;
    }
  }
  return origin;
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

namespace {

// 32 MB cap on encoded PNG output — anything larger gets rejected before we
// base64-encode and ship it across the message router.
constexpr size_t kMaxTiffPngBytes = 32 * 1024 * 1024;

bool EnsureVipsInit() {
  static std::once_flag flag;
  static bool ok = false;
  std::call_once(flag, []() {
    ok = (VIPS_INIT("otf-browser") == 0);
  });
  return ok;
}

bool FinishVipsToBase64(VipsImage* in, int page, std::string& out_png_base64, int& out_page_count) {
  int n_pages = 1;
  if (vips_image_get_typeof(in, "n-pages") == G_TYPE_INT) {
    vips_image_get_int(in, "n-pages", &n_pages);
  }
  out_page_count = n_pages;

  void* buf = nullptr;
  size_t len = 0;
  if (vips_pngsave_buffer(in, &buf, &len, NULL)) {
    vips_error_clear();
    g_object_unref(in);
    return false;
  }
  if (len > kMaxTiffPngBytes) {
    g_free(buf);
    g_object_unref(in);
    return false;
  }

  CefString base64_str = CefBase64Encode(buf, len);
  out_png_base64 = "data:image/png;base64," + base64_str.ToString();

  g_free(buf);
  g_object_unref(in);
  return true;
}

int CountVipsPages(const std::string& image_path) {
  VipsImage* in = vips_image_new_from_file(image_path.c_str(), NULL);
  if (!in) {
    vips_error_clear();
    return 1;
  }
  int n_pages = 1;
  if (vips_image_get_typeof(in, "n-pages") == G_TYPE_INT) {
    vips_image_get_int(in, "n-pages", &n_pages);
  }
  g_object_unref(in);
  return n_pages < 1 ? 1 : n_pages;
}

}  // namespace

bool IsTiffUrl(const std::string& url) {
  // Strip query/fragment then test the lowercase suffix.
  size_t end = url.find_first_of("?#");
  std::string path = (end == std::string::npos) ? url : url.substr(0, end);
  std::transform(path.begin(), path.end(), path.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  auto ends_with = [](const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
  };
  return ends_with(path, ".tif") || ends_with(path, ".tiff");
}

bool IsSupportedImageUrl(const std::string& url) {
  size_t end = url.find_first_of("?#");
  std::string path = (end == std::string::npos) ? url : url.substr(0, end);
  std::transform(path.begin(), path.end(), path.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  auto ends_with = [](const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
  };
  return ends_with(path, ".png") || ends_with(path, ".gif") ||
         ends_with(path, ".jpg") || ends_with(path, ".jpeg") ||
         ends_with(path, ".webp") || ends_with(path, ".bmp") ||
         ends_with(path, ".ico") || ends_with(path, ".svg") ||
         ends_with(path, ".avif") || IsTiffUrl(path);
}

bool IsSupportedDocumentUrl(const std::string& url) {
  size_t end = url.find_first_of("?#");
  std::string path = (end == std::string::npos) ? url : url.substr(0, end);
  std::transform(path.begin(), path.end(), path.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  auto ends_with = [](const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
  };
  return ends_with(path, ".pdf") || ends_with(path, ".txt") ||
         ends_with(path, ".json") || ends_with(path, ".jsonl") ||
         ends_with(path, ".xml") ||
         ends_with(path, ".csv") || ends_with(path, ".md") ||
         ends_with(path, ".js") || ends_with(path, ".ts") ||
         ends_with(path, ".py") || ends_with(path, ".html") ||
         ends_with(path, ".css") || ends_with(path, ".yaml") ||
         ends_with(path, ".yml") || ends_with(path, ".toml") ||
         ends_with(path, ".sh") || ends_with(path, ".bash") ||
         ends_with(path, ".log") || ends_with(path, ".ini") ||
         ends_with(path, ".cfg") || ends_with(path, ".conf") ||
         ends_with(path, ".c") || ends_with(path, ".cpp") ||
         ends_with(path, ".h") || ends_with(path, ".hpp") ||
         ends_with(path, ".rs") || ends_with(path, ".go") ||
         ends_with(path, ".java") || ends_with(path, ".rb") ||
         ends_with(path, ".sql") || ends_with(path, ".r") ||
         ends_with(path, ".lua") || ends_with(path, ".php") ||
         ends_with(path, ".swift") || ends_with(path, ".kt") ||
         ends_with(path, ".tex") || path == "makefile" ||
         path == "Makefile" || path == "GNUmakefile";
}

std::string GuessDocumentMimeType(const std::string& url) {
  size_t end = url.find_first_of("?#");
  std::string path = (end == std::string::npos) ? url : url.substr(0, end);
  auto ends_with = [&](std::string_view suffix) {
    return path.size() >= suffix.size() &&
           path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  if (ends_with(".pdf")) return "application/pdf";
  if (ends_with(".json")) return "application/json";
  if (ends_with(".jsonl")) return "application/json";
  if (ends_with(".xml")) return "application/xml";
  if (ends_with(".html")) return "text/html";
  if (ends_with(".css")) return "text/css";
  if (ends_with(".csv")) return "text/csv";
  if (ends_with(".yaml") || ends_with(".yml")) return "text/yaml";
  if (ends_with(".toml")) return "text/plain";
  if (ends_with(".md")) return "text/markdown";
  if (ends_with(".js")) return "text/javascript";
  if (ends_with(".ts")) return "text/typescript";
  if (ends_with(".py")) return "text/x-python";
  if (ends_with(".sh") || ends_with(".bash")) return "text/x-shellscript";
  if (ends_with(".sql")) return "text/x-sql";
  if (ends_with(".c") || ends_with(".cpp") || ends_with(".h") || ends_with(".hpp"))
    return "text/x-c";
  if (ends_with(".rs")) return "text/x-rust";
  if (ends_with(".go")) return "text/x-go";
  if (ends_with(".java")) return "text/x-java";
  if (ends_with(".rb")) return "text/x-ruby";
  if (ends_with(".lua")) return "text/x-lua";
  if (ends_with(".php")) return "text/x-php";
  if (ends_with(".tex")) return "text/x-tex";
  if (ends_with(".log") || ends_with(".ini") || ends_with(".cfg") || ends_with(".conf"))
    return "text/plain";
  return "text/plain";
}

bool DecodeTiffToPngBase64(const std::string& tiff_path, int page, std::string& out_png_base64, int& out_page_count) {
  if (!EnsureVipsInit()) return false;
  VipsImage* in = vips_image_new_from_file(tiff_path.c_str(), "page", page, NULL);
  if (!in) {
    vips_error_clear();
    return false;
  }
  if (!FinishVipsToBase64(in, page, out_png_base64, out_page_count)) {
    return false;
  }
  out_page_count = std::max(out_page_count, CountVipsPages(tiff_path));
  return true;
}

bool DecodeTiffBufferToPngBase64(const void* data, size_t size, int page, std::string& out_png_base64, int& out_page_count) {
  if (!EnsureVipsInit() || !data || size == 0) return false;
  VipsImage* in = vips_image_new_from_buffer(data, size, "", "page", page, NULL);
  if (!in) {
    vips_error_clear();
    return false;
  }
  return FinishVipsToBase64(in, page, out_png_base64, out_page_count);
}

ImagePreviewPayload BuildImagePreviewPayload(const std::string& url, int page) {
  ImagePreviewPayload payload;
  payload.display_url = url;
  payload.page_count = 1;
  if (!IsTiffUrl(url)) return payload;
  // Local file access is intentionally disabled for browser content. Remote
  // TIFFs are decoded by the renderer-driven async path.
  return payload;
}

// ---------------------------------------------------------------------------
// Configurable storage paths
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path& ActiveDataDir() {
  static std::filesystem::path p;
  return p;
}
std::filesystem::path& ActiveCacheDir() {
  static std::filesystem::path p;
  return p;
}
std::filesystem::path& ActiveDownloadsDir() {
  static std::filesystem::path p;
  return p;
}
bool& PathsLocked() {
  static bool locked = false;
  return locked;
}

// Reads a single string value from settings.json by key. Returns empty string
// if the key is missing, null, or not a string.
std::string ReadSettingsString(const std::string& key) {
  std::string raw = LoadSettingsJson();
  CefRefPtr<CefValue> root = CefParseJSON(raw, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) return {};
  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict || !dict->HasKey(key) || dict->GetType(key) != VTYPE_STRING) return {};
  return dict->GetString(key);
}

}  // namespace

void LockStoragePaths() {
  if (PathsLocked()) return;
  ActiveDataDir() = GetAppDataDir();

  const auto configured_cache = GetConfiguredCacheDir();
  ActiveCacheDir() = configured_cache.empty() ? GetAppCacheDir() : configured_cache;

  const auto configured_downloads = GetConfiguredDownloadsDir();
  ActiveDownloadsDir() = configured_downloads.empty() ? std::filesystem::path(GetDownloadsDir()) : configured_downloads;

  PathsLocked() = true;
}

bool StoragePathsLocked() {
  return PathsLocked();
}

std::filesystem::path GetActiveDataDir() {
  if (PathsLocked() && !ActiveDataDir().empty()) return ActiveDataDir();
  return GetAppDataDir();
}

std::filesystem::path GetActiveCacheDir() {
  if (PathsLocked() && !ActiveCacheDir().empty()) return ActiveCacheDir();
  return GetAppCacheDir();
}

std::filesystem::path GetActiveDownloadsDir() {
  if (PathsLocked() && !ActiveDownloadsDir().empty()) return ActiveDownloadsDir();
  return GetDownloadsDir();
}

std::filesystem::path GetConfiguredCacheDir() {
  std::string s = ReadSettingsString("cacheDir");
  if (s.empty()) return {};
  return Utf8Path(s);
}

std::filesystem::path GetConfiguredDownloadsDir() {
  std::string s = ReadSettingsString("downloadDir");
  if (s.empty()) return {};
  return Utf8Path(s);
}

std::string GetPendingPathsFilePath() {
  const auto dir = GetAppDataDir();
  if (dir.empty()) return "";
  return PathToUtf8(dir / "pending_paths.json");
}

std::string LoadPendingPathsJson() {
  const std::string fspath = GetPendingPathsFilePath();
  if (fspath.empty()) return "{}";
  std::string content = ReadFileText(fspath);
  if (content.empty()) return "{}";
  CefRefPtr<CefValue> root = CefParseJSON(content, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) return "{}";
  return content;
}

bool SavePendingPathsJson(const std::string& json) {
  CefRefPtr<CefValue> root = CefParseJSON(json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!root || root->GetType() != VTYPE_DICTIONARY) return false;
  const std::string fspath = GetPendingPathsFilePath();
  if (fspath.empty()) return false;
  std::filesystem::create_directories(std::filesystem::path(fspath).parent_path());
  return WriteFileText(fspath, json);
}

void ApplyPendingPathsOnStartup() {
  // Read pending_paths.json, merge cacheDir/downloadDir into settings.json,
  // then delete the pending file. This runs before LockStoragePaths() so
  // the new values become the active ones.
  const std::string pending_raw = LoadPendingPathsJson();
  if (pending_raw == "{}") return;

  CefRefPtr<CefValue> pending_root =
      CefParseJSON(pending_raw, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!pending_root || pending_root->GetType() != VTYPE_DICTIONARY) return;
  CefRefPtr<CefDictionaryValue> pending_dict = pending_root->GetDictionary();
  if (!pending_dict) return;

  // Read current settings
  const std::string settings_raw = LoadSettingsJson();
  CefRefPtr<CefValue> settings_root =
      CefParseJSON(settings_raw, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!settings_root || settings_root->GetType() != VTYPE_DICTIONARY) return;
  CefRefPtr<CefDictionaryValue> settings_dict = settings_root->GetDictionary();
  if (!settings_dict) return;

  bool changed = false;
  if (pending_dict->HasKey("cacheDir") &&
      pending_dict->GetType("cacheDir") == VTYPE_STRING) {
    settings_dict->SetString("cacheDir", pending_dict->GetString("cacheDir"));
    changed = true;
  }
  if (pending_dict->HasKey("downloadDir") &&
      pending_dict->GetType("downloadDir") == VTYPE_STRING) {
    settings_dict->SetString("downloadDir", pending_dict->GetString("downloadDir"));
    changed = true;
  }

  if (!changed) return;

  // Write updated settings
  CefRefPtr<CefValue> out_root = CefValue::Create();
  out_root->SetDictionary(settings_dict);
  CefString json_str = CefWriteJSON(out_root, JSON_WRITER_DEFAULT);
  if (!json_str.empty()) {
    const std::string out_json = json_str.ToString();
    std::string normalized;
    if (NormalizeSettingsJson(out_json, &normalized)) {
      const auto fspath = GetAppDataDir() / "settings.json";
      if (!fspath.empty()) {
        WriteFileText(PathToUtf8(fspath), normalized);
      }
    }
  }

  // Delete pending file
  const std::string pending_fspath = GetPendingPathsFilePath();
  if (!pending_fspath.empty()) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(pending_fspath), ec);
  }
}

// Path validation -----------------------------------------------------------

bool IsProtectedSystemPath(const std::filesystem::path& p) {
  if (p.empty()) return true;
  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::weakly_canonical(p, ec);
  if (ec) return true;

#if defined(_WIN32)
  // Windows protected locations
  const auto resolved_str = resolved.wstring();
  // System root (C:\Windows, etc.)
  WCHAR sysdir[MAX_PATH];
  if (GetSystemDirectoryW(sysdir, MAX_PATH) &&
      resolved_str.find(std::wstring(sysdir)) == 0)
    return true;
  // Program Files
  WCHAR progfiles[MAX_PATH];
  if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, progfiles) == S_OK &&
      resolved_str.find(std::wstring(progfiles)) == 0)
    return true;
  WCHAR progfilesx86[MAX_PATH];
  if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, progfilesx86) == S_OK &&
      resolved_str.find(std::wstring(progfilesx86)) == 0)
    return true;
  // System32
  WCHAR sysnative[MAX_PATH];
  if (GetSystemDirectoryW(sysnative, MAX_PATH) &&
      resolved_str.find(std::wstring(sysnative)) == 0)
    return true;
  // Drive root
  if (resolved_str.size() <= 3 && resolved_str.back() == L'\\')
    return true;
#else
  const auto resolved_str = resolved.string();
  // Root filesystem
  if (resolved_str == "/") return true;
  // Standard Unix system dirs
  const std::vector<std::string> protected_dirs = {
    "/bin", "/sbin", "/usr", "/etc", "/lib", "/lib64",
    "/proc", "/sys", "/dev", "/boot", "/opt", "/var"
  };
  for (const auto& d : protected_dirs) {
    if (resolved_str == d || resolved_str.rfind(d + "/", 0) == 0)
      return true;
  }
  // Home dir is also protected (unless it's the only option for downloads)
  // We allow home subdirectories but not the home dir itself
  const std::string home = GetHomeDir();
  if (!home.empty() && PathToUtf8(resolved) == home) return true;
#endif
  return false;
}

bool TestDirectoryWriteAccess(const std::filesystem::path& p) {
  if (p.empty()) return false;
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  if (ec) return false;
  // Create and delete a test file to verify write access
  auto test_file = p / ".otf_write_test_XXXXXX";
  // Use a deterministic name since we can't use mkstemp with fstream
  test_file = p / ".otf_write_test";
  {
    std::ofstream f(test_file);
    if (!f.is_open()) return false;
    f << "test";
  }
  std::filesystem::remove(test_file, ec);
  return true;
}

std::string ValidateStoragePath(const std::string& path,
                                const std::string& purpose) {
  if (path.empty()) return "Path cannot be empty.";

  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::weakly_canonical(
      std::filesystem::absolute(Utf8Path(path), ec), ec);
  if (ec) return "Path is invalid or cannot be resolved.";

  // Must be absolute
  if (!resolved.is_absolute()) return "Path must be absolute.";

  // Check for symlinks
  if (std::filesystem::is_symlink(resolved)) {
    return "Symbolic links are not allowed for storage paths.";
  }

  // Check if path exists and is a directory
  if (std::filesystem::exists(resolved, ec) && !std::filesystem::is_directory(resolved, ec)) {
    return "Path exists but is not a directory.";
  }

  // Check protected system locations
  if (IsProtectedSystemPath(resolved)) {
    return "This location is protected and cannot be used as a storage directory.";
  }

  // Check app installation directory
  const std::string exec_dir = GetExecutableDir();
  if (!exec_dir.empty()) {
    const auto exec_path = Utf8Path(exec_dir);
    if (PathToUtf8(resolved) == exec_dir ||
        PathToUtf8(resolved.lexically_normal()).rfind(PathToUtf8(exec_path.lexically_normal()) + "/", 0) == 0) {
      return "Cannot use the application installation directory.";
    }
  }

  // Check write access
  if (!TestDirectoryWriteAccess(resolved)) {
    return "Cannot write to this directory. Check permissions.";
  }

  // Check overlap with browser data directory
  const auto data_dir = GetActiveDataDir();
  if (purpose == "cache" && !data_dir.empty()) {
    const auto data_str = PathToUtf8(data_dir.lexically_normal());
    const auto cache_str = PathToUtf8(resolved.lexically_normal());
    if (cache_str == data_str ||
        cache_str.rfind(data_str + "/", 0) == 0) {
      return "Cache directory cannot overlap with the browser data directory.";
    }
    // Also check the reverse: data dir inside cache
    if (data_str.rfind(cache_str + "/", 0) == 0) {
      return "Cache directory cannot contain the browser data directory.";
    }
  }

  // Check overlap with cache directory for downloads
  if (purpose == "downloads") {
    const auto cache_dir = GetActiveCacheDir();
    if (!cache_dir.empty()) {
      const auto cache_str = PathToUtf8(cache_dir.lexically_normal());
      const auto dl_str = PathToUtf8(resolved.lexically_normal());
      if (!cache_str.empty() && dl_str.rfind(cache_str + "/", 0) == 0) {
        return "Downloads directory cannot be inside the cache directory.";
      }
    }
  }

  // Check free disk space (Linux/macOS: statvfs, Windows: GetDiskFreeSpaceEx)
  // Require at least 100 MB free for cache, 50 MB for downloads
  const uint64_t required_bytes = (purpose == "cache") ? 100ULL * 1024 * 1024 : 50ULL * 1024 * 1024;
#if defined(_WIN32)
  ULARGE_INTEGER free_bytes;
  if (GetDiskFreeSpaceExW(resolved.c_str(), &free_bytes, nullptr, nullptr)) {
    // QuadPart is ULONGLONG; compare unsigned-to-unsigned (required_bytes is
    // uint64_t) — a signed LONGLONG cast trips C4018 under /WX.
    if (free_bytes.QuadPart < required_bytes) {
      return std::string("Not enough free disk space. At least ") +
             (purpose == "cache" ? "100 MB" : "50 MB") + " required.";
    }
  }
#else
  struct statvfs vfs;
  if (statvfs(resolved.c_str(), &vfs) == 0) {
    const uint64_t free = static_cast<uint64_t>(vfs.f_frsize) * vfs.f_bavail;
    if (free < required_bytes) {
      return std::string("Not enough free disk space. At least ") +
             (purpose == "cache" ? "100 MB" : "50 MB") + " required.";
    }
  }
#endif

  return "";
}

uint64_t GetDirectorySize(const std::filesystem::path& dir) {
  if (dir.empty()) return 0;
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return 0;
  uint64_t total = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
    if (entry.is_regular_file(ec)) {
      total += entry.file_size(ec);
    }
  }
  return total;
}

// Scans IndexedDB directory for per-origin storage sizes.
// Returns a vector of {origin, sizeBytes} pairs.
static std::vector<std::pair<std::string, uint64_t>> ScanIndexedDbOrigins() {
  std::vector<std::pair<std::string, uint64_t>> results;
  const auto db_dir = GetActiveCacheDir() / "cef" / "Default" / "IndexedDB";
  std::error_code ec;
  if (!std::filesystem::is_directory(db_dir, ec)) return results;
  for (const auto& entry : std::filesystem::directory_iterator(db_dir, ec)) {
    if (!entry.is_directory(ec)) continue;
    const std::string name = entry.path().filename().string();
    // Pattern: https_google.com_0.indexeddb.leveldb or similar
    const size_t idx = name.rfind(".indexeddb.leveldb");
    if (idx == std::string::npos) continue;
    std::string prefix = name.substr(0, idx);
    // Remove trailing _<digits> from the prefix to get the origin
    // e.g. "https_google.com_0" -> "https://google.com"
    size_t last_underscore = prefix.rfind('_');
    if (last_underscore == std::string::npos) continue;
    // Check if the part after last underscore is all digits (it's the schema version marker)
    bool all_digits = true;
    for (size_t i = last_underscore + 1; i < prefix.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(prefix[i]))) { all_digits = false; break; }
    }
    std::string origin_part;
    if (all_digits) {
      origin_part = prefix.substr(0, last_underscore);
    } else {
      origin_part = prefix;
    }
    // Replace underscores in the scheme prefix (first _ after scheme)
    // e.g. "https_google.com" -> "https://google.com"
    size_t scheme_sep = origin_part.find('_');
    if (scheme_sep == std::string::npos) continue;
    std::string origin = origin_part.substr(0, scheme_sep) + "://" + origin_part.substr(scheme_sep + 1);
    if (origin.empty()) continue;
    const uint64_t size = GetDirectorySize(entry.path());
    results.emplace_back(origin, size);
  }
  return results;
}

// Scans CacheStorage (Service Worker cache) directories for per-origin sizes.
// Directory names are opaque hashes, but each contains an index.txt with the
// origin URL stored as a plaintext string (protobuf-embedded).
// Returns a vector of {origin, sizeBytes} pairs.
static std::vector<std::pair<std::string, uint64_t>> ScanCacheStorageOrigins() {
  std::vector<std::pair<std::string, uint64_t>> results;
  const auto cs_dir = GetActiveCacheDir() / "cef" / "Default" / "Service Worker" / "CacheStorage";
  std::error_code ec;
  if (!std::filesystem::is_directory(cs_dir, ec)) return results;
  for (const auto& entry : std::filesystem::directory_iterator(cs_dir, ec)) {
    if (!entry.is_directory(ec)) continue;
    // Each subdirectory is a hash-named origin directory containing cache bodies.
    const auto index_file = entry.path() / "index.txt";
    if (!std::filesystem::is_regular_file(index_file, ec)) continue;
    // Read the index file and search for an http(s):// URL
    std::ifstream ifs(index_file, std::ios::binary);
    if (!ifs) continue;
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();
    // Search for "http://" or "https://" - the origin URL is stored in the
    // protobuf-encoded index.txt as an opaque string field.
    static constexpr std::string_view kHttpPrefix = "http://";
    static constexpr std::string_view kHttpsPrefix = "https://";
    std::string origin;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content.compare(i, kHttpPrefix.size(), kHttpPrefix) == 0 ||
          content.compare(i, kHttpsPrefix.size(), kHttpsPrefix) == 0) {
        // Found a URL - extract up to the first space/control/quote char
        size_t end = i;
        while (end < content.size() && content[end] > 0x1f &&
               content[end] != '"' && content[end] != '<' && content[end] != '>' &&
               content[end] != '[' && content[end] != ']' && content[end] != ' ') {
          ++end;
        }
        const std::string url_str = content.substr(i, end - i);
        origin = ExtractOrigin(url_str);
        if (!origin.empty()) break;
      }
    }
    if (origin.empty()) continue;
    const uint64_t size = GetDirectorySize(entry.path());
    results.emplace_back(std::move(origin), size);
  }
  return results;
}

// Scans Local Storage LevelDB for per-origin data presence.
// Returns a vector of {origin, 0} for each origin found to have localStorage.
// Accurate per-origin sizing would require LevelDB parsing; we detect
// presence by scanning for META:https:// / META:http:// patterns in the
// binary LevelDB files.
static std::vector<std::pair<std::string, uint64_t>> ScanLocalStorageOrigins() {
  std::vector<std::pair<std::string, uint64_t>> results;
  const auto ls_dir = GetActiveCacheDir() / "cef" / "Default" / "Local Storage" / "leveldb";
  std::error_code ec;
  if (!std::filesystem::is_directory(ls_dir, ec)) return results;

  std::string all_data;
  for (const auto& entry : std::filesystem::directory_iterator(ls_dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const auto name = entry.path().filename().string();
    if (name.size() < 4) continue;
    const auto ext = name.substr(name.size() - 4);
    if (ext != ".ldb" && ext != ".log") continue;
    std::ifstream ifs(entry.path(), std::ios::binary);
    if (!ifs) continue;
    all_data.append((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());
  }
  if (all_data.empty()) return results;

  std::set<std::string> origins_found;
  static constexpr std::string_view kMetaHttp = "META:http://";
  static constexpr std::string_view kMetaHttps = "META:https://";
  for (size_t i = 0; i + 5 < all_data.size(); ++i) {
    size_t meta_end = 0;
    if (all_data.compare(i, kMetaHttps.size(), kMetaHttps) == 0)
      meta_end = i + kMetaHttps.size();
    else if (all_data.compare(i, kMetaHttp.size(), kMetaHttp) == 0)
      meta_end = i + kMetaHttp.size();
    else
      continue;
    size_t end = meta_end;
    while (end < all_data.size() && all_data[end] >= 0x20) ++end;
    if (end > meta_end) {
      std::string origin_url = all_data.substr(i + 5, end - i - 5);
      std::string origin = ExtractOrigin(origin_url);
      if (!origin.empty()) origins_found.insert(origin);
    }
  }
  for (const auto& origin : origins_found)
    results.emplace_back(origin, 0);
  return results;
}

static std::string NormalizeOrigin(const std::string& origin) {
  // Strip www. prefix(es) from host and remove default ports (80, 443)
  // so e.g. https://www.www.youtube.com:443 normalizes to https://youtube.com.
  std::string result = origin;
  constexpr std::string_view kDotWww = "://www.";
  size_t pos = result.find(kDotWww);
  if (pos != std::string::npos) {
    size_t start = pos + 3; // after "://"
    while (result.compare(start, 4, "www.") == 0) {
      result.erase(start, 4); // remove each "www."
      // after erasing, check if there's another "www." at the same position
    }
  }
  // Strip default port :443 for https, :80 for http
  if (result.compare(0, 8, "https://") == 0) {
    size_t port_pos = result.find(':');
    if (port_pos != std::string::npos) {
      auto port_str = result.substr(port_pos + 1);
      size_t slash = port_str.find('/');
      if (slash != std::string::npos) port_str = port_str.substr(0, slash);
      if (port_str == "443") result.erase(port_pos);
    }
  } else if (result.compare(0, 7, "http://") == 0) {
    size_t port_pos = result.find(':');
    if (port_pos != std::string::npos) {
      auto port_str = result.substr(port_pos + 1);
      size_t slash = port_str.find('/');
      if (slash != std::string::npos) port_str = port_str.substr(0, slash);
      if (port_str == "80") result.erase(port_pos);
    }
  }
  return result;
}

std::string BuildSiteUsageJson(const std::vector<std::string>& extra_origins,
                               const std::map<std::string, uint64_t>& cookie_sizes,
                               const std::map<std::string, uint64_t>& cookie_counts,
                               const std::map<std::string, uint64_t>& local_storage_sizes) {
  auto idb_origins = ScanIndexedDbOrigins();
  auto cs_origins = ScanCacheStorageOrigins();
  auto ls_origins = ScanLocalStorageOrigins();

  // Build normalized maps by origin for each storage type
  auto insert_into = [](std::map<std::string, uint64_t>& map,
                         const std::vector<std::pair<std::string, uint64_t>>& vec) {
    for (const auto& [origin, size] : vec)
      map[NormalizeOrigin(origin)] += size;
  };
  std::map<std::string, uint64_t> idb_map;
  std::map<std::string, uint64_t> cs_map;
  insert_into(idb_map, idb_origins);
  insert_into(cs_map, cs_origins);
  // Merge caller-provided localStorage sizes with scanned ones
  std::map<std::string, uint64_t> ls_map;
  for (const auto& [origin, size] : local_storage_sizes)
    ls_map[NormalizeOrigin(origin)] += size;
  for (const auto& [origin, size] : ls_origins)
    ls_map[NormalizeOrigin(origin)] += size;
  // Normalize cookie_sizes
  std::map<std::string, uint64_t> cookies_map;
  for (const auto& [origin, size] : cookie_sizes)
    cookies_map[NormalizeOrigin(origin)] += size;
  // Normalize cookie_counts
  std::map<std::string, uint64_t> cookie_count_map;
  for (const auto& [origin, count] : cookie_counts)
    cookie_count_map[NormalizeOrigin(origin)] += count;
  // Normalize extra_origins
  std::set<std::string> extra_set;
  for (const auto& origin : extra_origins)
    extra_set.insert(NormalizeOrigin(origin));

  // Collect all unique normalized origins
  std::set<std::string> all_origins;
  for (const auto& [origin, _] : idb_map) all_origins.insert(origin);
  for (const auto& [origin, _] : cs_map) all_origins.insert(origin);
  for (const auto& [origin, _] : ls_map) all_origins.insert(origin);
  for (const auto& [origin, _] : cookies_map) all_origins.insert(origin);
  for (const auto& [origin, _] : cookie_count_map) all_origins.insert(origin);
  for (const auto& origin : extra_set) all_origins.insert(origin);

  std::string json = "[";
  bool first = true;
  for (const auto& origin : all_origins) {
    if (!first) json += ",";
    first = false;
    const uint64_t idb = idb_map[origin];
    const uint64_t cs = cs_map[origin];
    const uint64_t ls = ls_map[origin];
    const uint64_t cookies = cookies_map[origin];
    const uint64_t cookie_count = cookie_count_map[origin];
    const uint64_t total = idb + cs + ls + cookies;
    json += JsonObjectBuilder()
                .AddString("origin", origin)
                .AddRaw("storageBytes", std::to_string(total))
                .AddRaw("indexedDB", std::to_string(idb))
                .AddRaw("cacheStorage", std::to_string(cs))
                .AddRaw("localStorage", std::to_string(ls))
                .AddRaw("cookies", std::to_string(cookies))
                .AddRaw("cookieCount", std::to_string(cookie_count))
                .Build();
  }
  json += "]";
  return json;
}

std::string BuildStorageTotalsJson() {
  std::error_code ec;
  const auto profile_dir = GetActiveCacheDir() / "cef" / "Default";

  const uint64_t http_cache = GetDirectorySize(profile_dir / "Cache");
  const uint64_t indexed_db = GetDirectorySize(profile_dir / "IndexedDB");
  const uint64_t localStorage = GetDirectorySize(profile_dir / "Local Storage");
  const uint64_t sessionStorage = GetDirectorySize(profile_dir / "Session Storage");
  const uint64_t blob_storage = GetDirectorySize(profile_dir / "blob_storage");
  const uint64_t file_system = GetDirectorySize(profile_dir / "File System");
  // Service Workers disabled in this browser - skip SW script dirs
  const uint64_t cs_total = GetDirectorySize(profile_dir / "Service Worker" / "CacheStorage");
  // Cookies file size
  uint64_t cookies = 0;
  if (std::filesystem::is_regular_file(profile_dir / "Cookies", ec))
    cookies = std::filesystem::file_size(profile_dir / "Cookies", ec);
  // Code Cache
  const uint64_t code_cache = GetDirectorySize(profile_dir / "Code Cache");

  return JsonObjectBuilder()
      .AddRaw("httpCache", std::to_string(http_cache))
      .AddRaw("indexedDB", std::to_string(indexed_db))
      .AddRaw("cacheStorage", std::to_string(cs_total))
      .AddRaw("localStorage", std::to_string(localStorage))
      .AddRaw("sessionStorage", std::to_string(sessionStorage))
      .AddRaw("blobStorage", std::to_string(blob_storage))
      .AddRaw("fileSystem", std::to_string(file_system))
      .AddRaw("cookies", std::to_string(cookies))
      .AddRaw("codeCache", std::to_string(code_cache))
      .Build();
}

} // namespace otf
