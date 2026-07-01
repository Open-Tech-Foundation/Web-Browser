#include "otf/shim/otf_internal_url_loader_factory.h"

#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "content/public/browser/file_url_loader.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/filename_util.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace otf {

namespace {

// Map a browser:// URL to the relative asset path under the UI dir. Empty on a
// disallowed (traversal / absolute) path.
base::FilePath AssetPathFor(const base::FilePath& root, const GURL& url) {
  const std::string host(url.host());
  const std::string path(url.path());
  std::string relative;
  if (path.empty() || path == "/") {
    // The shell is index.html; every other host is "<host>.html".
    relative = (host == "shell") ? "index.html" : host + ".html";
  } else {
    relative = path.substr(1);  // strip the leading '/'
  }
  base::FilePath rel = base::FilePath::FromUTF8Unsafe(relative);
  if (rel.empty() || rel.IsAbsolute() || rel.ReferencesParent()) {
    return base::FilePath();
  }
  return root.Append(rel);
}

}  // namespace

base::FilePath ResolveUiAssetDir() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch("otf-ui-dir")) {
    return command_line->GetSwitchValuePath("otf-ui-dir");
  }
  base::FilePath assets;
  base::PathService::Get(base::DIR_ASSETS, &assets);
  return assets.Append(FILE_PATH_LITERAL("ui"));
}

// static
mojo::PendingRemote<network::mojom::URLLoaderFactory>
OtfInternalURLLoaderFactory::Create(base::FilePath root) {
  mojo::PendingRemote<network::mojom::URLLoaderFactory> pending_remote;
  // Self-owned: deletes itself when its last receiver disconnects.
  base::MakeSelfDeleting<OtfInternalURLLoaderFactory>(
      std::move(root), pending_remote.InitWithNewPipeAndPassReceiver());
  return pending_remote;
}

OtfInternalURLLoaderFactory::OtfInternalURLLoaderFactory(
    base::FilePath root,
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> factory_receiver,
    base::SelfDeletingPassKey key)
    : network::SelfDeletingURLLoaderFactory(std::move(factory_receiver), key),
      root_(std::move(root)) {}

OtfInternalURLLoaderFactory::~OtfInternalURLLoaderFactory() = default;

void OtfInternalURLLoaderFactory::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader,
    int32_t /*request_id*/,
    uint32_t /*options*/,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& /*traffic_annotation*/) {
  const base::FilePath file = AssetPathFor(root_, request.url);
  if (file.empty()) {
    mojo::Remote<network::mojom::URLLoaderClient>(std::move(client))
        ->OnComplete(network::URLLoaderCompletionStatus(net::ERR_INVALID_URL));
    return;
  }
  // Delegate the read/mime/data-pipe work to content's file URL loader, pointed
  // at the resolved file (security is our AssetPathFor confinement above).
  network::ResourceRequest file_request = request;
  file_request.url = net::FilePathToFileURL(file);
  content::CreateFileURLLoaderBypassingSecurityChecks(
      file_request, std::move(loader), std::move(client),
      /*observer=*/nullptr, /*allow_directory_listing=*/false);
}

}  // namespace otf
