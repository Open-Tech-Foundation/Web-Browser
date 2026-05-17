#include "otf_utils.h"
#include "otf_page_policy.h"
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

} // namespace

int main() {
    TestSanitizeFilename();
    TestSearchUrl();
    TestPagePolicyAllowlist();
    TestJsonObjectBuilder();
    std::cout << "All extra native tests passed!" << std::endl;
    return 0;
}
