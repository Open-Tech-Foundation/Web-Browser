#include "otf/shim/otf_main_delegate.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "otf/shim/otf_content_browser_client.h"
#include "otf/shim/otf_content_client.h"
#include "otf/shim/otf_content_renderer_client.h"
#include "ui/base/resource/resource_bundle.h"

namespace otf {

namespace {

#if BUILDFLAG(IS_LINUX)
// Default the Ozone backend to Wayland when a compositor is present, else X11.
// Wayland is the modern default across Linux desktops; we fall back to X11 so
// X11-only sessions still run. An explicit --ozone-platform always wins. Must
// run before Ozone init.
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

// Loads otf's resource pack into the shared ResourceBundle. otf.pak ships next to
// the binary (DIR_ASSETS) and aggregates content/blink/ui/views resources.
void InitializeResourceBundle() {
  base::FilePath pak_file;
  CHECK(base::PathService::Get(base::DIR_ASSETS, &pak_file));
  pak_file = pak_file.Append(FILE_PATH_LITERAL("otf.pak"));
  ui::ResourceBundle::InitSharedInstanceWithPakPath(pak_file);
}

}  // namespace

OtfMainDelegate::OtfMainDelegate() = default;
OtfMainDelegate::~OtfMainDelegate() = default;

std::optional<int> OtfMainDelegate::BasicStartupComplete() {
#if BUILDFLAG(IS_LINUX)
  DefaultOzonePlatform(base::CommandLine::ForCurrentProcess());
#endif
  return std::nullopt;
}

void OtfMainDelegate::PreSandboxStartup() {
  InitializeResourceBundle();
}

content::ContentClient* OtfMainDelegate::CreateContentClient() {
  content_client_ = std::make_unique<OtfContentClient>();
  return content_client_.get();
}

content::ContentBrowserClient* OtfMainDelegate::CreateContentBrowserClient() {
  browser_client_ = std::make_unique<OtfContentBrowserClient>();
  return browser_client_.get();
}

content::ContentRendererClient* OtfMainDelegate::CreateContentRendererClient() {
  renderer_client_ = std::make_unique<OtfContentRendererClient>();
  return renderer_client_.get();
}

}  // namespace otf
