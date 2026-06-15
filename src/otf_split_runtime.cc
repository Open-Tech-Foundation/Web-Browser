#include "otf_split_runtime.h"

#include <algorithm>
#include <string>

#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_native_rpc.h"
#include "otf_store.h"
#include "otf_utils.h"

namespace otf {
namespace {

std::string BuildSplitViewStateJson(const OtfHandler::SplitViewState& state) {
  JsonObjectBuilder builder;
  builder.AddBool("enabled", state.enabled)
      .AddInt("leftTabId", state.left_tab_id)
      .AddInt("rightTabId", state.right_tab_id)
      .AddInt("activeTabId", state.active_tab_id);
  if (!state.left_url.empty()) builder.AddString("leftUrl", state.left_url);
  if (!state.right_url.empty()) builder.AddString("rightUrl", state.right_url);
  if (!state.active_url.empty()) {
    builder.AddString("activeUrl", state.active_url);
  }
  return builder.Build();
}

bool ParseSplitViewStateJson(const std::string& json,
                             OtfHandler::SplitViewState* state) {
  if (!state || json.empty()) return false;
  CefRefPtr<CefValue> value =
      CefParseJSON(json, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return false;
  CefRefPtr<CefDictionaryValue> dict = value->GetDictionary();
  if (!dict) return false;
  state->enabled = dict->HasKey("enabled") && dict->GetBool("enabled");
  state->left_tab_id =
      dict->HasKey("leftTabId") ? dict->GetInt("leftTabId") : -1;
  state->right_tab_id =
      dict->HasKey("rightTabId") ? dict->GetInt("rightTabId") : -1;
  state->active_tab_id =
      dict->HasKey("activeTabId") ? dict->GetInt("activeTabId") : -1;
  state->left_url =
      dict->HasKey("leftUrl") ? dict->GetString("leftUrl").ToString() : "";
  state->right_url =
      dict->HasKey("rightUrl") ? dict->GetString("rightUrl").ToString() : "";
  state->active_url =
      dict->HasKey("activeUrl") ? dict->GetString("activeUrl").ToString() : "";
  return true;
}

}  // namespace

bool IsSplitPlaceholderTab(TabManager* tab_manager, int tab_id) {
  if (!tab_manager || tab_id < 0) return false;
  const std::string url = tab_manager->GetUrl(tab_id);
  const std::string scheme_url = tab_manager->GetSchemeUrl(tab_id);
  return scheme_url == "browser://split-placeholder" ||
         url == "browser://split-placeholder" ||
         url.find("/splitplaceholder.html") != std::string::npos;
}

OtfHandler::SplitViewState OtfHandler::GetSplitViewState(
    int workspace_id) const {
  auto it = workspace_split_states_.find(workspace_id);
  if (it != workspace_split_states_.end()) {
    return it->second;
  }
  SplitViewState state;
  if (store_ && workspace_id > 0) {
    const std::string json =
        store_->GetWorkspaceStateValue(workspace_id, "split");
    if (!json.empty()) {
      ParseSplitViewStateJson(json, &state);
    }
  }
  return state;
}

std::string OtfHandler::BuildSplitViewStateJson(int workspace_id) const {
  return ::otf::BuildSplitViewStateJson(GetSplitViewState(workspace_id));
}

bool OtfHandler::SetSplitViewTabs(int workspace_id,
                                  int left_tab_id,
                                  int right_tab_id,
                                  int active_tab_id) {
  if (workspace_id <= 0 || left_tab_id < 0 || right_tab_id < 0 ||
      left_tab_id == right_tab_id) {
    return ClearSplitViewState(workspace_id);
  }

  SplitViewState state;
  state.enabled = true;
  state.left_tab_id = left_tab_id;
  state.right_tab_id = right_tab_id;
  state.active_tab_id =
      (active_tab_id == right_tab_id) ? right_tab_id : left_tab_id;
  if (tab_manager_) {
    state.left_url = IsSplitPlaceholderTab(tab_manager_, left_tab_id)
                         ? ""
                         : tab_manager_->GetUrl(left_tab_id);
    state.right_url = IsSplitPlaceholderTab(tab_manager_, right_tab_id)
                          ? ""
                          : tab_manager_->GetUrl(right_tab_id);
    state.active_url =
        state.active_tab_id == right_tab_id ? state.right_url : state.left_url;
  }
  workspace_split_states_[workspace_id] = state;
  PersistWorkspaceSplitState(workspace_id);
  return true;
}

bool OtfHandler::ClearSplitViewState(int workspace_id) {
  if (workspace_id <= 0) return false;
  SplitViewState state;
  workspace_split_states_[workspace_id] = state;
  PersistWorkspaceSplitState(workspace_id);
  return true;
}

bool OtfHandler::ApplySplitViewState(int workspace_id) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !tab_manager_) return false;
  SplitViewState state = GetSplitViewState(workspace_id);
  if (!state.enabled) {
    app->ClearSplitView();
    return false;
  }
  const auto ids = tab_manager_->GetTabIdsForWorkspace(workspace_id);
  auto is_alive = [&ids](int tab_id) {
    return std::find(ids.begin(), ids.end(), tab_id) != ids.end();
  };
  auto is_matching_tab = [this, &is_alive](int tab_id,
                                           const std::string& url) {
    if (!is_alive(tab_id)) return false;
    return url.empty() || tab_manager_->GetUrl(tab_id) == url;
  };
  auto find_by_url = [this, &ids](const std::string& url) {
    if (url.empty()) return -1;
    for (int id : ids) {
      if (tab_manager_->GetUrl(id) == url) return id;
    }
    return -1;
  };

  if (!is_matching_tab(state.left_tab_id, state.left_url)) {
    state.left_tab_id = find_by_url(state.left_url);
  }
  if (!is_matching_tab(state.right_tab_id, state.right_url)) {
    state.right_tab_id = find_by_url(state.right_url);
  }
  if (!is_matching_tab(state.active_tab_id, state.active_url)) {
    if (!state.active_url.empty() && state.active_url == state.right_url) {
      state.active_tab_id = state.right_tab_id;
    } else {
      state.active_tab_id = state.left_tab_id;
    }
  }

  const bool left_alive = is_alive(state.left_tab_id);
  const bool right_alive = is_alive(state.right_tab_id);
  if (!left_alive || !right_alive) {
    ClearSplitViewState(workspace_id);
    app->ClearSplitView();
    NotifySplitStateChanged(workspace_id);
    return false;
  }
  workspace_split_states_[workspace_id] = state;
  PersistWorkspaceSplitState(workspace_id);
  app->OpenSplitView(state.left_tab_id, state.right_tab_id,
                     state.active_tab_id);
  NotifySplitStateChanged(workspace_id);
  return true;
}

void OtfHandler::PersistWorkspaceSplitState(int workspace_id) {
  if (!store_ || workspace_id <= 0) return;
  const SplitViewState state = GetSplitViewState(workspace_id);
  if (!state.enabled) {
    store_->SetWorkspaceStateValue(workspace_id, "split", "");
    return;
  }
  if (!tab_manager_ ||
      IsSplitPlaceholderTab(tab_manager_, state.left_tab_id) ||
      IsSplitPlaceholderTab(tab_manager_, state.right_tab_id) ||
      !otf::IsPersistableWebUrl(state.left_url) ||
      !otf::IsPersistableWebUrl(state.right_url)) {
    store_->SetWorkspaceStateValue(workspace_id, "split", "");
    return;
  }
  store_->SetWorkspaceStateValue(workspace_id, "split",
                                 ::otf::BuildSplitViewStateJson(state));
}

void OtfHandler::NotifySplitStateChanged(int workspace_id) {
  if (workspace_id <= 0) return;
  SendEvent(JsonObjectBuilder()
                .AddString("key", "split-state-changed")
                .AddInt("workspaceId", workspace_id)
                .AddRaw("state", BuildSplitViewStateJson(workspace_id))
                .Build());
}

void OtfHandler::SyncSplitStateFromApp() {
  OtfApp* app = OtfApp::GetInstance();
  if (!app) return;
  if (!app->HasSplitView()) {
    if (active_workspace_id_ > 0) {
      ClearSplitViewState(active_workspace_id_);
      NotifySplitStateChanged(active_workspace_id_);
    }
    return;
  }
  SetSplitViewTabs(active_workspace_id_, app->GetSplitLeftTabId(),
                   app->GetSplitRightTabId(), app->GetCurrentTabId());
  NotifySplitStateChanged(active_workspace_id_);
}

bool OtfHandler::IsSplitTab(int tab_id) const {
  if (!tab_manager_ || tab_id < 0) return false;
  const int workspace_id = tab_manager_->GetWorkspaceId(tab_id);
  if (workspace_id <= 0) return false;
  const SplitViewState state = GetSplitViewState(workspace_id);
  return tab_id == state.left_tab_id || tab_id == state.right_tab_id;
}

bool OtfHandler::IsSplitActive() const {
  if (!tab_manager_) return false;
  return GetSplitViewState(active_workspace_id_).enabled;
}

bool OtfHandler::SplitCurrentTab(std::string* error) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !tab_manager_) {
    if (error) *error = "App not ready";
    return false;
  }
  const int active_tab_id = app->GetCurrentTabId();
  if (active_tab_id < 0) {
    if (error) *error = "No active tab";
    return false;
  }
  auto existing_state = GetSplitViewState(active_workspace_id_);
  if (existing_state.enabled) {
    ApplySplitViewState(active_workspace_id_);
    return true;
  }
  const int secondary_tab_id =
      app->CreateTab("browser://split-placeholder", active_tab_id);
  NotifyNewTab(secondary_tab_id, active_tab_id);
  tab_manager_->SetTitle(secondary_tab_id, "Add a tab to split view");
  tab_manager_->SetSchemeUrl(secondary_tab_id, "browser://split-placeholder");
  app->OpenSplitView(active_tab_id, secondary_tab_id, active_tab_id);
  SetSplitViewTabs(active_workspace_id_, active_tab_id, secondary_tab_id,
                   active_tab_id);
  PersistWorkspaceForTab(active_tab_id);
  NotifySplitStateChanged(active_workspace_id_);
  return true;
}

bool OtfHandler::AddTabToSplit(int target_tab_id, std::string* error) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !tab_manager_) {
    if (error) *error = "App not ready";
    return false;
  }
  auto state = GetSplitViewState(active_workspace_id_);
  if (!state.enabled) {
    if (error) *error = "Split view inactive";
    return false;
  }
  const auto ids = tab_manager_->GetTabIdsForWorkspace(active_workspace_id_);
  if (std::find(ids.begin(), ids.end(), target_tab_id) == ids.end()) {
    if (error) *error = "Tab is not in the active workspace";
    return false;
  }
  if (target_tab_id == state.left_tab_id ||
      target_tab_id == state.right_tab_id) {
    app->SwitchTab(target_tab_id);
    SyncSplitStateFromApp();
    return true;
  }
  const bool left_is_placeholder =
      IsSplitPlaceholderTab(tab_manager_, state.left_tab_id);
  const bool right_is_placeholder =
      IsSplitPlaceholderTab(tab_manager_, state.right_tab_id);
  const bool replace_right =
      right_is_placeholder ? true
                           : left_is_placeholder
                                 ? false
                                 : state.active_tab_id != state.right_tab_id;
  const int replaced_tab_id =
      replace_right ? state.right_tab_id : state.left_tab_id;
  const int next_left = replace_right ? state.left_tab_id : target_tab_id;
  const int next_right = replace_right ? target_tab_id : state.right_tab_id;
  app->OpenSplitView(next_left, next_right, target_tab_id);
  SetSplitViewTabs(active_workspace_id_, next_left, next_right, target_tab_id);
  app->ActivateSplitPane(target_tab_id, true);
  const bool should_close_placeholder =
      IsSplitPlaceholderTab(tab_manager_, replaced_tab_id);
  PersistWorkspaceForTab(target_tab_id);
  NotifySplitStateChanged(active_workspace_id_);
  if (should_close_placeholder) {
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(
            [](int tab_id) {
              if (auto* handler = OtfHandler::GetInstance()) {
                handler->CloseTabAndNotify(tab_id);
              }
            },
            replaced_tab_id),
        100);
  }
  return true;
}

bool OtfHandler::CloseSplitView(std::string* error) {
  (void)error;
  OtfApp* app = OtfApp::GetInstance();
  const auto state = GetSplitViewState(active_workspace_id_);
  int placeholder_tab_id = -1;
  if (tab_manager_ && state.enabled) {
    if (IsSplitPlaceholderTab(tab_manager_, state.left_tab_id)) {
      placeholder_tab_id = state.left_tab_id;
    } else if (IsSplitPlaceholderTab(tab_manager_, state.right_tab_id)) {
      placeholder_tab_id = state.right_tab_id;
    }
  }
  if (app) {
    app->ClearSplitView();
  }
  ClearSplitViewState(active_workspace_id_);
  NotifySplitStateChanged(active_workspace_id_);
  if (placeholder_tab_id >= 0) {
    CloseTabAndNotify(placeholder_tab_id);
  }
  return true;
}

bool OtfHandler::SwapSplitView(std::string* error) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app) {
    if (error) *error = "App not ready";
    return false;
  }
  const auto state = GetSplitViewState(active_workspace_id_);
  if (!state.enabled) {
    if (error) *error = "Split view inactive";
    return false;
  }
  const int active_tab_id =
      app->GetCurrentTabId() == state.left_tab_id ? state.right_tab_id
                                                  : state.left_tab_id;
  app->OpenSplitView(state.right_tab_id, state.left_tab_id, active_tab_id);
  SetSplitViewTabs(active_workspace_id_, state.right_tab_id, state.left_tab_id,
                   active_tab_id);
  NotifySplitStateChanged(active_workspace_id_);
  return true;
}

bool OtfHandler::CloseSplitPane(const std::string& pane, std::string* error) {
  const auto state = GetSplitViewState(active_workspace_id_);
  if (!state.enabled) {
    if (error) *error = "Split view inactive";
    return false;
  }
  int tab_id = -1;
  if (pane == "left") {
    tab_id = state.left_tab_id;
  } else if (pane == "right") {
    tab_id = state.right_tab_id;
  } else {
    if (error) *error = "Invalid split pane";
    return false;
  }
  if (tab_manager_ && tab_manager_->IsPinned(tab_id)) {
    if (error) *error = "Pinned tabs cannot be closed";
    return false;
  }
  CloseTabAndNotify(tab_id);
  return true;
}

}  // namespace otf
