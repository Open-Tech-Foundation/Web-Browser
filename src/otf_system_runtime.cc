#include "otf_system_runtime.h"

#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_values.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_utils.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace otf {
namespace {

#if !defined(_WIN32)
bool SpawnDetached(const char* program,
                   const std::vector<std::string>& args) {
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    const pid_t grandchild = fork();
    if (grandchild == 0) {
      setsid();
      const int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) {
          close(devnull);
        }
      }
      std::vector<char*> argv;
      argv.reserve(args.size() + 2);
      argv.push_back(const_cast<char*>(program));
      for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);
      execvp(program, argv.data());
      _exit(127);
    }
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return true;
}
#endif

bool RestartBrowserProcess() {
  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();
  if (!command_line) {
    return false;
  }

  const std::string executable_path = otf::GetExecutablePath();
  if (executable_path.empty()) {
    return false;
  }

  CefCommandLine::ArgumentList arguments;
  command_line->GetArguments(arguments);

#if defined(_WIN32)
  auto quote_windows = [](const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
      if (c == '"') {
        out += "\\\"";
      } else {
        out += c;
      }
    }
    out += "\"";
    return out;
  };

  std::string command_line_str = quote_windows(executable_path);
  for (const auto& arg : arguments) {
    command_line_str += " ";
    command_line_str += quote_windows(arg.ToString());
  }

  STARTUPINFOA startup_info;
  ZeroMemory(&startup_info, sizeof(startup_info));
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info;
  ZeroMemory(&process_info, sizeof(process_info));
  std::vector<char> mutable_command_line(command_line_str.begin(),
                                         command_line_str.end());
  mutable_command_line.push_back('\0');
  const BOOL started = CreateProcessA(
      nullptr, mutable_command_line.data(), nullptr, nullptr, FALSE,
      CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup_info, &process_info);
  if (!started) {
    return false;
  }
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return true;
#else
  std::vector<std::string> args;
  args.reserve(arguments.size());
  for (const auto& arg : arguments) {
    args.push_back(arg.ToString());
  }
  return SpawnDetached(executable_path.c_str(), args);
#endif
}

}  // namespace

bool OtfHandler::RestartBrowser() {
  return RestartBrowserProcess();
}

bool OtfHandler::StartSnipCapture(bool hide_app_menu, std::string* error) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !tab_manager_ || !devtools_bridge_) {
    if (error) *error = "not ready";
    return false;
  }
  const int current_tab_id = app->GetCurrentTabId();
  CefRefPtr<CefBrowser> target = tab_manager_->GetBrowser(current_tab_id);
  if (!target) {
    if (error) *error = "no active tab";
    return false;
  }

  devtools_bridge_->Attach(target);
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("format", "png");
  devtools_bridge_->Execute(
      "Page.captureScreenshot", params,
      [hide_app_menu](bool ok, const std::string& result_json) {
        if (!ok) return;
        OtfApp* app = OtfApp::GetInstance();
        OtfHandler* handler = OtfHandler::GetInstance();
        if (!app || !handler || !handler->snip_preview_browser_) return;
        if (hide_app_menu) app->HideAppMenuOverlay();
        app->ShowSnipPreviewOverlay();
        const std::string js = "window.__otfSetSnipImage(" + result_json + ");";
        handler->snip_preview_browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
      });
  return true;
}

}  // namespace otf
