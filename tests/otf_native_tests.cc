#include "otf_utils.h"
#include "otf_store.h"

// Release builds set NDEBUG, which makes assert() a no-op. That would turn
// this entire suite into a placeholder that exits 0 without checking anything.
// Keep assertions live in the test translation unit regardless of build type.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void TestJsonEscape() {
  const std::string input = std::string("quote\" slash\\ line\n tab\t ctrl") +
                            static_cast<char>(0x01);
  const std::string expected =
      "quote\\\" slash\\\\ line\\n tab\\t ctrl\\u0001";
  assert(otf::JsonEscape(input) == expected);
  assert(otf::JsonString("ok") == "\"ok\"");
  // Note: plain narrow string literals so the assert compiles under C++20
  // (u8"..." has type const char8_t* in C++20 and isn't convertible to
  // std::string). Snowman is UTF-8 bytes E2 98 83.
  assert(otf::JsonEscape("snowman \xe2\x98\x83") == "snowman \xe2\x98\x83");
}

void TestSettingsValidation() {
  // TODO: NormalizeSettingsJson uses CefParseJSON, which aborts ("CefValue
  // CppToC called with invalid version -1") when invoked outside a running
  // CEF process. Re-enable once we either (a) stub CEF in the test target
  // or (b) move settings parsing off CEF.
  std::cerr << "[SKIPPED] TestSettingsValidation (needs CEF runtime)\n";
}

void TestSettingsLoadAndSave() {
  // TODO: same CEF dependency as TestSettingsValidation. Re-enable when
  // settings parsing no longer requires CefParseJSON or when the test target
  // can stand up enough of CEF.
  std::cerr << "[SKIPPED] TestSettingsLoadAndSave (needs CEF runtime)\n";
}

void TestBrowserPageAllowlist() {
  assert(otf::IsAllowedBrowserPageUrl("browser://newtab"));
  assert(otf::IsAllowedBrowserPageUrl("browser://settings/"));
  assert(otf::IsAllowedBrowserPageUrl("browser://findbar"));
  assert(otf::IsAllowedBrowserPageUrl("browser://history"));
  assert(otf::IsAllowedBrowserPageUrl("browser://bookmarks"));
  assert(!otf::IsAllowedBrowserPageUrl("https://example.com"));
  assert(otf::GetBrowserPageDevUrl("http://localhost:3000", "browser://settings") ==
         "http://localhost:3000/settings.html");
  assert(otf::GetBrowserPageDevUrl(
             "http://localhost:3000",
             "browser://insecure-blocked?url=http%3A%2F%2Fexample.com") ==
         "http://localhost:3000/insecure-blocked.html?url=http%3A%2F%2Fexample.com");
  assert(otf::GetBrowserPageFilePath("browser://insecure-blocked") ==
         otf::GetExecutableDir() + "/ui/insecure-blocked.html");
}

void TestHttpUrlGate() {
  assert(otf::IsAllowedHttpUrl("https://example.com"));
  assert(otf::IsAllowedHttpUrl("http://localhost:3000"));
  assert(otf::IsAllowedHttpUrl("http://127.0.0.1:8080"));
  assert(otf::IsAllowedHttpUrl("http://[::1]:3000"));
  assert(!otf::IsAllowedHttpUrl("http://example.com"));
  assert(!otf::IsAllowedHttpUrl("http://192.168.1.10"));
}

void TestCloseSelection() {
  const std::vector<int> ids = {1, 2, 3};
  assert(otf::SelectNextActiveTabId(ids, 2) == 3);
  assert(otf::SelectNextActiveTabId(ids, 3) == 2);
  assert(otf::SelectNextActiveTabId(ids, 99) == 1);
  assert(otf::SelectNextActiveTabId({7}, 7) == -1);
  assert(otf::SelectNextActiveTabId({}, 7) == -1);
}

void TestZoomHelpers() {
  // Use std::lround — the percent-to-level-to-percent round trip loses a tiny
  // amount of precision via log2, so 110 comes back as 109.999…; truncating
  // would give 109. lround matches what the production code does for the
  // user-facing percent.
  assert(std::lround(otf::ZoomLevelToPercent(otf::ZoomReset())) == 100);
  assert(std::lround(otf::ZoomLevelToPercent(otf::ZoomIn(otf::ZoomReset()))) == 110);
  assert(std::lround(otf::ZoomLevelToPercent(otf::ZoomOut(otf::ZoomReset()))) == 90);
}

void TestStorePersistence() {
  fs::path temp_home = fs::temp_directory_path() / "otf-browser-store-tests-home";
  fs::remove_all(temp_home);
  fs::create_directories(temp_home);
  setenv("HOME", temp_home.c_str(), 1);

  otf::OtfStore store;
  assert(store.IsReady());

  assert(store.RecordVisit("https://example.com", "Example", "link"));
  assert(store.RecordVisit("http://localhost:3000/downloads.html", "Downloads", "link"));
  assert(store.UpdateHistoryTitle("https://example.com", "Example Title"));
  const auto history = store.GetHistory();
  assert(history.size() == 1);
  assert(history[0].url == "https://example.com");
  assert(history[0].title == "Example Title");

  const int download_id = store.CreateDownload(
      "https://example.com/file.zip", "https://example.com/file.zip",
      "/tmp/file.zip", "file.zip", "application/zip", "starting");
  assert(download_id > 0);
  otf::PersistedDownload item;
  item.id = download_id;
  item.url = "https://example.com/file.zip";
  item.original_url = "https://example.com/file.zip";
  item.target_path = "/tmp/file.zip";
  item.filename = "file.zip";
  item.mime_type = "application/zip";
  item.total_bytes = 2048;
  item.received_bytes = 2048;
  item.status = "completed";
  assert(store.UpdateDownload(item));
  assert(store.GetDownloads().size() == 1);

  assert(store.AddBookmark("https://example.com", "Example"));
  assert(store.IsBookmarked("https://example.com"));
  assert(store.IsBookmarked("https://example.com/"));
  assert(store.GetBookmarks().size() == 1);
}

void TestBookmarkNormalization() {
  assert(otf::NormalizeBookmarkUrl("https://example.com") ==
         otf::NormalizeBookmarkUrl("https://example.com/"));
  assert(otf::NormalizeBookmarkUrl("https://example.com/path/") ==
         "https://example.com/path");
}

void TestIsInternalBrowserUiUrl() {
  // browser:// scheme — always trusted.
  assert(otf::IsInternalBrowserUiUrl("browser://newtab"));
  assert(otf::IsInternalBrowserUiUrl("browser://settings?foo=bar"));
  // file:// paths to packaged UI HTML — every page in the manifest.
  // Regression: /index.html was missing from the allowlist; without it the
  // main UI shell can't reach cefQuery and the toolbar appears dead.
  assert(otf::IsInternalBrowserUiUrl("file:///opt/otf/ui/index.html"));
  assert(otf::IsInternalBrowserUiUrl("file:///opt/otf/ui/newtab.html"));
  assert(otf::IsInternalBrowserUiUrl("file:///opt/otf/ui/settings.html"));
  assert(otf::IsInternalBrowserUiUrl("file:///opt/otf/ui/findbar.html"));
  assert(otf::IsInternalBrowserUiUrl("file:///opt/otf/ui/downloads.html"));
  assert(otf::IsInternalBrowserUiUrl("file:///opt/otf/ui/bookmarks.html"));
  // External web origins — always denied.
  assert(!otf::IsInternalBrowserUiUrl("https://example.com"));
  assert(!otf::IsInternalBrowserUiUrl("http://attacker.com/newtab.html"));
  assert(!otf::IsInternalBrowserUiUrl(""));
  // Defensive: a file:// path that ends in a non-allowlisted .html should
  // not be trusted just for being file://.
  assert(!otf::IsInternalBrowserUiUrl("file:///etc/passwd"));
  assert(!otf::IsInternalBrowserUiUrl("file:///home/me/scratch.html"));
}

void TestIsPersistableWebUrl() {
  assert(otf::IsPersistableWebUrl("http://example.com"));
  assert(otf::IsPersistableWebUrl("https://example.com/path?q=1"));
  assert(!otf::IsPersistableWebUrl("browser://newtab"));
  assert(!otf::IsPersistableWebUrl("file:///etc/passwd"));
  assert(!otf::IsPersistableWebUrl("javascript:alert(1)"));
  assert(!otf::IsPersistableWebUrl("data:text/html,x"));
  assert(!otf::IsPersistableWebUrl(""));
}

void TestIsAllowedStartupUrl() {
  // Browser pages and well-formed web URLs are accepted.
  assert(otf::IsAllowedStartupUrl("browser://newtab"));
  assert(otf::IsAllowedStartupUrl("https://example.com"));
  assert(otf::IsAllowedStartupUrl("http://localhost:3000"));
  // Non-persistable schemes are rejected.
  assert(!otf::IsAllowedStartupUrl("javascript:alert(1)"));
  assert(!otf::IsAllowedStartupUrl("file:///etc/passwd"));
  assert(!otf::IsAllowedStartupUrl("data:text/html,x"));
  // Persistable scheme but no host — rejected.
  assert(!otf::IsAllowedStartupUrl("http://"));
}

void TestSecureDeletePragma() {
  fs::path temp_home = fs::temp_directory_path() / "otf-browser-pragma-tests-home";
  fs::remove_all(temp_home);
  fs::create_directories(temp_home);
  setenv("HOME", temp_home.c_str(), 1);

  otf::OtfStore store;
  assert(store.IsReady());
  // Open() must apply `PRAGMA secure_delete = ON` so cleared data is wiped
  // on disk and cannot be recovered by carving the SQLite file.
  assert(store.IsSecureDeleteEnabled());
}

void TestBookmarkRoundTrip() {
  fs::path temp_home = fs::temp_directory_path() / "otf-browser-bookmark-tests-home";
  fs::remove_all(temp_home);
  fs::create_directories(temp_home);
  setenv("HOME", temp_home.c_str(), 1);

  otf::OtfStore store;
  assert(store.IsReady());

  assert(store.AddBookmark("https://example.com/a", "A"));
  assert(store.IsBookmarked("https://example.com/a"));

  auto bookmarks = store.GetBookmarks();
  assert(bookmarks.size() == 1);
  const int id = bookmarks[0].id;
  assert(bookmarks[0].title == "A");

  assert(store.UpdateBookmark(id, "https://example.com/a", "A renamed"));
  bookmarks = store.GetBookmarks();
  assert(bookmarks.size() == 1);
  assert(bookmarks[0].title == "A renamed");

  assert(store.RemoveBookmark(id));
  assert(!store.IsBookmarked("https://example.com/a"));
  assert(store.GetBookmarks().empty());

  // RemoveBookmarkByUrl should normalize and clear matching entries.
  assert(store.AddBookmark("https://example.com/b/", "B"));
  assert(store.IsBookmarked("https://example.com/b"));
  assert(store.RemoveBookmarkByUrl("https://example.com/b"));
  assert(!store.IsBookmarked("https://example.com/b"));
}

void TestZoomBoundaryClamping() {
  // Zoom-out from the minimum stays at the minimum; zoom-in from the maximum
  // stays at the maximum. Catches off-by-one regressions in the percentage
  // ladder.
  const double min_level = otf::PercentToZoomLevel(25.0);
  const double max_level = otf::PercentToZoomLevel(500.0);
  // Sanity: the ladder endpoints round-trip cleanly (lround for FP noise).
  assert(std::lround(otf::ZoomLevelToPercent(min_level)) == 25);
  assert(std::lround(otf::ZoomLevelToPercent(max_level)) == 500);
  // ZoomOut from min must NOT go below 25%.
  assert(std::lround(otf::ZoomLevelToPercent(otf::ZoomOut(min_level))) == 25);
  // ZoomIn from max must NOT exceed 500%.
  assert(std::lround(otf::ZoomLevelToPercent(otf::ZoomIn(max_level))) == 500);
}

}  // namespace

int main() {
  TestJsonEscape();
  TestSettingsValidation();
  TestSettingsLoadAndSave();
  TestBrowserPageAllowlist();
  TestHttpUrlGate();
  TestCloseSelection();
  TestZoomHelpers();
  TestStorePersistence();
  TestBookmarkNormalization();
  TestIsInternalBrowserUiUrl();
  TestIsPersistableWebUrl();
  TestIsAllowedStartupUrl();
  TestSecureDeletePragma();
  TestBookmarkRoundTrip();
  TestZoomBoundaryClamping();
  return 0;
}
