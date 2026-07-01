#include "otf/shim/otf_browser_context_manager.h"

#include <utility>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "otf/shim/otf_browser_context.h"

namespace otf {

namespace {

OtfBrowserContextManager* g_instance = nullptr;

// A workspace directory carrying this marker file is pending deletion; it is
// wiped on the next launch.
constexpr base::FilePath::CharType kDeletedMarker[] =
    FILE_PATH_LITERAL(".otf-deleted");

// Remove any workspace directory that was marked for deletion in a prior run.
// Runs at startup (blocking is allowed here).
void WipeMarkedWorkspaces(const base::FilePath& root) {
  const base::FilePath workspaces = root.Append(FILE_PATH_LITERAL("workspaces"));
  if (!base::DirectoryExists(workspaces)) {
    return;
  }
  base::FileEnumerator dirs(workspaces, /*recursive=*/false,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = dirs.Next(); !dir.empty(); dir = dirs.Next()) {
    if (base::PathExists(dir.Append(kDeletedMarker))) {
      base::DeletePathRecursively(dir);
    }
  }
}

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
  // Finish deletions confirmed in a previous session before anything opens.
  WipeMarkedWorkspaces(root_);
  system_ = std::make_unique<OtfBrowserContext>(
      root_.Append(FILE_PATH_LITERAL("system")), /*off_the_record=*/false);
}

OtfBrowserContextManager::~OtfBrowserContextManager() {
  workspaces_.clear();
  system_.reset();
  if (g_instance == this) {
    g_instance = nullptr;
  }
}

content::BrowserContext* OtfBrowserContextManager::System() {
  return system_.get();
}

content::BrowserContext* OtfBrowserContextManager::ForWorkspace(
    const std::string& id) {
  auto it = workspaces_.find(id);
  if (it != workspaces_.end()) {
    return it->second.get();
  }
  base::FilePath path =
      root_.Append(FILE_PATH_LITERAL("workspaces")).AppendASCII(id);
  auto context =
      std::make_unique<OtfBrowserContext>(std::move(path), /*off_the_record=*/false);
  content::BrowserContext* raw = context.get();
  workspaces_[id] = std::move(context);
  return raw;
}

void OtfBrowserContextManager::ReleaseWorkspace(const std::string& id) {
  workspaces_.erase(id);  // destroy the context (its tabs are already closed).
  // Mark the directory off the UI thread (file I/O blocks); it is wiped on the
  // next launch. Best-effort + crash-safe enough until the DB owns the list.
  base::FilePath dir =
      root_.Append(FILE_PATH_LITERAL("workspaces")).AppendASCII(id);
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(
          [](base::FilePath dir) {
            if (base::DirectoryExists(dir)) {
              base::WriteFile(dir.Append(kDeletedMarker), std::string_view());
            }
          },
          std::move(dir)));
}

}  // namespace otf
