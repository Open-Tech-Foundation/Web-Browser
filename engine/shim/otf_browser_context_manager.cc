#include "otf/shim/otf_browser_context_manager.h"

#include <utility>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "otf/shim/otf_browser_context.h"

namespace otf {

namespace {

OtfBrowserContextManager* g_instance = nullptr;

// The user-data root:
//   1. --user-data-dir=<path>  (automated / e2e per-session temp dirs)
//   2. dev mode (OTF_DEV_MODE)  -> $HOME/.otf-browser-dev
//   3. production               -> $HOME/.otf-browser
base::FilePath ResolveUserDataRoot() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch("user-data-dir")) {
    return command_line->GetSwitchValuePath("user-data-dir");
  }
  base::FilePath home;
  CHECK(base::PathService::Get(base::DIR_HOME, &home));
  const bool dev = base::Environment::Create()->HasVar("OTF_DEV_MODE");
  return home.Append(dev ? FILE_PATH_LITERAL(".otf-browser-dev")
                         : FILE_PATH_LITERAL(".otf-browser"));
}

}  // namespace

// static
OtfBrowserContextManager* OtfBrowserContextManager::Get() {
  return g_instance;
}

OtfBrowserContextManager::OtfBrowserContextManager()
    : root_(ResolveUserDataRoot()) {
  g_instance = this;
  system_ = std::make_unique<OtfBrowserContext>(
      root_.Append(FILE_PATH_LITERAL("system")), /*off_the_record=*/false);
}

OtfBrowserContextManager::~OtfBrowserContextManager() {
  system_.reset();
  if (g_instance == this) {
    g_instance = nullptr;
  }
}

content::BrowserContext* OtfBrowserContextManager::System() {
  return system_.get();
}

}  // namespace otf
