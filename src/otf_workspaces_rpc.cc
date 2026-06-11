#include "otf_workspaces_rpc.h"

#include <set>
#include <sstream>
#include <string>
#include <vector>

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

}  // namespace

bool HandleWorkspacesRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler ||
      (request.method != "workspaces.list" &&
       request.method != "session.isGuest" &&
       request.method != "session.createGuest")) {
    return false;
  }

  std::string error;
  if (!RequireNoParams(request, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  if (request.method == "workspaces.list") {
    NativeRpcSuccessRaw(
        callback, request,
        handler->store_ && !handler->guest_session_active_
            ? BuildWorkspacesJson(handler->store_->GetWorkspaces(),
                                  handler->active_workspace_id_)
            : "[]");
    return true;
  }

  if (request.method == "session.isGuest") {
    NativeRpcSuccessRaw(callback, request,
                        handler->guest_session_active_ ? "true" : "false");
    return true;
  }

  handler->StartGuestSession();
  NativeRpcSuccessString(callback, request, "ok");
  return true;
}

}  // namespace otf
