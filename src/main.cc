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

#if defined(_WIN32)
  // The Windows sandbox is NOT linked: this CEF distribution ships only the
  // Linux chrome-sandbox helper, not cef_sandbox.lib, and the build never links
  // a Windows sandbox library. With the sandbox enabled (the default) CEF
  // expects a real sandbox_info pointer; we pass nullptr, so CefInitialize
  // returns false and the app never starts (the cause of the blank window on
  // Windows). Disabling the sandbox here is mandatory until cef_sandbox.lib is
  // linked and CefScopedSandboxInfo is wired into CefExecuteProcess/CefInitialize.
  // Linux is unaffected — it keeps its SUID chrome-sandbox helper.
  settings.no_sandbox = true;
#endif

  // Determine cache path: use user-configured cacheDir if set, otherwise default.
  std::filesystem::path app_cache;
  const auto configured_cache = otf::GetConfiguredCacheDir();
  if (!configured_cache.empty()) {
    app_cache = configured_cache;
  } else {
    app_cache = otf::GetAppCacheDir();
  }
  const std::filesystem::path app_data = otf::GetAppDataDir();
  const std::string exe_dir = otf::GetExecutableDir();

  // Direct CEF logging to debug.log NEXT TO THE BINARY. This is REQUIRED to see
  // anything past CefInitialize: once CefInitialize applies the CefSettings
  // logging config, an unset log_file makes Chromium log to stderr — which is a
  // void for a GUI-subsystem (wWinMain, no console) binary, so every line after
  // CefInitialize is silently discarded. Pointing log_file at the exe dir (which
  // is guaranteed to exist and be writable) is the only way the startup-flow
  // logs reach disk. CEF passes --log-file= to every subprocess so they append
  // to this same file instead of clobbering a default debug.log.
  const std::filesystem::path log_file =
      std::filesystem::path(exe_dir) / "debug.log";
#if defined(_WIN32)
  CefString(&settings.log_file) = log_file.wstring();
#else
  CefString(&settings.log_file).FromString(log_file.string());
#endif
  settings.log_severity = LOGSEVERITY_INFO;

  LOG(INFO) << "[otf] app data dir : " << app_data.string();
  LOG(INFO) << "[otf] app cache dir: " << app_cache.string();
  otf::DiagLog("app data dir : " + app_data.string());
  otf::DiagLog("app cache dir: " + app_cache.string());
#if defined(_WIN32)
  otf::DiagLog("sandbox: DISABLED (no_sandbox=true; cef_sandbox.lib not linked)");
#endif

  // Diagnostics for production UI serving: the browser:// scheme handler serves
  // the UI from <exe dir>/ui. If that resolution is wrong (or the folder didn't
  // ship), the app paints a blank window. Log it so the log shows the truth.
  {
    std::error_code ec;
    const std::filesystem::path ui_index =
        std::filesystem::path(exe_dir) / "ui" / "index.html";
    LOG(INFO) << "[otf] executable dir: [" << exe_dir << "]";
    LOG(INFO) << "[otf] ui index.html: " << ui_index.string()
              << " exists=" << (std::filesystem::exists(ui_index, ec) ? "YES" : "NO");
    otf::DiagLog("exe dir: [" + exe_dir + "]");
    otf::DiagLog("ui index.html exists=" +
                 std::string(std::filesystem::exists(ui_index, ec) ? "YES" : "NO"));
  }

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

  LOG(INFO) << "[otf] startup 1/4: cef cache path = " << cef_cache.string();
  LOG(INFO) << "[otf] startup 2/4: calling CefInitialize...";
  otf::DiagLog("startup 1/4: cef cache path = " + cef_cache.string());
  otf::DiagLog("startup 2/4: calling CefInitialize...");
  const bool initialized =
      CefInitialize(main_args, settings, app.get(), nullptr);
  if (!initialized) {
    LOG(ERROR) << "[otf] startup 2/4 FAILED: CefInitialize returned false";
    otf::DiagLog("startup 2/4 FAILED: CefInitialize returned false — ABORTING");
    return 1;
  }
  LOG(INFO) << "[otf] startup 3/4: CefInitialize OK, entering message loop "
               "(OnContextInitialized fires next)";
  otf::DiagLog("startup 3/4: CefInitialize OK, entering message loop");
  // Re-log the UI path facts AFTER CefInitialize: opening log_file may have
  // truncated the pre-init lines above, so repeat the ones that matter for the
  // blank-window diagnosis here where they are guaranteed to persist.
  {
    std::error_code ec;
    const std::filesystem::path ui_index =
        std::filesystem::path(exe_dir) / "ui" / "index.html";
    LOG(INFO) << "[otf] startup 4/4: exe dir=[" << exe_dir << "] ui index.html "
              << ui_index.string() << " exists="
              << (std::filesystem::exists(ui_index, ec) ? "YES" : "NO");
  }
  otf::DiagLog("startup: CefRunMessageLoop entered (app is now live)");
  CefRunMessageLoop();
  LOG(INFO) << "[otf] startup: message loop exited, calling CefShutdown";
  otf::DiagLog("startup: MESSAGE LOOP EXITED — app is quitting (CefShutdown)");
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
