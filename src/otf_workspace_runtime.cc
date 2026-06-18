#include "otf_workspace_runtime.h"

#include <algorithm>
#include <vector>

#include "include/cef_command_line.h"
#include "otf_app.h"
#include "otf_doc_preview_runtime.h"
#include "otf_handler.h"
#include "otf_image_preview_runtime.h"
#include "otf_utils.h"

namespace otf {
namespace {

std::string GetDevUiUrl() {
  return CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("dev-ui-url");
}

std::string TrimTrailingSlash(std::string value) {
  while (value.size() > 1 && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

bool IsDevUiUrl(const std::string& url) {
  const std::string dev_ui_url = TrimTrailingSlash(GetDevUiUrl());
  return !dev_ui_url.empty() &&
         (url == dev_ui_url || url == dev_ui_url + "/" ||
          (url.rfind(dev_ui_url + "/", 0) == 0 &&
           otf::IsInternalUiPagePath(url)));
}

bool IsRestorableWorkspaceTab(const WorkspaceTab& tab) {
  if (tab.is_image_preview) {
    return otf::IsPersistableWebUrl(tab.url);
  }
  if (tab.is_doc_preview) {
    return otf::IsPersistableWebUrl(tab.url);
  }
  return otf::IsPersistableWebUrl(tab.url) &&
         tab.url.rfind("browser://", 0) != 0 &&
         !IsDevUiUrl(tab.url);
}

}  // namespace

void OtfHandler::PersistWorkspaceTabs(int workspace_id) {
  if (!store_ || !tab_manager_ || workspace_id <= 0) return;

  const auto tab_ids = tab_manager_->GetTabIdsForWorkspace(workspace_id);
  if (startup_session_guard_ && workspace_id == active_workspace_id_) {
    const bool all_newtab = std::all_of(tab_ids.begin(), tab_ids.end(),
        [this](int id) {
          const std::string url = tab_manager_->GetUrl(id);
          return url.empty() || url == "browser://newtab";
        });
    if (all_newtab) return;
    startup_session_guard_ = false;
  }

  std::vector<WorkspaceTab> snapshot;
  OtfApp* app = OtfApp::GetInstance();
  const int active_tab = app ? app->GetCurrentTabId() : -1;
  for (int tab_id : tab_ids) {
    if (tab_manager_->IsPrivate(tab_id)) continue;
    WorkspaceTab t;
    if (app && app->GetLazyTab(tab_id, &t)) {
      t.was_active = (tab_id == active_tab);
      snapshot.push_back(t);
      continue;
    }
    t.workspace_id = workspace_id;
    t.is_image_preview =
        tab_manager_->GetImagePreviewMode(tab_id) ==
        ImagePreviewMode::kDedicated;
    t.is_doc_preview =
        tab_manager_->GetDocPreviewMode(tab_id) == DocPreviewMode::kDedicated;
    if (t.is_image_preview) {
      t.url = GetImagePreviewUrlForTab(tab_id);
      t.preview_local_path = GetImagePreviewLocalFileForTab(tab_id);
      t.preview_page = GetImagePreviewPageForTab(tab_id);
    } else if (t.is_doc_preview) {
      t.url = GetDocPreviewUrlForTab(tab_id);
      t.preview_local_path = GetDocPreviewLocalFileForTab(tab_id);
    } else {
      const std::string scheme_url = tab_manager_->GetSchemeUrl(tab_id);
      if (scheme_url.rfind("browser://", 0) == 0) continue;
      t.url = tab_manager_->GetUrl(tab_id);
    }
    if (!IsRestorableWorkspaceTab(t)) continue;
    t.title = tab_manager_->GetTitle(tab_id);
    t.favicon = tab_manager_->GetFaviconUrl(tab_id);
    t.was_active = (tab_id == active_tab);
    t.pinned = tab_manager_->IsPinned(tab_id);
    snapshot.push_back(t);
  }
  store_->ReplaceWorkspaceTabs(workspace_id, snapshot);
}

void OtfHandler::PersistWorkspaceForTab(int tab_id) {
  if (!tab_manager_) return;
  const int workspace_id = tab_manager_->GetWorkspaceId(tab_id);
  if (workspace_id > 0) PersistWorkspaceTabs(workspace_id);
}

}  // namespace otf
