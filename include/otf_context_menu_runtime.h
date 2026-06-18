#ifndef OTF_CONTEXT_MENU_RUNTIME_H_
#define OTF_CONTEXT_MENU_RUNTIME_H_

#include "include/cef_base.h"

class CefBrowser;
class CefContextMenuParams;
class CefMenuModel;

namespace otf {

class OtfHandler;

bool IsBlockedContextMenuCommand(int command_id);
void SanitizeContextMenu(CefRefPtr<CefMenuModel> model);
bool HandleContextMenuCommand(OtfHandler* handler,
                              CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefContextMenuParams> params,
                              int command_id);

}  // namespace otf

#endif  // OTF_CONTEXT_MENU_RUNTIME_H_
