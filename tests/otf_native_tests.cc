#include "otf_utils.h"

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
  assert(!otf::IsAllowedBrowserPageUrl("browser://history"));
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

}  // namespace

int main() {
  TestJsonEscape();
  TestSettingsValidation();
  TestSettingsLoadAndSave();
  TestBrowserPageAllowlist();
  TestCloseSelection();
  return 0;
}
