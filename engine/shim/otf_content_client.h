// otf's ContentClient: supplies localized strings and packed resources to the
// content layer from the ResourceBundle (loaded from otf's .pak). Replaces
// content_shell's ShellContentClient. Minimal and non-testonly — no web-test
// string overrides, no origin-trial policy yet (origin trials stay disabled).

#ifndef OTF_ENGINE_SHIM_OTF_CONTENT_CLIENT_H_
#define OTF_ENGINE_SHIM_OTF_CONTENT_CLIENT_H_

#include <string>
#include <string_view>

#include "content/public/common/content_client.h"

namespace otf {

class OtfContentClient : public content::ContentClient {
 public:
  OtfContentClient();
  ~OtfContentClient() override;

  // content::ContentClient:
  std::u16string GetLocalizedString(int message_id) override;
  std::string_view GetDataResource(
      int resource_id,
      ui::ResourceScaleFactor scale_factor) override;
  base::RefCountedMemory* GetDataResourceBytes(int resource_id) override;
  std::string GetDataResourceString(int resource_id) override;
  gfx::Image& GetNativeImageNamed(int resource_id) override;
};

}  // namespace otf

#endif  // OTF_ENGINE_SHIM_OTF_CONTENT_CLIENT_H_
