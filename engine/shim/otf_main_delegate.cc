#include "otf/shim/otf_main_delegate.h"

#include <memory>

#include "otf/shim/otf_content_browser_client.h"
#include "otf/shim/otf_content_renderer_client.h"

namespace otf {

OtfMainDelegate::OtfMainDelegate() = default;
OtfMainDelegate::~OtfMainDelegate() = default;

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
