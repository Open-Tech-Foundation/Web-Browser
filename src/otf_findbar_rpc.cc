#include "otf_findbar_rpc.h"

#include <set>
#include <string>

#include "otf_app.h"
#include "otf_handler.h"
#include "otf_popup_overlay.h"
#include "otf_utils.h"

namespace otf {
namespace {

using Callback = CefMessageRouterBrowserSide::Handler::Callback;

struct FindbarFindRequest {
  int tab_id = -1;
  std::string text;
  bool forward = true;
  bool match_case = false;
  bool find_next = false;
  int seq = 0;
};

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

bool ReadBoolParam(CefRefPtr<CefDictionaryValue> params,
                   const std::string& key,
                   bool* value,
                   std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_BOOL) {
    if (error) *error = key + " must be boolean";
    return false;
  }
  if (value) *value = params->GetBool(key);
  return true;
}

bool ReadIntParam(CefRefPtr<CefDictionaryValue> params,
                  const std::string& key,
                  int* value,
                  std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_INT) {
    if (error) *error = key + " must be an integer";
    return false;
  }
  if (value) *value = params->GetInt(key);
  return true;
}

bool ReadStringParam(CefRefPtr<CefDictionaryValue> params,
                     const std::string& key,
                     std::string* value,
                     std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_STRING) {
    if (error) *error = key + " must be a string";
    return false;
  }
  if (value) *value = params->GetString(key).ToString();
  return true;
}

bool ReadFindParams(CefRefPtr<CefDictionaryValue> params,
                    FindbarFindRequest* out,
                    std::string* error) {
  if (!params || !HasOnlyParamKeys(params,
                                   {"tabId", "text", "forward", "matchCase",
                                    "findNext", "seq"},
                                   error)) {
    return false;
  }
  if (!out) return false;
  return ReadIntParam(params, "tabId", &out->tab_id, error) &&
         ReadStringParam(params, "text", &out->text, error) &&
         ReadBoolParam(params, "forward", &out->forward, error) &&
         ReadBoolParam(params, "matchCase", &out->match_case, error) &&
         ReadBoolParam(params, "findNext", &out->find_next, error) &&
         ReadIntParam(params, "seq", &out->seq, error);
}

std::string BuildFindResultEvent(int count,
                                 int active,
                                 int tab_id,
                                 const std::string& text,
                                 bool final_update,
                                 int seq = 0) {
  JsonObjectBuilder builder;
  builder.AddString("key", "find-result")
      .AddInt("count", count)
      .AddInt("active", active)
      .AddInt("tabId", tab_id)
      .AddBool("final", final_update)
      .AddInt("seq", seq);
  if (!text.empty() || tab_id < 0) {
    builder.AddString("text", text);
  }
  return builder.Build();
}

std::string BuildFindbarClosedEvent(int tab_id) {
  return JsonObjectBuilder()
      .AddString("key", "findbar-closed")
      .AddInt("tabId", tab_id)
      .Build();
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

bool HandleFindbarRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler ||
      (request.method != "findbar.find" &&
       request.method != "findbar.stop" &&
       request.method != "findbar.close")) {
    return false;
  }

  std::string error;
  if (request.method == "findbar.find") {
    FindbarFindRequest findbar_request;
    if (!ReadFindParams(request.params, &findbar_request, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }

    OtfApp* app = OtfApp::GetInstance();
    if (!app || !handler->tab_manager_) {
      Success(callback, request);
      return true;
    }
    int tab_id = app->GetCurrentTabId();
    if (tab_id < 0 || findbar_request.tab_id != tab_id) {
      Success(callback, request);
      return true;
    }

    handler->tab_manager_->SetFindVisible(tab_id, true);
    handler->tab_manager_->SetFindText(tab_id, findbar_request.text);
    handler->tab_manager_->SetFindCase(tab_id, findbar_request.match_case);

    auto target_browser = handler->tab_manager_->GetBrowser(tab_id);
    if (!target_browser) {
      Success(callback, request);
      return true;
    }

    if (findbar_request.text.empty()) {
      target_browser->GetHost()->StopFinding(true);
      if (handler->findbar_subscription_) {
        handler->findbar_subscription_->Success(
            BuildFindResultEvent(0, 0, tab_id, "", true));
      }
    } else {
      handler->pending_find_tab_ = tab_id;
      handler->pending_find_text_ = findbar_request.text;
      handler->pending_find_seq_ = findbar_request.seq;
      target_browser->GetHost()->Find(findbar_request.text,
                                      findbar_request.forward,
                                      findbar_request.match_case,
                                      findbar_request.find_next);
    }
    Success(callback, request);
    return true;
  }

  if (!RequireNoParams(request, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  if (request.method == "findbar.stop") {
    OtfApp* app = OtfApp::GetInstance();
    if (app && handler->tab_manager_) {
      int tab_id = app->GetCurrentTabId();
      if (tab_id >= 0) {
        handler->tab_manager_->ClearFindState(tab_id);
        auto target_browser = handler->tab_manager_->GetBrowser(tab_id);
        if (target_browser) target_browser->GetHost()->StopFinding(true);
      }
    }
    handler->pending_find_tab_ = -1;
    handler->pending_find_text_.clear();
    if (handler->findbar_subscription_) {
      handler->findbar_subscription_->Success(
          BuildFindResultEvent(0, 0, -1, "", true));
    }
    Success(callback, request);
    return true;
  }

  OtfApp* app = OtfApp::GetInstance();
  int tab_id = app ? app->GetCurrentTabId() : -1;
  if (app && app->findbar_overlay_) {
    app->findbar_overlay_->SetVisible(false);
    if (handler->tab_manager_) {
      handler->tab_manager_->ClearFindState(tab_id);
      auto target_browser = handler->tab_manager_->GetBrowser(tab_id);
      if (target_browser) target_browser->GetHost()->StopFinding(true);
    }
    if (handler->pending_find_tab_ == tab_id) {
      handler->pending_find_tab_ = -1;
      handler->pending_find_text_.clear();
    }
    app->FocusCurrentTabContent();
  }
  if (handler->findbar_subscription_) {
    handler->findbar_subscription_->Success(
        BuildFindResultEvent(0, 0, -1, "", true));
    handler->findbar_subscription_->Success(BuildFindbarClosedEvent(tab_id));
  }
  Success(callback, request);
  return true;
}

}  // namespace otf
