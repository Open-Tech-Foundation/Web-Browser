#include "otf_rpc_dispatcher.h"

#include <array>

#include "otf_clear_data_rpc.h"
#include "otf_console_rpc.h"
#include "otf_cookie_tracking_rpc.h"
#include "otf_doc_preview_rpc.h"
#include "otf_downloads_rpc.h"
#include "otf_findbar_rpc.h"
#include "otf_handler.h"
#include "otf_history_bookmarks_rpc.h"
#include "otf_image_preview_rpc.h"
#include "otf_navigation_rpc.h"
#include "otf_permissions_rpc.h"
#include "otf_search_rpc.h"
#include "otf_settings_rpc.h"
#include "otf_site_data_rpc.h"
#include "otf_tabs_rpc.h"
#include "otf_ui_rpc.h"
#include "otf_workspaces_rpc.h"

namespace otf {
namespace {

using Callback = CefMessageRouterBrowserSide::Handler::Callback;
using RpcHandler = bool (*)(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<Callback> callback,
    const NativeRpcRequest& request);

constexpr std::array<RpcHandler, 16> kRpcHandlers = {
    HandleSiteDataRpc,
    HandleClearDataRpc,
    HandleSettingsRpc,
    HandleHistoryBookmarksRpc,
    HandleNavigationRpc,
    HandleDownloadsRpc,
    HandleImagePreviewRpc,
    HandleDocPreviewRpc,
    HandleWorkspacesRpc,
    HandleUiRpc,
    HandleSearchRpc,
    HandlePermissionsRpc,
    HandleFindbarRpc,
    HandleConsoleRpc,
    HandleCookieTrackingRpc,
    HandleTabsRpc,
};

}  // namespace

bool DispatchNativeRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  for (RpcHandler rpc_handler : kRpcHandlers) {
    if (rpc_handler(handler, browser, callback, request)) {
      return true;
    }
  }
  return false;
}

}  // namespace otf
