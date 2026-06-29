#include "otf/shim/otf_browser_main_parts.h"

#include <utility>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "net/base/filename_util.h"
#include "otf/shim/otf_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace otf {

namespace {

constexpr int kDefaultWindowWidth = 1280;
constexpr int kDefaultWindowHeight = 800;

// The page the UI WebContents loads on launch: the first positional arg (the
// dev server URL in `bun run dev`), falling back to the internal new-tab UI.
GURL ResolveStartupURL() {
  const base::CommandLine::StringVector& args =
      base::CommandLine::ForCurrentProcess()->GetArgs();
  if (args.empty()) {
    return GURL("browser://newtab");
  }
  GURL url(args[0]);
  if (url.is_valid() && url.has_scheme()) {
    return url;
  }
  return net::FilePathToFileURL(
      base::MakeAbsoluteFilePath(base::FilePath(args[0])));
}

}  // namespace

OtfBrowserMainParts::OtfBrowserMainParts() = default;
OtfBrowserMainParts::~OtfBrowserMainParts() = default;

void OtfBrowserMainParts::InitializeMessageLoopContext() {
  // Our own UI surface, replacing Shell::CreateNewWindow. The UI WebContents
  // hosts the React chrome; page tabs are layered on top by OtfTabHost.
  content::WebContents::CreateParams params(browser_context());
  ui_contents_ = content::WebContents::Create(params);

  ui_contents_->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(ResolveStartupURL()));

  window_ = std::make_unique<OtfWindow>(
      ui_contents_.get(),
      gfx::Size(kDefaultWindowWidth, kDefaultWindowHeight),
      base::BindOnce(&OtfBrowserMainParts::OnWindowClosed,
                     weak_factory_.GetWeakPtr()));
}

void OtfBrowserMainParts::WillRunMainMessageLoop(
    std::unique_ptr<base::RunLoop>& run_loop) {
  // otf owns the quit: closing OtfWindow ends the run loop. (Deliberately does
  // not chain to the base, which would hand the closure to content::Shell.)
  quit_closure_ = run_loop->QuitClosure();
}

void OtfBrowserMainParts::PostMainMessageLoopRun() {
  // Tear the UI down before the base resets the browser contexts: the WebView in
  // the widget and the WebContents must outlive neither the context nor each
  // other incorrectly.
  window_.reset();
  ui_contents_.reset();
  ShellBrowserMainParts::PostMainMessageLoopRun();
}

void OtfBrowserMainParts::OnWindowClosed() {
  if (quit_closure_) {
    std::move(quit_closure_).Run();
  }
}

}  // namespace otf
