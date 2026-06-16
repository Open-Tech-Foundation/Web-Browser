#ifndef OTF_CONTEXT_MENU_RUNTIME_H_
#define OTF_CONTEXT_MENU_RUNTIME_H_

#include "include/cef_base.h"

class CefMenuModel;

namespace otf {

bool IsBlockedContextMenuCommand(int command_id);
void SanitizeContextMenu(CefRefPtr<CefMenuModel> model);

}  // namespace otf

#endif  // OTF_CONTEXT_MENU_RUNTIME_H_
