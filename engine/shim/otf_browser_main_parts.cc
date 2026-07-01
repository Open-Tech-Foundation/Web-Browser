#include "otf/shim/otf_browser_main_parts.h"

#include <utility>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "build/build_config.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "net/base/filename_util.h"
#include "otf/shim/otf_browser_context.h"
#include "otf/shim/otf_devtools.h"
#include "otf/shim/otf_platform_window.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_LINUX)
#include "ui/base/ime/init/input_method_initializer.h"
#include "ui/linux/linux_ui.h"
#include "ui/linux/linux_ui_factory.h"
#endif

namespace otf {

namespace {

constexpr int kDefaultWindowWidth = 1280;
constexpr int kDefaultWindowHeight = 800;

// The page the UI WebContents loads on launch: `--dev-ui-url=<url>` (used by the
// e2e harness) takes precedence, then the first positional arg (the dev server
// URL in `bun run dev`), falling back to the internal new-tab UI.
GURL ResolveStartupURL() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch("dev-ui-url")) {
    GURL dev_url(command_line->GetSwitchValueASCII("dev-ui-url"));
    if (dev_url.is_valid() && dev_url.has_scheme()) {
      return dev_url;
    }
  }
  const base::CommandLine::StringVector& args = command_line->GetArgs();
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

int OtfBrowserMainParts::PreEarlyInitialization() {
#if BUILDFLAG(IS_LINUX)
  // TODO(input): revisit vs. Ozone's native input method when polishing IME.
  ui::InitializeInputMethodForTesting();
#endif
  return 0;  // content::RESULT_CODE_NORMAL_EXIT
}

void OtfBrowserMainParts::ToolkitInitialized() {
#if BUILDFLAG(IS_LINUX)
  ui::LinuxUi::SetInstance(ui::GetDefaultLinuxUi());
#endif
}

int OtfBrowserMainParts::PreMainMessageLoopRun() {
  browser_context_ =
      std::make_unique<OtfBrowserContext>(/*off_the_record=*/false);

  // Re-wire the DevTools/CDP http handler content_shell used to start (needed
  // for e2e tooling and DevTools to attach). No-op without --remote-debugging-port.
  MaybeStartDevToolsServer(browser_context_.get());

  // Bring up the windowing environment (screen/wm/ViewsDelegate) before any
  // WebContents — content registers display observers that require a Screen.
  OtfPlatformWindow::InitToolkit();

  // Our own UI surface (replacing content::Shell): the UI WebContents hosts the
  // React chrome; page tabs are layered on top by OtfTabHost. UI + tabs share
  // this one browser context (OtfBrowserContext::Get()).
  content::WebContents::CreateParams params(browser_context_.get());
  ui_contents_ = content::WebContents::Create(params);
  ui_contents_->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(ResolveStartupURL()));

  window_ = OtfPlatformWindow::Create(
      ui_contents_.get(),
      gfx::Size(kDefaultWindowWidth, kDefaultWindowHeight),
      base::BindOnce(&OtfBrowserMainParts::OnWindowClosed,
                     weak_factory_.GetWeakPtr()));
  return 0;
}

void OtfBrowserMainParts::WillRunMainMessageLoop(
    std::unique_ptr<base::RunLoop>& run_loop) {
  // otf owns the quit: closing OtfWindow ends the run loop.
  quit_closure_ = run_loop->QuitClosure();
}

void OtfBrowserMainParts::PostMainMessageLoopRun() {
  // Stop the DevTools server before tearing down targets/context.
  StopDevToolsServer();
  // Tear the UI down before the browser context (WebContents must outlive
  // neither the context nor the window incorrectly).
  window_.reset();
  ui_contents_.reset();
#if BUILDFLAG(IS_LINUX)
  ui::LinuxUi::SetInstance(nullptr);
#endif
  browser_context_.reset();
}

void OtfBrowserMainParts::OnWindowClosed() {
  if (quit_closure_) {
    std::move(quit_closure_).Run();
  }
}

}  // namespace otf
