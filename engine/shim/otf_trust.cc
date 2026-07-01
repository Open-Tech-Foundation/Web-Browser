#include "otf/shim/otf_trust.h"

#include <string>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "content/public/browser/render_frame_host.h"
#include "net/base/filename_util.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace otf {

namespace {

// Switch carrying the trusted dev UI origin from the browser to child processes
// (the dev http(s) origin the UI loads from). Absent for the internal browser://
// UI, where the scheme alone establishes trust.
constexpr char kUiOriginSwitch[] = "otf-ui-origin";
constexpr char kDevUiUrlSwitch[] = "dev-ui-url";

}  // namespace

GURL ResolveUiUrl() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(kDevUiUrlSwitch)) {
    GURL dev_url(command_line->GetSwitchValueASCII(kDevUiUrlSwitch));
    if (dev_url.is_valid() && dev_url.has_scheme()) {
      return dev_url;
    }
  }
  const base::CommandLine::StringVector& args = command_line->GetArgs();
  if (args.empty()) {
    // Production default: the shell page, served over the internal scheme.
    return GURL("browser://shell");
  }
  GURL url(args[0]);
  if (url.is_valid() && url.has_scheme()) {
    return url;
  }
  return net::FilePathToFileURL(
      base::MakeAbsoluteFilePath(base::FilePath(args[0])));
}

void InitTrustedUiOrigin() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(kUiOriginSwitch)) {
    return;  // Already recorded (browser) or forwarded (child process).
  }
  GURL ui_url = ResolveUiUrl();
  // Only a real (dev http(s)) origin needs recording; the internal browser://
  // scheme is trusted directly by IsTrustedBridgeUrl.
  if (ui_url.is_valid() && !ui_url.SchemeIs(kInternalScheme)) {
    command_line->AppendSwitchASCII(kUiOriginSwitch,
                                    url::Origin::Create(ui_url).Serialize());
  }
}

void AppendTrustSwitches(base::CommandLine* child_command_line) {
  const base::CommandLine& browser = *base::CommandLine::ForCurrentProcess();
  if (browser.HasSwitch(kUiOriginSwitch) &&
      !child_command_line->HasSwitch(kUiOriginSwitch)) {
    child_command_line->AppendSwitchASCII(
        kUiOriginSwitch, browser.GetSwitchValueASCII(kUiOriginSwitch));
  }
}

bool IsTrustedBridgeUrl(const GURL& url) {
  if (url.SchemeIs(kInternalScheme)) {
    return true;
  }
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(kUiOriginSwitch)) {
    return false;
  }
  const std::string trusted = command_line->GetSwitchValueASCII(kUiOriginSwitch);
  if (trusted.empty() || trusted == "null") {
    return false;  // Opaque/unset origin never matches.
  }
  return url::Origin::Create(url).Serialize() == trusted;
}

bool IsTrustedBridgeFrame(content::RenderFrameHost* render_frame_host) {
  return render_frame_host &&
         IsTrustedBridgeUrl(render_frame_host->GetLastCommittedURL());
}

}  // namespace otf
