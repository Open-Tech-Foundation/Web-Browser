#include "otf_workspaces_rpc.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "otf_app.h"
#include "otf_handler.h"
#include "otf_utils.h"

namespace otf {
namespace {

using Callback = CefMessageRouterBrowserSide::Handler::Callback;

bool HasOnlyParamKeys(CefRefPtr<CefDictionaryValue> params,
                      const std::set<std::string>& allowed,
                      std::string* error) {
  CefDictionaryValue::KeyList keys;
  params->GetKeys(keys);
  for (const auto& key : keys) {
    const std::string k = key.ToString();
    if (!allowed.count(k)) {
      if (error) *error = "unexpected param: " + k;
      return false;
    }
  }
  return true;
}

bool RequireNoParams(const NativeRpcRequest& request, std::string* error) {
  return request.params && HasOnlyParamKeys(request.params, {}, error);
}

bool ReadId(CefRefPtr<CefDictionaryValue> params,
            int* id,
            std::string* error) {
  if (!params || !params->HasKey("id") || params->GetType("id") != VTYPE_INT) {
    if (error) *error = "id must be an integer";
    return false;
  }
  const int parsed = params->GetInt("id");
  if (parsed <= 0) {
    if (error) *error = "id must be positive";
    return false;
  }
  if (id) *id = parsed;
  return true;
}

bool ReadName(CefRefPtr<CefDictionaryValue> params,
              std::string* name,
              std::string* error) {
  if (!params || !params->HasKey("name") ||
      params->GetType("name") != VTYPE_STRING) {
    if (error) *error = "name must be a string";
    return false;
  }
  std::string parsed = params->GetString("name").ToString();
  if (parsed.empty()) parsed = "Workspace";
  if (name) *name = std::move(parsed);
  return true;
}

std::string BuildWorkspacesJson(const std::vector<Workspace>& workspaces,
                                int active_workspace_id) {
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < workspaces.size(); ++i) {
    const auto& w = workspaces[i];
    if (i > 0) ss << ",";
    ss << JsonObjectBuilder()
              .AddInt("id", w.id)
              .AddString("uuid", w.uuid)
              .AddString("name", w.name)
              .AddString("color", w.color)
              .AddInt("position", w.position)
              .AddBool("active", w.id == active_workspace_id)
              .AddBool("guest", false)
              .Build();
  }
  ss << "]";
  return ss.str();
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

bool HasDuplicateWorkspaceName(OtfHandler* handler,
                               const std::string& name,
                               int ignore_id) {
  if (!handler || !handler->store_) return false;
  const auto existing = handler->store_->GetWorkspaces();
  return std::any_of(existing.begin(), existing.end(),
                     [&name, ignore_id](const Workspace& w) {
                       return w.name == name && w.id != ignore_id;
                     });
}

bool IsRestorableWorkspaceTab(const WorkspaceTab& tab) {
  if (tab.is_image_preview || tab.is_doc_preview) {
    return !tab.preview_local_path.empty();
  }
  return otf::IsPersistableWebUrl(tab.url) &&
         !otf::IsInternalUiUrl(tab.url);
}

void SendWorkspacesUpdated(OtfHandler* handler) {
  handler->SendEvent(JsonObjectBuilder()
                         .AddString("key", "workspaces-updated")
                         .Build());
}

void SendWorkspaceChanged(OtfHandler* handler, int id) {
  handler->SendEvent(JsonObjectBuilder()
                         .AddString("key", "workspace-changed")
                         .AddInt("id", id)
                         .Build());
}

bool EnsureCanMutate(OtfHandler* handler,
                     CefRefPtr<Callback> callback,
                     const NativeRpcRequest& request) {
  if (handler->guest_session_active_) {
    Failure(callback, request, "guest_session",
            "Workspaces are disabled in guest sessions");
    return false;
  }
  if (!handler->store_) {
    Failure(callback, request, "store_unavailable", "store unavailable");
    return false;
  }
  return true;
}

}  // namespace

bool HandleWorkspacesRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler ||
      (request.method != "workspaces.list" &&
       request.method != "workspaces.create" &&
       request.method != "workspaces.rename" &&
       request.method != "workspaces.delete" &&
       request.method != "workspaces.switch" &&
       request.method != "session.isGuest" &&
       request.method != "session.createGuest")) {
    return false;
  }

  std::string error;
  if (request.method == "workspaces.list") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    NativeRpcSuccessRaw(
        callback, request,
        handler->store_ && !handler->guest_session_active_
            ? BuildWorkspacesJson(handler->store_->GetWorkspaces(),
                                  handler->active_workspace_id_)
            : "[]");
    return true;
  }

  if (request.method == "session.isGuest") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    NativeRpcSuccessRaw(callback, request,
                        handler->guest_session_active_ ? "true" : "false");
    return true;
  }

  if (request.method == "session.createGuest") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    handler->StartGuestSession();
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  if (request.method == "workspaces.create") {
    if (!HasOnlyParamKeys(request.params, {"name"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!EnsureCanMutate(handler, callback, request)) return true;
    std::string name;
    if (!ReadName(request.params, &name, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (HasDuplicateWorkspaceName(handler, name, -1)) {
      Failure(callback, request, "duplicate_name", "duplicate name");
      return true;
    }
    const int new_id = handler->store_->CreateWorkspace(name, "");
    if (new_id <= 0) {
      Failure(callback, request, "create_failed", "create failed");
      return true;
    }
    SendWorkspacesUpdated(handler);
    NativeRpcSuccessRaw(callback, request, std::to_string(new_id));
    return true;
  }

  if (request.method == "workspaces.rename") {
    if (!HasOnlyParamKeys(request.params, {"id", "name"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!EnsureCanMutate(handler, callback, request)) return true;
    int id = 0;
    std::string name;
    if (!ReadId(request.params, &id, &error) ||
        !ReadName(request.params, &name, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (HasDuplicateWorkspaceName(handler, name, id)) {
      Failure(callback, request, "duplicate_name", "duplicate name");
      return true;
    }
    if (!handler->store_->RenameWorkspace(id, name)) {
      Failure(callback, request, "rename_failed", "rename failed");
      return true;
    }
    SendWorkspacesUpdated(handler);
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  if (request.method == "workspaces.delete") {
    if (!HasOnlyParamKeys(request.params, {"id"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!EnsureCanMutate(handler, callback, request)) return true;
    int target = 0;
    if (!ReadId(request.params, &target, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    const auto workspaces = handler->store_->GetWorkspaces();
    if (workspaces.size() <= 1) {
      Failure(callback, request, "cannot_delete_last", "cannot delete last");
      return true;
    }
    if (handler->tab_manager_) {
      const auto tab_ids = handler->tab_manager_->GetTabIdsForWorkspace(target);
      for (int tid : tab_ids) {
        handler->CloseTabAndNotify(tid);
      }
    }
    if (handler->active_workspace_id_ == target) {
      int fallback = 1;
      for (const auto& w : workspaces) {
        if (w.id != target) {
          fallback = w.id;
          break;
        }
      }
      handler->active_workspace_id_ = fallback;
      handler->store_->SetActiveWorkspace(fallback);
    }
    handler->store_->DeleteWorkspace(target);
    handler->workspace_contexts_.erase(target);
    if (handler->tab_manager_) {
      handler->tab_manager_->ClearWorkspaceOriginZooms(target);
      handler->tab_manager_->ClearPrivateWorkspaceOriginZooms(target);
    }
    std::error_code ec;
    std::filesystem::remove_all(otf::GetWorkspaceCefCacheDir(target), ec);
    SendWorkspacesUpdated(handler);
    SendWorkspaceChanged(handler, handler->active_workspace_id_);
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }

  if (!HasOnlyParamKeys(request.params, {"id"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  int target = 0;
  if (!ReadId(request.params, &target, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  if (target == handler->active_workspace_id_) {
    NativeRpcSuccessString(callback, request, "ok");
    return true;
  }
  if (!EnsureCanMutate(handler, callback, request)) return true;

  handler->PersistWorkspaceTabs(handler->active_workspace_id_);
  if (OtfApp* app = OtfApp::GetInstance()) {
    handler->workspace_last_active_tab_[handler->active_workspace_id_] =
        app->GetCurrentTabId();
  }

  handler->active_workspace_id_ = target;
  handler->store_->SetActiveWorkspace(target);

  OtfApp* app = OtfApp::GetInstance();
  if (app && handler->tab_manager_) {
    auto tab_ids = handler->tab_manager_->GetTabIdsForWorkspace(target);
    if (!tab_ids.empty()) {
      int target_tab = tab_ids.front();
      const auto last_it = handler->workspace_last_active_tab_.find(target);
      if (last_it != handler->workspace_last_active_tab_.end()) {
        const int last = last_it->second;
        if (std::find(tab_ids.begin(), tab_ids.end(), last) != tab_ids.end()) {
          target_tab = last;
        }
      }
      app->SwitchTab(target_tab);
    } else {
      const auto persisted = handler->store_->GetWorkspaceTabs(target);
      auto active_it = std::find_if(persisted.begin(), persisted.end(),
                                    [](const WorkspaceTab& t) {
                                      return t.was_active &&
                                             IsRestorableWorkspaceTab(t);
                                    });
      if (active_it == persisted.end()) {
        active_it = std::find_if(persisted.begin(), persisted.end(),
                                  [](const WorkspaceTab& t) {
                                    return IsRestorableWorkspaceTab(t);
                                  });
      }

      if (active_it != persisted.end()) {
        std::map<int, int> db_pos_to_tab_id;
        int active_tab_id = -1;
        for (auto it = persisted.begin(); it != persisted.end(); ++it) {
          if (!IsRestorableWorkspaceTab(*it)) continue;
          const int id = app->CreateRestoredTab(*it);
          handler->NotifyNewTab(id, -1);
          db_pos_to_tab_id[it->position] = id;
          if (it == active_it) active_tab_id = id;
        }
        if (db_pos_to_tab_id.size() > 1) {
          std::vector<int> sorted_ids;
          sorted_ids.reserve(db_pos_to_tab_id.size());
          for (auto& [_, tid] : db_pos_to_tab_id) sorted_ids.push_back(tid);
          handler->tab_manager_->SetWorkspaceTabOrder(target, sorted_ids);
        }
        if (active_tab_id >= 0) app->SwitchTab(active_tab_id);
      } else {
        const int new_id = app->CreateTab("browser://newtab");
        handler->NotifyNewTab(new_id, -1);
        app->SwitchTab(new_id);
      }
    }
  }

  handler->ApplySplitViewState(target);
  handler->PersistWorkspaceTabs(target);
  SendWorkspaceChanged(handler, target);
  NativeRpcSuccessString(callback, request, "ok");
  return true;
}

}  // namespace otf
