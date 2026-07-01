#include "otf/shim/otf_content_client.h"

#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

namespace otf {

OtfContentClient::OtfContentClient() = default;
OtfContentClient::~OtfContentClient() = default;

void OtfContentClient::AddAdditionalSchemes(Schemes* schemes) {
  // `browser://` is otf's internal page scheme (newtab, settings, the shell, …):
  // standard (host/path URLs), secure (a trustworthy origin so the UI runs in a
  // secure context), and CSP-bypassing so its own assets always load.
  schemes->standard_schemes.push_back("browser");
  schemes->secure_schemes.push_back("browser");
  schemes->csp_bypassing_schemes.push_back("browser");
}

std::u16string OtfContentClient::GetLocalizedString(int message_id) {
  return l10n_util::GetStringUTF16(message_id);
}

std::string_view OtfContentClient::GetDataResource(
    int resource_id,
    ui::ResourceScaleFactor scale_factor) {
  return ui::ResourceBundle::GetSharedInstance().GetRawDataResourceForScale(
      resource_id, scale_factor);
}

base::RefCountedMemory* OtfContentClient::GetDataResourceBytes(
    int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().LoadDataResourceBytes(
      resource_id);
}

std::string OtfContentClient::GetDataResourceString(int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
      resource_id);
}

gfx::Image& OtfContentClient::GetNativeImageNamed(int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().GetNativeImageNamed(
      resource_id);
}

}  // namespace otf
