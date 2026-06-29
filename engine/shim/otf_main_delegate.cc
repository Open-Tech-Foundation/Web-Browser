#include "otf/shim/otf_main_delegate.h"

#include <memory>

#include "base/command_line.h"
#include "content/shell/common/shell_switches.h"
#include "otf/shim/otf_content_browser_client.h"
#include "otf/shim/otf_content_renderer_client.h"

namespace otf {

OtfMainDelegate::OtfMainDelegate() = default;
OtfMainDelegate::~OtfMainDelegate() = default;

std::optional<int> OtfMainDelegate::BasicStartupComplete() {
  // Suppress content_shell's built-in test toolbar (Back/Forward/Refresh/Stop +
  // URL field). otf draws its own chrome (the React UI WebContents), so the
  // shell WebView fills the whole window. Forced on here, before the window is
  // built, so it doesn't depend on a launch flag.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kContentShellHideToolbar);
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
