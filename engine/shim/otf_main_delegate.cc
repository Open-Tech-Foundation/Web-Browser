#include "otf/shim/otf_main_delegate.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "build/build_config.h"
#include "content/shell/common/shell_switches.h"
#include "otf/shim/otf_content_browser_client.h"
#include "otf/shim/otf_content_renderer_client.h"

namespace otf {

namespace {

#if BUILDFLAG(IS_LINUX)
// Default the Ozone backend to Wayland when a compositor is present, else X11.
// Wayland is the modern default across Linux desktops; we fall back to X11 so
// X11-only sessions (and Xwayland-less setups) still run. An explicit
// --ozone-platform on the command line always wins. Must run before Ozone init.
void DefaultOzonePlatform(base::CommandLine* command_line) {
  constexpr char kOzonePlatform[] = "ozone-platform";
  if (command_line->HasSwitch(kOzonePlatform)) {
    return;
  }
  std::string platform = "x11";
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  if (env->HasVar("WAYLAND_DISPLAY")) {
    platform = "wayland";
  }
  command_line->AppendSwitchASCII(kOzonePlatform, platform);
}
#endif  // BUILDFLAG(IS_LINUX)

}  // namespace

OtfMainDelegate::OtfMainDelegate() = default;
OtfMainDelegate::~OtfMainDelegate() = default;

std::optional<int> OtfMainDelegate::BasicStartupComplete() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  // Suppress content_shell's built-in test toolbar (Back/Forward/Refresh/Stop +
  // URL field). otf draws its own chrome (the React UI WebContents), so the
  // shell WebView fills the whole window. Forced on here, before the window is
  // built, so it doesn't depend on a launch flag.
  command_line->AppendSwitch(switches::kContentShellHideToolbar);

#if BUILDFLAG(IS_LINUX)
  DefaultOzonePlatform(command_line);
#endif

  return ShellMainDelegate::BasicStartupComplete();
}

content::ContentBrowserClient* OtfMainDelegate::CreateContentBrowserClient() {
  // Store in the base's member (our client derives from ShellContentBrowserClient)
  // so ShellMainDelegate's own uses of browser_client_ keep working and lifetime
  // is owned by the base.
  browser_client_ = std::make_unique<OtfContentBrowserClient>();
  return browser_client_.get();
}

content::ContentRendererClient* OtfMainDelegate::CreateContentRendererClient() {
  renderer_client_ = std::make_unique<OtfContentRendererClient>();
  return renderer_client_.get();
}

}  // namespace otf
