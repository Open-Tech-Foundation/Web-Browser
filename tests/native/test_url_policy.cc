#include "otf_utils.h"

#undef NDEBUG
#include <cassert>
#include <cmath>
#include <string>

namespace {

void TestBrowserPageAllowlist() {
  assert(otf::IsAllowedBrowserPageUrl("browser://newtab"));
  assert(otf::IsAllowedBrowserPageUrl("browser://settings/"));
  assert(otf::IsAllowedBrowserPageUrl("browser://downloads?view=all"));
  assert(otf::IsAllowedBrowserPageUrl("browser://insecure-blocked#top"));
  assert(!otf::IsAllowedBrowserPageUrl("browser://not-real"));
  assert(!otf::IsAllowedBrowserPageUrl("https://example.com"));
}

void TestBrowserPageResolution() {
  assert(otf::GetBrowserPageDevUrl("http://localhost:3000",
                                   "browser://settings") ==
         "http://localhost:3000/settings.html");
  assert(otf::GetBrowserPageDevUrl(
             "http://localhost:3000",
             "browser://insecure-blocked?url=http%3A%2F%2Fexample.com") ==
         "http://localhost:3000/insecure-blocked.html?url=http%3A%2F%2Fexample.com");
  assert(otf::GetBrowserPageDevUrl("", "browser://settings").empty());
  assert(otf::GetBrowserPageFilePath("browser://history").find(
             "/ui/history.html") != std::string::npos);
}

void TestHttpAndStartupGates() {
  assert(otf::IsAllowedHttpUrl("https://example.com"));
  assert(otf::IsAllowedHttpUrl("http://localhost:3000"));
  assert(otf::IsAllowedHttpUrl("http://127.0.0.1:8080"));
  assert(otf::IsAllowedHttpUrl("http://[::1]:3000"));
  assert(!otf::IsAllowedHttpUrl("http://example.com"));
  assert(!otf::IsAllowedHttpUrl("file:///tmp/a"));

  assert(otf::IsAllowedStartupUrl("browser://newtab"));
  assert(otf::IsAllowedStartupUrl("https://example.com"));
  assert(otf::IsAllowedStartupUrl("http://localhost:3000"));
  assert(!otf::IsAllowedStartupUrl("javascript:alert(1)"));
  assert(!otf::IsAllowedStartupUrl("file:///etc/passwd"));
  assert(!otf::IsAllowedStartupUrl("data:text/html,x"));
  assert(!otf::IsAllowedStartupUrl("http://"));
}

void TestInternalUiTrustBoundaries() {
  assert(otf::IsInternalBrowserUiUrl("browser://newtab"));
  assert(otf::IsInternalBrowserUiUrl("browser://settings?x=1"));
  assert(otf::IsInternalBrowserUiUrl("browser://appmenu"));
  assert(otf::IsInternalBrowserUiUrl("browser://docpreview"));
  assert(!otf::IsInternalBrowserUiUrl("browser://popup-intent"));
  assert(!otf::IsInternalBrowserUiUrl("browser://newtab/assets/app.js"));
  assert(!otf::IsInternalBrowserUiUrl("browser://doc-preview/content/token"));
  assert(!otf::IsInternalBrowserUiUrl("http://localhost:3000/index.html"));
  assert(!otf::IsInternalBrowserUiUrl("file:///opt/otf/ui/index.html"));
  assert(!otf::IsInternalBrowserUiUrl("https://example.com/settings.html"));

  assert(otf::IsInternalUiUrl("browser://newtab"));
  assert(otf::IsInternalUiUrl("http://localhost:3000/appmenu.html"));
  assert(otf::IsInternalUiUrl("http://localhost:3000/blockedpopup.html"));
  assert(otf::IsInternalUiUrl("http://localhost:3000/downloadrequest.html"));
  assert(otf::IsInternalUiUrl("http://localhost:3000/insecure-blocked.html?x=1"));
  assert(!otf::IsInternalUiUrl("file:///opt/otf/ui/index.html"));
  assert(!otf::IsInternalUiUrl("https://example.com/products"));
}

void TestPersistableAndFilesystemLikeUrls() {
  assert(otf::IsPersistableWebUrl("http://example.com"));
  assert(otf::IsPersistableWebUrl("https://example.com/path?q=1"));
  assert(!otf::IsPersistableWebUrl("browser://newtab"));
  assert(!otf::IsPersistableWebUrl("javascript:alert(1)"));

  assert(otf::IsLocalFilesystemPathLike("/tmp/file.txt"));
  assert(otf::IsLocalFilesystemPathLike("~/file.txt"));
  assert(otf::IsLocalFilesystemPathLike("relative/path.txt"));
  assert(!otf::IsLocalFilesystemPathLike("https://example.com/a"));
  assert(!otf::IsLocalFilesystemPathLike("browser://settings"));
}

void TestBookmarkAndDownloadHelpers() {
  assert(otf::NormalizeBookmarkUrl("https://example.com") ==
         otf::NormalizeBookmarkUrl("https://example.com/"));
  assert(otf::NormalizeBookmarkUrl("https://example.com/path/") ==
         "https://example.com/path");
  assert(otf::NormalizeBookmarkUrl("https://example.com/path/?q=1") ==
         "https://example.com/path?q=1");
  assert(otf::NormalizeBookmarkUrl(
             "https://example.com/path/?utm_campaign=launch&q=1&_ga=abc") ==
         "https://example.com/path?q=1");

  assert(otf::SanitizeFilename("hello/world.txt") == "hello_world.txt");
  assert(otf::SanitizeFilename("file.   ") == "file");
  assert(otf::SanitizeFilename("   ") == "download");
}

void TestZoomAndImagePredicates() {
  assert(std::lround(otf::ZoomLevelToPercent(otf::ZoomReset())) == 100);
  assert(std::lround(otf::ZoomLevelToPercent(otf::ZoomIn(otf::ZoomReset()))) == 110);
  assert(std::lround(otf::ZoomLevelToPercent(otf::ZoomOut(otf::ZoomReset()))) == 90);
  assert(otf::IsTiffUrl("https://example.com/a.TIFF?download=1"));
  assert(otf::IsSupportedImageUrl("https://example.com/a.webp#x"));
  assert(!otf::IsSupportedImageUrl("https://example.com/a.txt"));
}

void TestStripTrackingParamsFromUrl() {
  // No query — unchanged
  assert(otf::StripTrackingParamsFromUrl("https://example.com/path") ==
         "https://example.com/path");

  // No tracking params — unchanged
  assert(otf::StripTrackingParamsFromUrl("https://example.com/path?q=1&page=2") ==
         "https://example.com/path?q=1&page=2");

  // UTM params stripped
  assert(otf::StripTrackingParamsFromUrl(
             "https://example.com/path?utm_source=newsletter&utm_medium=email&q=1") ==
         "https://example.com/path?q=1");

  // All tracking params stripped, query removed entirely
  assert(otf::StripTrackingParamsFromUrl(
             "https://example.com/path?utm_campaign=launch&_ga=abc") ==
         "https://example.com/path");

  // Multiple tracking param types
  assert(otf::StripTrackingParamsFromUrl(
             "https://example.com/path?keep=1&fbclid=abc&gclid=xyz&page=3") ==
         "https://example.com/path?keep=1&page=3");

  // Fragment preserved
  assert(otf::StripTrackingParamsFromUrl(
             "https://example.com/path?utm_source=x#section") ==
         "https://example.com/path#section");

  // Non-http scheme — unchanged
  assert(otf::StripTrackingParamsFromUrl("ftp://example.com/file?utm_source=x") ==
         "ftp://example.com/file?utm_source=x");

  // Empty string — unchanged
  assert(otf::StripTrackingParamsFromUrl("") == "");

  // Single tracking param, no value
  assert(otf::StripTrackingParamsFromUrl("https://example.com?fbclid=") ==
         "https://example.com/");

  // Tracking param without value
  assert(otf::StripTrackingParamsFromUrl("https://example.com?fbclid&utm_source") ==
         "https://example.com/");

  // Port preserved
  assert(otf::StripTrackingParamsFromUrl(
             "https://example.com:8080/path?utm_source=x&q=1") ==
         "https://example.com:8080/path?q=1");

  // Path-only URL with query
  assert(otf::StripTrackingParamsFromUrl("https://example.com/?gclid=abc") ==
         "https://example.com/");

  // Multiple equal signs in value
  assert(otf::StripTrackingParamsFromUrl(
             "https://example.com?utm_source=a=b&keep=1") ==
         "https://example.com/?keep=1");

  // All param types from kTrackingParams
  assert(otf::StripTrackingParamsFromUrl(
             "https://example.com?"
             "utm_source=x&utm_medium=y&utm_campaign=z&utm_term=w&utm_content=v"
             "&fbclid=a&gclid=b&gbraid=c&wbraid=d&msclkid=e&twclid=f&igshid=g"
             "&mc_cid=h&mc_eid=i&_ga=j&_gl=k&yclid=l&dclid=m"
             "&keep=1") ==
         "https://example.com/?keep=1");

  // http scheme works too
  assert(otf::StripTrackingParamsFromUrl(
             "http://example.com/path?utm_source=x&q=1") ==
         "http://example.com/path?q=1");

  // Invalid URL — unchanged
  assert(otf::StripTrackingParamsFromUrl("not-a-url") == "not-a-url");

  fprintf(stderr, "TestStripTrackingParamsFromUrl PASSED\n");
}

}  // namespace

int main() {
  TestBrowserPageAllowlist();
  TestBrowserPageResolution();
  TestHttpAndStartupGates();
  TestInternalUiTrustBoundaries();
  TestPersistableAndFilesystemLikeUrls();
  TestBookmarkAndDownloadHelpers();
  TestStripTrackingParamsFromUrl();
  TestZoomAndImagePredicates();
  return 0;
}
