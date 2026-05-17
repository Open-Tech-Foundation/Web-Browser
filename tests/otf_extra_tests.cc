#include "otf_utils.h"
#include "otf_page_policy.h"
// Release builds set NDEBUG, which makes assert() a no-op. Force it off here
// so the assertions in this suite actually execute.
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <vector>

namespace {

void TestSanitizeFilename() {
    assert(otf::SanitizeFilename("hello.txt") == "hello.txt");
    assert(otf::SanitizeFilename("hello/world.txt") == "hello_world.txt");
    assert(otf::SanitizeFilename("foo:bar<baz>|qux") == "foo_bar_baz__qux");
    // Trailing dots and spaces are stripped (matches Windows POSIX rules).
    assert(otf::SanitizeFilename("file.   ") == "file");
    assert(otf::SanitizeFilename("file...") == "file");
    // Empty / all-stripped input falls back to "download".
    assert(otf::SanitizeFilename("") == "download");
    assert(otf::SanitizeFilename("   ") == "download");
    // NOTE: Windows reserved names (CON/PRN/AUX/NUL/COM*/LPT*) are NOT
    // currently rewritten — fine for the Linux-first release. Revisit when
    // adding a Windows target.
    std::cout << "TestSanitizeFilename passed" << std::endl;
}

void TestSearchUrl() {
    assert(otf::BuildSearchUrl("google", "test query") == "https://www.google.com/search?q=test+query");
    assert(otf::BuildSearchUrl("duckduckgo", "otf browser") == "https://duckduckgo.com/?q=otf+browser");
    assert(otf::BuildSearchUrl("bing", "hello") == "https://www.bing.com/search?q=hello");
    std::cout << "TestSearchUrl passed" << std::endl;
}

void TestPagePolicyAllowlist() {
    assert(otf::ShouldInjectPagePolicy("https://google.com"));
    assert(otf::ShouldInjectPagePolicy("http://example.com"));
    assert(!otf::ShouldInjectPagePolicy("browser://settings"));
    assert(!otf::ShouldInjectPagePolicy("file:///etc/passwd"));
    std::cout << "TestPagePolicyAllowlist passed" << std::endl;
}

void TestJsonObjectBuilder() {
    otf::JsonObjectBuilder builder;
    builder.AddString("key", "value")
           .AddInt("num", 42)
           .AddBool("flag", true)
           .AddNull("empty");
    std::string json = builder.Build();
    assert(json.find("\"key\":\"value\"") != std::string::npos);
    assert(json.find("\"num\":42") != std::string::npos);
    assert(json.find("\"flag\":true") != std::string::npos);
    assert(json.find("\"empty\":null") != std::string::npos);
    std::cout << "TestJsonObjectBuilder passed" << std::endl;
}

void TestHtmlAttrEscape() {
    // All five HTML metacharacters get escaped.
    assert(otf::HtmlAttrEscape("plain") == "plain");
    assert(otf::HtmlAttrEscape("a&b") == "a&amp;b");
    assert(otf::HtmlAttrEscape("<script>") == "&lt;script&gt;");
    assert(otf::HtmlAttrEscape("\"quote\"") == "&quot;quote&quot;");
    assert(otf::HtmlAttrEscape("'apos'") == "&#39;apos&#39;");
    // Regression: dev-mode redirect input that previously injected markup.
    const std::string evil = "browser://newtab?'><script>alert(1)</script>";
    const std::string out = otf::HtmlAttrEscape(evil);
    assert(out.find("<script>") == std::string::npos);
    assert(out.find("'>") == std::string::npos);
    std::cout << "TestHtmlAttrEscape passed" << std::endl;
}

void TestParseIntStrict() {
    assert(otf::ParseIntStrict("0").value() == 0);
    assert(otf::ParseIntStrict("42").value() == 42);
    assert(otf::ParseIntStrict("-7").value() == -7);
    // Rejections — these are exactly the inputs that used to crash via
    // std::stoi.
    assert(!otf::ParseIntStrict("").has_value());
    assert(!otf::ParseIntStrict("abc").has_value());
    assert(!otf::ParseIntStrict("12abc").has_value());
    assert(!otf::ParseIntStrict(" 12").has_value());
    assert(!otf::ParseIntStrict("12 ").has_value());
    assert(!otf::ParseIntStrict("1.0").has_value());
    // Out of range for int32: 99999999999 overflows.
    assert(!otf::ParseIntStrict("99999999999").has_value());
    std::cout << "TestParseIntStrict passed" << std::endl;
}

void TestParseUint32Strict() {
    assert(otf::ParseUint32Strict("0").value() == 0u);
    assert(otf::ParseUint32Strict("4294967295").value() == 4294967295u);
    // Negative rejected for unsigned.
    assert(!otf::ParseUint32Strict("-1").has_value());
    // Overflow rejected.
    assert(!otf::ParseUint32Strict("4294967296").has_value());
    assert(!otf::ParseUint32Strict("").has_value());
    assert(!otf::ParseUint32Strict("12abc").has_value());
    std::cout << "TestParseUint32Strict passed" << std::endl;
}

void TestIsAllowedSearchEngineId() {
    for (const char* id : {"google", "bing", "yahoo", "duckduckgo", "baidu",
                            "yandex", "ecosia", "naver", "startpage"}) {
        assert(otf::IsAllowedSearchEngineId(id));
    }
    assert(!otf::IsAllowedSearchEngineId("notarealengine"));
    assert(!otf::IsAllowedSearchEngineId("Google"));  // case-sensitive
    std::cout << "TestIsAllowedSearchEngineId passed" << std::endl;
}

} // namespace

int main() {
    TestSanitizeFilename();
    TestSearchUrl();
    TestPagePolicyAllowlist();
    TestJsonObjectBuilder();
    TestHtmlAttrEscape();
    TestParseIntStrict();
    TestParseUint32Strict();
    TestIsAllowedSearchEngineId();
    std::cout << "All extra native tests passed!" << std::endl;
    return 0;
}
