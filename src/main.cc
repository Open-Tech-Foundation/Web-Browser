#include "include/cef_app.h"
#include "otf_app.h"

#include <cstdio>
#include <cstring>
#include <string>

// Defined by CMake via -DOTF_VERSION="…". Provide a fallback so the source
// still compiles if someone builds bypassing CMake.
#ifndef OTF_VERSION
#define OTF_VERSION "0.0.0-unknown"
#endif

namespace {

std::string GetOtfUserAgent() {
#if defined(OS_WIN)
  return std::string("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) OTFBrowser/") +
         OTF_VERSION;
#elif defined(OS_MAC)
  return std::string("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) OTFBrowser/") +
         OTF_VERSION;
#else
  return std::string("Mozilla/5.0 (X11; Linux x86_64) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) OTFBrowser/") +
         OTF_VERSION;
#endif
}

}  // namespace

#if defined(OS_WIN)
#include <windows.h>
#include "include/cef_sandbox_win.h"
#endif

// Unified entry point for OTF Browser
#if defined(OS_WIN)
int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR lpCmdLine,
                     int nCmdShow) {
  CefMainArgs main_args(hInstance);
#else
int main(int argc, char* argv[]) {
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

  // Execute the secondary process (renderer, plugin, etc) if needed.
  int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  // Initialize CEF.
  CefSettings settings;
  
  // Use a professional cache path
  // CefString(&settings.cache_path).FromASCII("./cache");
  CefString(&settings.user_agent).FromASCII(GetOtfUserAgent().c_str());

  CefInitialize(main_args, settings, app.get(), nullptr);

  // Run the CEF message loop. This will block until the window is closed.
  CefRunMessageLoop();

  // Shut down CEF.
  CefShutdown();

  return 0;
}
