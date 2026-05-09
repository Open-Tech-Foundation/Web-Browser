#include "include/cef_app.h"
#include "otf_app.h"

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

  CefInitialize(main_args, settings, app.get(), nullptr);

  // Run the CEF message loop. This will block until the window is closed.
  CefRunMessageLoop();

  // Shut down CEF.
  CefShutdown();

  return 0;
}
