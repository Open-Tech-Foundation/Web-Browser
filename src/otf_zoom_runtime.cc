#include "otf_zoom_runtime.h"

#include <cmath>

#include "include/cef_browser.h"
#include "otf_handler.h"
#include "otf_native_rpc.h"
#include "otf_store.h"
#include "otf_utils.h"

namespace otf {

namespace {

std::string BuildZoomTabPropertyEvent(int tab_id, int zoom_percent) {
  return JsonObjectBuilder()
      .AddInt("id", tab_id)
      .AddString("key", "zoomPercent")
      .AddString("value", std::to_string(zoom_percent))
      .Build();
}

}  // namespace

int ToRoundedZoomPercent(double zoom_level) {
  return static_cast<int>(std::lround(otf::ZoomLevelToPercent(zoom_level)));
}

std::string BuildZoomUpdateEvent(int tab_id, int zoom_percent) {
  return JsonObjectBuilder()
      .AddString("key", "zoom-restore")
      .AddInt("tabId", tab_id)
      .AddInt("zoomPercent", zoom_percent)
      .Build();
}

bool IsPersistableZoomUrl(const std::string& url) {
  return IsPersistableWebUrl(url) && !IsInternalUiUrl(url);
}

bool SaveWorkspaceOriginZoom(TabManager* tab_manager,
                             OtfStore* store,
                             int tab_id,
                             int zoom_percent) {
  if (!tab_manager) return false;
  const int workspace_id = tab_manager->GetWorkspaceId(tab_id);
  const std::string url = tab_manager->GetUrl(tab_id);
  if (workspace_id <= 0 || !IsPersistableZoomUrl(url)) return false;

  const std::string origin = otf::ExtractOrigin(url);
  if (origin.empty()) return false;
  if (tab_manager->IsPrivate(tab_id)) {
    tab_manager->SetPrivateOriginZoom(workspace_id, origin, zoom_percent);
    return true;
  }
  if (!store) return false;
  tab_manager->SetOriginZoom(workspace_id, origin, zoom_percent);
  return store->SetWorkspaceOriginZoom(workspace_id, origin, zoom_percent);
}

bool ApplyWorkspaceOriginZoom(CefRefPtr<CefBrowser> browser,
                              TabManager* tab_manager,
                              int tab_id,
                              int* applied_percent) {
  if (!browser || !tab_manager || tab_manager->IsPrivate(tab_id)) return false;
  const int workspace_id = tab_manager->GetWorkspaceId(tab_id);
  const std::string url = tab_manager->GetUrl(tab_id);
  if (workspace_id <= 0 || !IsPersistableZoomUrl(url)) return false;

  const std::string origin = otf::ExtractOrigin(url);
  if (origin.empty()) return false;
  const int zoom_percent = tab_manager->GetOriginZoom(workspace_id, origin);
  browser->GetHost()->SetZoomLevel(otf::PercentToZoomLevel(zoom_percent));
  tab_manager->SetZoomPercent(tab_id, zoom_percent);
  if (applied_percent) *applied_percent = zoom_percent;
  return true;
}

bool ApplyPrivateTabZoom(CefRefPtr<CefBrowser> browser,
                         TabManager* tab_manager,
                         int tab_id,
                         int* applied_percent) {
  if (!browser || !tab_manager || !tab_manager->IsPrivate(tab_id)) return false;
  int zoom_percent = tab_manager->GetZoomPercent(tab_id);
  const int workspace_id = tab_manager->GetWorkspaceId(tab_id);
  const std::string url = tab_manager->GetUrl(tab_id);
  if (workspace_id > 0 && IsPersistableZoomUrl(url)) {
    const std::string origin = otf::ExtractOrigin(url);
    if (!origin.empty()) {
      zoom_percent = tab_manager->GetPrivateOriginZoom(workspace_id, origin);
    }
  }
  browser->GetHost()->SetZoomLevel(otf::PercentToZoomLevel(zoom_percent));
  tab_manager->SetZoomPercent(tab_id, zoom_percent);
  if (applied_percent) *applied_percent = zoom_percent;
  return true;
}

bool OtfHandler::ApplyTabZoomAction(int tab_id, const std::string& action) {
  if (!tab_manager_) return false;
  CefRefPtr<CefBrowser> browser = tab_manager_->GetBrowser(tab_id);
  if (!browser) return false;

  double next_zoom = browser->GetHost()->GetZoomLevel();
  if (action == "in") {
    next_zoom = otf::ZoomIn(next_zoom);
  } else if (action == "out") {
    next_zoom = otf::ZoomOut(next_zoom);
  } else if (action == "reset") {
    next_zoom = otf::ZoomReset();
  } else {
    return false;
  }

  browser->GetHost()->SetZoomLevel(next_zoom);
  const int pct = ToRoundedZoomPercent(next_zoom);
  tab_manager_->SetZoomPercent(tab_id, pct);
  SaveWorkspaceOriginZoom(tab_manager_, store_.get(), tab_id, pct);
  SendEvent(BuildZoomTabPropertyEvent(tab_id, pct));
  if (zoombar_subscription_) {
    zoombar_subscription_->Success(BuildZoomUpdateEvent(tab_id, pct));
  }
  return true;
}

}  // namespace otf
