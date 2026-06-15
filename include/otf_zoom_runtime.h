#ifndef OTF_ZOOM_RUNTIME_H_
#define OTF_ZOOM_RUNTIME_H_

#include <string>

#include "include/cef_base.h"

class CefBrowser;

namespace otf {

class OtfStore;
class TabManager;

int ToRoundedZoomPercent(double zoom_level);
std::string BuildZoomUpdateEvent(int tab_id, int zoom_percent);
bool IsPersistableZoomUrl(const std::string& url);
bool SaveWorkspaceOriginZoom(TabManager* tab_manager,
                             OtfStore* store,
                             int tab_id,
                             int zoom_percent);
bool ApplyWorkspaceOriginZoom(CefRefPtr<CefBrowser> browser,
                              TabManager* tab_manager,
                              int tab_id,
                              int* applied_percent);
bool ApplyPrivateTabZoom(CefRefPtr<CefBrowser> browser,
                         TabManager* tab_manager,
                         int tab_id,
                         int* applied_percent);

}  // namespace otf

#endif  // OTF_ZOOM_RUNTIME_H_
