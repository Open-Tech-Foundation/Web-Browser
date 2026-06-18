#include "otf_event_runtime.h"

#include <sstream>
#include <string>
#include <vector>

#include "include/wrapper/cef_helpers.h"
#include "otf_handler.h"
#include "otf_message_router_handler.h"
#include "otf_utils.h"

namespace otf {
namespace {

std::string BuildTabJson(TabManager* tab_manager, OtfStore* store, int tab_id) {
  JsonObjectBuilder builder;
  const std::string url = tab_manager ? tab_manager->GetUrl(tab_id) : "";
  OtfHandler* handler = OtfHandler::GetInstance();
  const bool is_guest_tab = handler && tab_manager && handler->IsGuestTab(tab_id);
  builder.AddInt("id", tab_id)
      .AddString("url", url)
      .AddString("title", tab_manager ? tab_manager->GetTitle(tab_id)
                                      : "New Tab");
  if (tab_manager) {
    builder.AddInt("zoomPercent", tab_manager->GetZoomPercent(tab_id));
    builder.AddBool("sslError", tab_manager->HasSslError(tab_id));
    builder.AddBool("muted", tab_manager->GetMuted(tab_id));
    builder.AddBool("private", tab_manager->IsPrivate(tab_id));
    builder.AddBool("pinned", tab_manager->IsPinned(tab_id));
    builder.AddBool("guest", is_guest_tab);
    const std::string favicon = tab_manager->GetFaviconUrl(tab_id);
    if (!favicon.empty()) {
      builder.AddString("favicon", favicon);
    }
  }
  builder.AddBool("bookmarked",
                  store && tab_manager && !is_guest_tab &&
                      IsPersistableWebUrl(url) &&
                      store->IsBookmarked(NormalizeBookmarkUrl(url)));
  return builder.Build();
}

}  // namespace

std::string OtfHandler::BuildTabsJson() const {
  std::stringstream ss;
  ss << "[";
  const std::vector<int> ids =
      guest_session_active_
          ? tab_manager_->GetTabIdsForWorkspace(0)
          : tab_manager_->GetTabIdsForWorkspace(active_workspace_id_);
  for (size_t i = 0; i < ids.size(); ++i) {
    ss << BuildTabJson(tab_manager_, store_.get(), ids[i]);
    if (i + 1 < ids.size()) ss << ",";
  }
  ss << "]";
  return ss.str();
}

void OtfHandler::NotifyNewTab(int new_tab_id, int parent_tab_id) {
  SendEvent(JsonObjectBuilder()
                .AddString("key", "new-tab")
                .AddRaw("tab", BuildTabJson(tab_manager_, store_.get(), new_tab_id))
                .AddInt("parentTabId", parent_tab_id)
                .Build());
}

void OtfHandler::SendEvent(const std::string& event_json) {
  if (subscription_callback_) {
    subscription_callback_->Success(event_json);
  }
}

void OtfHandler::NotifyMessageRouterBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                                 CefRefPtr<CefFrame> frame) {
  if (message_router_) {
    message_router_->OnBeforeBrowse(browser, frame);
  }
}

void OtfHandler::EnsureMessageRouterInitialized() {
  if (message_router_) {
    return;
  }
  CefMessageRouterConfig config;
  config.js_query_function = "cefQuery";
  config.js_cancel_function = "cefQueryCancel";
  message_router_ = CefMessageRouterBrowserSide::Create(config);
  message_router_->AddHandler(new OtfMessageRouterHandler(), true);
}

bool OtfHandler::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          CefProcessId source_process,
                                          CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  return message_router_->OnProcessMessageReceived(browser, frame,
                                                   source_process, message);
}

}  // namespace otf
