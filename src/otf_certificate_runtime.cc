#include "otf_certificate_runtime.h"

#include <cstdint>
#include <string>
#include <vector>

#include "include/cef_navigation_entry.h"
#include "include/cef_ssl_info.h"
#include "include/internal/cef_time.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_handler.h"
#include "otf_native_rpc.h"
#include "otf_utils.h"

namespace otf {
namespace {

std::string BuildStringArrayJson(const std::vector<CefString>& values) {
  std::string json = "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      json += ",";
    }
    json += JsonString(values[i].ToString());
  }
  json += "]";
  return json;
}

std::string BuildCertPrincipalJson(CefRefPtr<CefX509CertPrincipal> principal) {
  JsonObjectBuilder builder;
  std::vector<CefString> organizations;
  if (principal) {
    principal->GetOrganizationNames(organizations);
  }

  if (!principal) {
    return builder.AddString("displayName", "")
        .AddString("commonName", "")
        .AddRaw("organization", BuildStringArrayJson(organizations))
        .Build();
  }

  return builder.AddString("displayName", principal->GetDisplayName().ToString())
      .AddString("commonName", principal->GetCommonName().ToString())
      .AddRaw("organization", BuildStringArrayJson(organizations))
      .Build();
}

std::string BuildCertificateValidityJson(CefRefPtr<CefX509Certificate> cert) {
  time_t not_before = 0;
  time_t not_after = 0;
  if (cert) {
    cef_time_t valid_start{};
    cef_time_t valid_expiry{};
    if (cef_time_from_basetime(cert->GetValidStart(), &valid_start)) {
      cef_time_to_timet(&valid_start, &not_before);
    }
    if (cef_time_from_basetime(cert->GetValidExpiry(), &valid_expiry)) {
      cef_time_to_timet(&valid_expiry, &not_after);
    }
  }
  return JsonObjectBuilder()
      .AddRaw("notBefore", std::to_string(static_cast<int64_t>(not_before)))
      .AddRaw("notAfter", std::to_string(static_cast<int64_t>(not_after)))
      .Build();
}

std::string BuildCurrentCertificateJson(CefRefPtr<CefBrowser> browser,
                                        OtfHandler* handler,
                                        int tab_id,
                                        bool* ok,
                                        std::string* reason) {
  if (ok) {
    *ok = false;
  }

  if (!browser || tab_id < 0) {
    if (reason) {
      *reason = "No active tab";
    }
    return JsonObjectBuilder().AddBool("ok", false)
        .AddBool("hasCertificateError", false)
        .AddString("reason", reason ? *reason : "No active tab")
        .Build();
  }

  CefRefPtr<CefNavigationEntry> entry =
      browser->GetHost() ? browser->GetHost()->GetVisibleNavigationEntry()
                         : nullptr;
  CefRefPtr<CefSSLStatus> ssl_status = entry ? entry->GetSSLStatus() : nullptr;
  CefRefPtr<CefX509Certificate> certificate =
      ssl_status ? ssl_status->GetX509Certificate() : nullptr;
  const std::string tab_url =
      handler && handler->tab_manager_ ? handler->tab_manager_->GetUrl(tab_id) : "";
  const std::string entry_url = entry ? entry->GetURL().ToString() : "";
  const std::string url = !tab_url.empty() ? tab_url : entry_url;
  if (!certificate) {
    if (reason) {
      *reason = "No certificate available for current tab";
    }
    return JsonObjectBuilder().AddBool("ok", false)
        .AddBool("hasCertificateError", ssl_status &&
                                        CefIsCertStatusError(ssl_status->GetCertStatus()))
        .AddString("reason", reason ? *reason : "No certificate available for current tab")
        .Build();
  }

  if (ok) {
    *ok = true;
  }
  if (reason) {
    reason->clear();
  }

  return JsonObjectBuilder()
      .AddBool("ok", true)
      .AddBool("hasCertificateError",
               ssl_status && CefIsCertStatusError(ssl_status->GetCertStatus()))
      .AddString("url", url)
      .AddRaw("issuedTo", BuildCertPrincipalJson(certificate->GetSubject()))
      .AddRaw("issuedBy", BuildCertPrincipalJson(certificate->GetIssuer()))
      .AddRaw("validity", BuildCertificateValidityJson(certificate))
      .Build();
}

}  // namespace

bool IsCertificateErrorCode(cef_errorcode_t error_code) {
  return error_code <= ERR_CERT_COMMON_NAME_INVALID &&
         error_code >= ERR_CERT_END;
}

std::string OtfHandler::GetCertificateJsonForTab(int tab_id) {
  CefRefPtr<CefBrowser> tab_browser =
      tab_manager_ ? tab_manager_->GetBrowser(tab_id) : nullptr;
  return BuildCurrentCertificateJson(tab_browser, this, tab_id, nullptr,
                                     nullptr);
}

bool OtfHandler::OnCertificateError(CefRefPtr<CefBrowser> browser,
                                    ErrorCode cert_error,
                                    const CefString& request_url,
                                    CefRefPtr<CefSSLInfo> ssl_info,
                                    CefRefPtr<CefCallback> callback) {
  CEF_REQUIRE_UI_THREAD();

  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (view && tab_manager_) {
    int tab_id = view->GetID();
    const std::string url = request_url.ToString();
    tab_manager_->SetSslError(tab_id, true);
    tab_manager_->SetSslErrorUrl(tab_id, url);
    SendEvent(JsonObjectBuilder()
                  .AddInt("id", tab_id)
                  .AddString("key", "sslError")
                  .AddBool("value", true)
                  .Build());
  }

  return false;
}

}  // namespace otf
