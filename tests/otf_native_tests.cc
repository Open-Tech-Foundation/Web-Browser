#include "otf_utils.h"
#include "otf_store.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
  assert(otf::JsonEscape(u8"snowman \u2603") == u8"snowman \u2603");
}

void TestSettingsValidation() {
  std::string normalized;
  assert(otf::NormalizeSettingsJson("{\"searchEngine\":\"bing\"}", &normalized));
  assert(normalized == "{\"searchEngine\":\"bing\"}");
  assert(!otf::NormalizeSettingsJson("{\"searchEngine\":\"invalid\"}", &normalized));
  assert(!otf::NormalizeSettingsJson("{\"searchEngine\":\"bing\",\"extra\":true}", &normalized));
  assert(!otf::NormalizeSettingsJson("{bad json", &normalized));
}

void TestSettingsLoadAndSave() {
  fs::path temp_home = fs::temp_directory_path() / "otf-browser-native-tests-home";
  fs::remove_all(temp_home);
  fs::create_directories(temp_home);
  setenv("HOME", temp_home.c_str(), 1);

  std::string normalized;
  assert(otf::SaveSettingsJson("{\"searchEngine\":\"duckduckgo\"}", &normalized));
  assert(normalized == "{\"searchEngine\":\"duckduckgo\"}");
  assert(otf::LoadSettingsJson() == normalized);

  std::ofstream corrupt(otf::GetSettingsFilePath());
  corrupt << "{corrupt";
  corrupt.close();

  const std::string default_settings = otf::GetDefaultSettingsJson();
  assert(otf::LoadSettingsJson() == default_settings);

  std::ifstream rewritten(otf::GetSettingsFilePath());
  std::string contents((std::istreambuf_iterator<char>(rewritten)),
                       std::istreambuf_iterator<char>());
  assert(contents == default_settings);
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
  assert(static_cast<int>(otf::ZoomLevelToPercent(otf::ZoomReset())) == 100);
  assert(static_cast<int>(otf::ZoomLevelToPercent(otf::ZoomIn(otf::ZoomReset()))) == 110);
  assert(static_cast<int>(otf::ZoomLevelToPercent(otf::ZoomOut(otf::ZoomReset()))) == 90);
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

}  // namespace

int main() {
  TestJsonEscape();
  TestSettingsValidation();
  TestSettingsLoadAndSave();
  TestBrowserPageAllowlist();
  TestCloseSelection();
  TestZoomHelpers();
  TestStorePersistence();
  TestBookmarkNormalization();
  return 0;
}
