#include "otf/shim/otf_browser_main_parts.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "build/build_config.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "otf/shim/otf_browser_context_manager.h"
#include "otf/shim/otf_devtools.h"
#include "otf/shim/otf_platform_window.h"
#include "otf/shim/otf_popup_overlay.h"
#include "otf/shim/otf_tab_host.h"
#include "otf/shim/otf_trust.h"
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
  // Record the trusted UI origin before the UI renderer spawns (below), so the
  // bridge gate agrees across processes. Safe here: URL schemes are registered by
  // now, but no frame-hosting child process has launched yet. No-op for the
  // internal browser:// UI (trusted by scheme).
  InitTrustedUiOrigin();

  // Owns otf's BrowserContexts + the per-workspace on-disk layout. The UI shell
  // and overlays use the persistent `system` context (page tabs move to their
  // workspace's context in Phase 2 of data isolation).
  context_manager_ = std::make_unique<OtfBrowserContextManager>();
  content::BrowserContext* system_context = context_manager_->System();

  // Re-wire the DevTools/CDP http handler content_shell used to start (needed
  // for e2e tooling and DevTools to attach). No-op without --remote-debugging-port.
  MaybeStartDevToolsServer(system_context);

  // Bring up the windowing environment (screen/wm/ViewsDelegate) before any
  // WebContents — content registers display observers that require a Screen.
  OtfPlatformWindow::InitToolkit();

  // Our own UI surface (replacing content::Shell): the UI WebContents hosts the
  // React chrome; page tabs are layered on top by OtfTabHost.
  content::WebContents::CreateParams params(system_context);
  ui_contents_ = content::WebContents::Create(params);
  ui_contents_->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(ResolveUiUrl()));

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
  // Tab + overlay WebContents live in singletons; destroy them before their
  // browser contexts (a WebContents must not outlive its context).
  OtfTabHost::Get().Shutdown();
  OtfPopupOverlay::Get().Shutdown();
#if BUILDFLAG(IS_LINUX)
  ui::LinuxUi::SetInstance(nullptr);
#endif
  context_manager_.reset();
}

void OtfBrowserMainParts::OnWindowClosed() {
  if (quit_closure_) {
    std::move(quit_closure_).Run();
  }
}

}  // namespace otf
