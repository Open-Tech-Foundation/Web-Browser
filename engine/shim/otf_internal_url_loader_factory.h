// Serves otf's internal `browser://` pages (newtab, settings, history, the shell,
// …) from the UI asset directory. `browser://<host>/<path>` maps to a file:
//   - path "/" (or empty)  -> "<host>.html"  ("shell" -> "index.html")
//   - other paths          -> the path itself (shared "/assets/…", etc.)
// Each request is delegated to content's file URL loader for the actual read.

#ifndef OTF_ENGINE_SHIM_OTF_INTERNAL_URL_LOADER_FACTORY_H_
#define OTF_ENGINE_SHIM_OTF_INTERNAL_URL_LOADER_FACTORY_H_

#include "base/files/file_path.h"
#include "base/memory/self_deleting.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"

namespace otf {

// The directory internal pages are served from: `--otf-ui-dir=<path>` if set,
// else "<DIR_ASSETS>/ui" next to the binary.
base::FilePath ResolveUiAssetDir();

class OtfInternalURLLoaderFactory
    : public network::SelfDeletingURLLoaderFactory {
 public:
  // Self-owned: deletes itself once all receivers disconnect.
  static mojo::PendingRemote<network::mojom::URLLoaderFactory> Create(
      base::FilePath root);

  OtfInternalURLLoaderFactory(
      base::FilePath root,
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> factory_receiver,
      base::SelfDeletingPassKey key);

  OtfInternalURLLoaderFactory(const OtfInternalURLLoaderFactory&) = delete;
  OtfInternalURLLoaderFactory& operator=(const OtfInternalURLLoaderFactory&) =
      delete;

 private:
  ~OtfInternalURLLoaderFactory() override;

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override;

  base::FilePath root_;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_INTERNAL_URL_LOADER_FACTORY_H_
