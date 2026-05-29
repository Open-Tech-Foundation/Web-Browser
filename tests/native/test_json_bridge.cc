#include "otf_utils.h"

#undef NDEBUG
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

// Redirect app-data lookups to an isolated, empty directory. otf_utils' search
// helpers reach settings via LoadSettingsJson() -> CefParseJSON(), which aborts
// ("CefValue ... invalid version -1") in a standalone unit binary where CEF is
// never initialized. Pointing OTF_TEST_HOME at an empty dir means no
// settings.json exists, so LoadSettingsJson() returns the in-memory default and
// the CEF parser is never invoked. Also keeps tests off the real user profile.
void IsolateAppDataHome() {
  namespace fs = std::filesystem;
  const fs::path temp_home = fs::temp_directory_path() / "otf-json-bridge-test";
  fs::remove_all(temp_home);
  fs::create_directories(temp_home);
#if defined(_WIN32)
  _putenv_s("OTF_TEST_HOME", temp_home.string().c_str());
  _putenv_s("OTF_DEV_MODE", "");
#else
  setenv("OTF_TEST_HOME", temp_home.string().c_str(), 1);
  unsetenv("OTF_DEV_MODE");
#endif
}

void TestJsonEscaping() {
  const std::string input = std::string("quote\" slash\\ line\n tab\t ctrl") +
                            static_cast<char>(0x01);
  assert(otf::JsonEscape(input) ==
         "quote\\\" slash\\\\ line\\n tab\\t ctrl\\u0001");
  assert(otf::JsonString("ok") == "\"ok\"");
  assert(otf::HtmlAttrEscape("a&<b>\"'") == "a&amp;&lt;b&gt;&quot;&#39;");
}

void TestJsonObjectBuilder() {
  const std::string json = otf::JsonObjectBuilder()
                               .AddString("key", "value")
                               .AddInt("num", 42)
                               .AddBool("flag", true)
                               .AddNull("empty")
                               .Build();
  assert(json.find("\"key\":\"value\"") != std::string::npos);
  assert(json.find("\"num\":42") != std::string::npos);
  assert(json.find("\"flag\":true") != std::string::npos);
  assert(json.find("\"empty\":null") != std::string::npos);
}

void TestStrictParsers() {
  assert(otf::ParseIntStrict("42").value() == 42);
  assert(otf::ParseIntStrict("-7").value() == -7);
  assert(!otf::ParseIntStrict("").has_value());
  assert(!otf::ParseIntStrict("12abc").has_value());
  assert(!otf::ParseIntStrict(" 12").has_value());
  assert(!otf::ParseIntStrict("99999999999").has_value());

  assert(otf::ParseUint32Strict("4294967295").value() == 4294967295u);
  assert(!otf::ParseUint32Strict("-1").has_value());
  assert(!otf::ParseUint32Strict("4294967296").has_value());

  assert(otf::ParseUint64Strict("18446744073709551615").value() ==
         18446744073709551615ull);
  assert(!otf::ParseUint64Strict("12abc").has_value());
}

void TestLengthPrefixedFields() {
  size_t cursor = 0;
  bool ok = false;
  std::string value = otf::ParseLengthPrefixedField("5:hello4:test", &cursor, &ok);
  assert(ok);
  assert(value == "hello");
  assert(cursor == 7);
  value = otf::ParseLengthPrefixedField("5:hello4:test", &cursor, &ok);
  assert(ok);
  assert(value == "test");
  assert(cursor == 13);

  cursor = 0;
  value = otf::ParseLengthPrefixedField("0:", &cursor, &ok);
  assert(ok);
  assert(value.empty());
  assert(cursor == 2);

  cursor = 0;
  value = otf::ParseLengthPrefixedField("10:short", &cursor, &ok);
  assert(!ok);
  assert(value.empty());

  cursor = 0;
  value = otf::ParseLengthPrefixedField(":missing", &cursor, &ok);
  assert(!ok);

  cursor = 99;
  value = otf::ParseLengthPrefixedField("1:a", &cursor, &ok);
  assert(!ok);
}

void TestSearchEngines() {
  assert(otf::IsAllowedSearchEngineId("google"));
  assert(otf::IsAllowedSearchEngineId("duckduckgo"));
  assert(!otf::IsAllowedSearchEngineId("Google"));
  assert(!otf::IsAllowedSearchEngineId("notarealengine"));
  assert(otf::BuildSearchUrl("google", "hello world") ==
         "https://www.google.com/search?q=hello+world");
  assert(otf::BuildSearchUrl("duckduckgo", "a+b") ==
         "https://duckduckgo.com/?q=a%2Bb");
  assert(otf::BuildSearchUrl("notarealengine", "hello").empty());
}

}  // namespace

int main() {
  IsolateAppDataHome();
  TestJsonEscaping();
  TestJsonObjectBuilder();
  TestStrictParsers();
  TestLengthPrefixedFields();
  TestSearchEngines();
  return 0;
}
