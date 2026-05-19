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
  assert(!otf::IsInternalBrowserUiUrl("http://localhost:3000/index.html"));
  assert(!otf::IsInternalBrowserUiUrl("file:///opt/otf/ui/index.html"));
  assert(!otf::IsInternalBrowserUiUrl("https://example.com/settings.html"));

  assert(otf::IsInternalUiUrl("browser://newtab"));
  assert(otf::IsInternalUiUrl("http://localhost:3000/appmenu.html"));
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

}  // namespace

int main() {
  TestBrowserPageAllowlist();
  TestBrowserPageResolution();
  TestHttpAndStartupGates();
  TestInternalUiTrustBoundaries();
  TestPersistableAndFilesystemLikeUrls();
  TestBookmarkAndDownloadHelpers();
  TestZoomAndImagePredicates();
  return 0;
}
