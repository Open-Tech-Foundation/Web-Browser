#include "otf_tabs_rpc.h"

#include <set>
#include <string>

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

bool ReadTabId(CefRefPtr<CefDictionaryValue> params,
               int* tab_id,
               std::string* error) {
  if (!params || !params->HasKey("tabId") ||
      params->GetType("tabId") != VTYPE_INT) {
    if (error) *error = "tabId must be an integer";
    return false;
  }
  const int parsed = params->GetInt("tabId");
  if (parsed < 0) {
    if (error) *error = "tabId must be non-negative";
    return false;
  }
  if (tab_id) *tab_id = parsed;
  return true;
}

bool RequireTabIdParams(const NativeRpcRequest& request,
                        int* tab_id,
                        std::string* error) {
  return request.params && HasOnlyParamKeys(request.params, {"tabId"}, error) &&
         ReadTabId(request.params, tab_id, error);
}

bool ReadPane(CefRefPtr<CefDictionaryValue> params,
              std::string* pane,
              std::string* error) {
  if (!params || !params->HasKey("pane") ||
      params->GetType("pane") != VTYPE_STRING) {
    if (error) *error = "pane must be a string";
    return false;
  }
  const std::string parsed = params->GetString("pane");
  if (parsed != "left" && parsed != "right") {
    if (error) *error = "pane must be left or right";
    return false;
  }
  if (pane) *pane = parsed;
  return true;
}

bool RequirePaneParams(const NativeRpcRequest& request,
                       std::string* pane,
                       std::string* error) {
  return request.params && HasOnlyParamKeys(request.params, {"pane"}, error) &&
         ReadPane(request.params, pane, error);
}

void Success(CefRefPtr<Callback> callback, const NativeRpcRequest& request) {
  NativeRpcSuccessRaw(callback, request, "null");
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

}  // namespace

bool HandleTabsRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler ||
      (request.method != "tabs.list" &&
       request.method != "tabs.active" &&
       request.method != "tabs.splitState" &&
       request.method != "tabs.close" &&
       request.method != "tabs.switch" &&
       request.method != "tabs.back" &&
       request.method != "tabs.forward" &&
       request.method != "tabs.reload" &&
       request.method != "tabs.stop" &&
       request.method != "tabs.mute" &&
       request.method != "tabs.unmute" &&
       request.method != "tabs.zoomIn" &&
       request.method != "tabs.zoomOut" &&
       request.method != "tabs.zoomReset" &&
       request.method != "split.current" &&
       request.method != "split.addTab" &&
       request.method != "split.withCurrent" &&
       request.method != "split.close" &&
       request.method != "split.swap" &&
       request.method != "split.closePane")) {
    return false;
  }

  std::string error;
  if (request.method == "tabs.list") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    NativeRpcSuccessRaw(callback, request, handler->BuildTabsJson());
    return true;
  }

  if (request.method == "tabs.active") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    OtfApp* app = OtfApp::GetInstance();
    NativeRpcSuccessRaw(callback, request,
                        std::to_string(app ? app->GetCurrentTabId() : -1));
    return true;
  }

  if (request.method == "tabs.splitState") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    NativeRpcSuccessRaw(
        callback, request,
        handler->BuildSplitViewStateJson(handler->active_workspace_id_));
    return true;
  }

  if (request.method == "split.current") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!handler->SplitCurrentTab(&error)) {
      Failure(callback, request, "failed", error);
      return true;
    }
    Success(callback, request);
    return true;
  }

  if (request.method == "split.close") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!handler->CloseSplitView(&error)) {
      Failure(callback, request, "failed", error);
      return true;
    }
    Success(callback, request);
    return true;
  }

  if (request.method == "split.swap") {
    if (!RequireNoParams(request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!handler->SwapSplitView(&error)) {
      Failure(callback, request, "failed", error);
      return true;
    }
    Success(callback, request);
    return true;
  }

  if (request.method == "split.closePane") {
    std::string pane;
    if (!RequirePaneParams(request, &pane, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (!handler->CloseSplitPane(pane, &error)) {
      Failure(callback, request, "failed", error);
      return true;
    }
    Success(callback, request);
    return true;
  }

  int tab_id = -1;
  if (!RequireTabIdParams(request, &tab_id, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  if (request.method == "split.addTab" ||
      request.method == "split.withCurrent") {
    if (!handler->AddTabToSplit(tab_id, &error)) {
      Failure(callback, request, "failed", error);
      return true;
    }
    Success(callback, request);
    return true;
  }

  if (request.method == "tabs.close") {
    const int workspace_id =
        handler->tab_manager_ ? handler->tab_manager_->GetWorkspaceId(tab_id) : 0;
    const bool closes_guest_session =
        handler->guest_session_active_ && workspace_id == 0 &&
        handler->tab_manager_ &&
        handler->tab_manager_->GetTabIdsForWorkspace(0).size() == 1;
    if (closes_guest_session) {
      Success(callback, request);
      handler->CloseTabAndNotify(tab_id);
      return true;
    }
    if (OtfApp::GetInstance()) {
      handler->CloseTabAndNotify(tab_id);
    }
    if (workspace_id > 0) {
      handler->PersistWorkspaceTabs(workspace_id);
    }
    Success(callback, request);
    return true;
  }

  if (request.method == "tabs.switch") {
    OtfApp* app = OtfApp::GetInstance();
    if (app) app->SwitchTab(tab_id);
    if (handler->IsSplitTab(tab_id)) {
      handler->SyncSplitStateFromApp();
    }
    handler->PersistWorkspaceForTab(tab_id);
    Success(callback, request);
    return true;
  }

  CefRefPtr<CefBrowser> target =
      handler->tab_manager_ ? handler->tab_manager_->GetBrowser(tab_id)
                            : nullptr;
  if (request.method == "tabs.back") {
    if (target) target->GoBack();
  } else if (request.method == "tabs.forward") {
    if (target) target->GoForward();
  } else if (request.method == "tabs.reload") {
    if (target) target->Reload();
  } else if (request.method == "tabs.stop") {
    if (target) target->StopLoad();
  } else if (request.method == "tabs.zoomIn") {
    handler->ApplyTabZoomAction(tab_id, "in");
  } else if (request.method == "tabs.zoomOut") {
    handler->ApplyTabZoomAction(tab_id, "out");
  } else if (request.method == "tabs.zoomReset") {
    handler->ApplyTabZoomAction(tab_id, "reset");
  } else if (request.method == "tabs.mute") {
    if (target) {
      target->GetHost()->SetAudioMuted(true);
      handler->tab_manager_->SetMuted(tab_id, true);
      handler->SendEvent(JsonObjectBuilder()
                             .AddString("key", "muted")
                             .AddInt("id", tab_id)
                             .AddBool("value", true)
                             .Build());
    }
  } else if (target) {
    target->GetHost()->SetAudioMuted(false);
    handler->tab_manager_->SetMuted(tab_id, false);
    handler->SendEvent(JsonObjectBuilder()
                           .AddString("key", "muted")
                           .AddInt("id", tab_id)
                           .AddBool("value", false)
                           .Build());
  }
  Success(callback, request);
  return true;
}

}  // namespace otf
