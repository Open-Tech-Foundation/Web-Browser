#include "include/cef_app.h"
#include "otf_app.h"

namespace {

const char* GetOtfUserAgent() {
#if defined(OS_WIN)
  return "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) OTFBrowser/1.0.0";
#elif defined(OS_MAC)
  return "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) OTFBrowser/1.0.0";
#else
  return "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) OTFBrowser/1.0.0";
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
  CefString(&settings.user_agent).FromASCII(GetOtfUserAgent());

  CefInitialize(main_args, settings, app.get(), nullptr);

  // Run the CEF message loop. This will block until the window is closed.
  CefRunMessageLoop();

  // Shut down CEF.
  CefShutdown();

  return 0;
}
