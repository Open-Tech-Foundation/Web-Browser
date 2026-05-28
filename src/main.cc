#include "include/cef_app.h"
#include "include/cef_version.h"
#include "otf_app.h"
#include "otf_utils.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef OTF_VERSION
#define OTF_VERSION "0.0.0-unknown"
#endif

#define OTF_STRINGIFY(x) #x
#define OTF_TOSTRING(x) OTF_STRINGIFY(x)
#define CHROMIUM_VERSION_STRING        \
  OTF_TOSTRING(CHROME_VERSION_MAJOR)  \
  "." OTF_TOSTRING(CHROME_VERSION_MINOR) \
  "." OTF_TOSTRING(CHROME_VERSION_BUILD) \
  "." OTF_TOSTRING(CHROME_VERSION_PATCH)

namespace {

std::string OtfVersionBase() {
  std::string v(OTF_VERSION);
  auto pos = v.find('-');
  return pos != std::string::npos ? v.substr(0, pos) : v;
}

std::string GetOtfUserAgent() {
#if defined(_WIN32)
  return std::string("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) OTFBrowser/") +
         OtfVersionBase() + " Chromium/" CHROMIUM_VERSION_STRING " Safari/537.36";
#elif defined(__APPLE__)
  return std::string("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) OTFBrowser/") +
         OtfVersionBase() + " Chromium/" CHROMIUM_VERSION_STRING " Safari/537.36";
#else
  return std::string("Mozilla/5.0 (X11; Linux x86_64) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) OTFBrowser/") +
         OtfVersionBase() + " Chromium/" CHROMIUM_VERSION_STRING " Safari/537.36";
#endif
}

static int RunApp(int argc, char* argv[]) {
#if defined(_WIN32)
  // GUI subsystem — no console, so --version is not useful here.
  // CefMainArgs on Windows takes HINSTANCE; obtain it from the module handle.
  CefMainArgs main_args(GetModuleHandle(nullptr));
#else
  // Handle --version / -v before initializing CEF so users can identify a
  // packaged binary without launching a window. CEF sub-processes (renderer,
  // gpu, etc.) are spawned with a --type=… flag and never pass --version,
  // so this check is safe to do here.
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") == 0 ||
        std::strcmp(argv[i], "-v") == 0) {
      std::printf("OTF Browser %s\n", OTF_VERSION);
      return 0;
    }
  }
  CefMainArgs main_args(argc, argv);
#endif

  CefRefPtr<otf::OtfApp> app(new otf::OtfApp());

  int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  // Apply any pending storage path changes before CEF initializes.
  otf::ApplyPendingPathsOnStartup();

  CefSettings settings;
  CefString(&settings.user_agent).FromASCII(GetOtfUserAgent().c_str());

  // Determine cache path: use user-configured cacheDir if set, otherwise default.
  std::filesystem::path app_cache;
  const auto configured_cache = otf::GetConfiguredCacheDir();
  if (!configured_cache.empty()) {
    app_cache = configured_cache;
  } else {
    app_cache = otf::GetAppCacheDir();
  }
  const std::filesystem::path app_data = otf::GetAppDataDir();

  LOG(INFO) << "[otf] app data dir : " << app_data.string();
  LOG(INFO) << "[otf] app cache dir: " << app_cache.string();

  // Point CEF at the platform-correct cache directory so cookies, HTTP cache,
  // and localStorage survive across restarts in a predictable location.
  // workspace-specific request contexts each get a sub-directory under this.
  const std::filesystem::path cef_cache = app_cache / "cef";
  if (!cef_cache.empty()) {
#if defined(_WIN32)
    CefString(&settings.root_cache_path) = cef_cache.wstring();
#else
    CefString(&settings.root_cache_path).FromString(cef_cache.string());
#endif
  }

  // Freeze runtime storage paths so mid-session changes don't take effect.
  otf::LockStoragePaths();

  CefInitialize(main_args, settings, app.get(), nullptr);
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}

}  // namespace

#if defined(_WIN32)
#include <windows.h>
#include "include/cef_sandbox_win.h"

int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPWSTR lpCmdLine,
                      int nCmdShow) {
  return RunApp(__argc, __argv);
}
#else
int main(int argc, char* argv[]) {
  return RunApp(argc, argv);
}
#endif
