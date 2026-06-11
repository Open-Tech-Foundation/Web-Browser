#include "otf_ui_rpc.h"

#include <set>
#include <string>

#include "otf_app.h"
#include "otf_handler.h"
#include "otf_popup_overlay.h"
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

bool ReadPopupName(CefRefPtr<CefDictionaryValue> params,
                   std::string* name,
                   std::string* error) {
  if (!params || !params->HasKey("name") ||
      params->GetType("name") != VTYPE_STRING) {
    if (error) *error = "name must be a string";
    return false;
  }
  std::string parsed = params->GetString("name").ToString();
  if (parsed.empty()) {
    if (error) *error = "name must not be empty";
    return false;
  }
  if (name) *name = std::move(parsed);
  return true;
}

void Failure(CefRefPtr<Callback> callback,
             const NativeRpcRequest& request,
             const std::string& code,
             const std::string& message) {
  NativeRpcFailure(callback, request, code, message);
}

void Success(CefRefPtr<Callback> callback, const NativeRpcRequest& request) {
  NativeRpcSuccessString(callback, request, "ok");
}

bool HandlePopupAction(OtfHandler* handler,
                       CefRefPtr<Callback> callback,
                       const NativeRpcRequest& request) {
  std::string error;
  if (!HasOnlyParamKeys(request.params, {"name"}, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  std::string name;
  if (!ReadPopupName(request.params, &name, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }
  OtfApp* app = OtfApp::GetInstance();
  otf::PopupOverlay* popup = app ? app->GetPopup(name) : nullptr;
  if (!popup) {
    NativeRpcSuccessString(callback, request, "no-such-popup");
    return true;
  }

  if (request.method == "ui.popup.show") {
    if (name == "blockedpopup") {
      int* pid = &handler->popup_ask_pending_id_;
      std::string* purl = &handler->popup_ask_pending_url_;
      std::string* porigin = &handler->popup_ask_pending_origin_;
      popup->SetRestoreProducer([pid, purl, porigin]() {
        return JsonObjectBuilder()
            .AddInt("id", *pid)
            .AddString("url", *purl)
            .AddString("origin", *porigin)
            .Build();
      });
    }
    popup->Show();
  } else if (request.method == "ui.popup.hide") {
    popup->Hide();
  } else {
    popup->Toggle();
  }
  Success(callback, request);
  return true;
}

}  // namespace

bool HandleUiRpc(
    OtfHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefMessageRouterBrowserSide::Handler::Callback> callback,
    const NativeRpcRequest& request) {
  (void)browser;
  if (!handler) return false;

  if (request.method == "ui.popup.show" ||
      request.method == "ui.popup.hide" ||
      request.method == "ui.popup.toggle") {
    return HandlePopupAction(handler, callback, request);
  }

  if (request.method != "ui.appMenu.toggle" &&
      request.method != "ui.appMenu.hide" &&
      request.method != "ui.downloadsBar.toggle" &&
      request.method != "ui.downloadsBar.hide" &&
      request.method != "ui.zoomBar.toggle" &&
      request.method != "ui.zoomBar.hide" &&
      request.method != "ui.certificate.hide" &&
      request.method != "ui.console.toggle" &&
      request.method != "ui.findbar.show") {
    return false;
  }

  std::string error;
  if (!RequireNoParams(request, &error)) {
    Failure(callback, request, "invalid_params", error);
    return true;
  }

  OtfApp* app = OtfApp::GetInstance();
  if (!app) {
    Failure(callback, request, "not_ready", "App not ready");
    return true;
  }

  if (request.method == "ui.appMenu.toggle") {
    if (app->appmenu_overlay_ && app->appmenu_overlay_->IsVisible()) {
      app->HideAppMenuOverlay();
    } else {
      app->ShowAppMenuOverlay();
    }
  } else if (request.method == "ui.appMenu.hide") {
    app->HideAppMenuOverlay();
  } else if (request.method == "ui.downloadsBar.toggle") {
    if (app->downloads_overlay_ && app->downloads_overlay_->IsVisible()) {
      app->HideDownloadsOverlay();
    } else {
      app->ShowDownloadsOverlay();
    }
  } else if (request.method == "ui.downloadsBar.hide") {
    app->HideDownloadsOverlay();
  } else if (request.method == "ui.zoomBar.toggle") {
    if (app->zoombar_overlay_ && app->zoombar_overlay_->IsVisible()) {
      app->HideZoomBarOverlay();
    } else {
      app->ShowZoomBarOverlay();
    }
  } else if (request.method == "ui.zoomBar.hide") {
    app->HideZoomBarOverlay();
  } else if (request.method == "ui.certificate.hide") {
    app->HideCertificateOverlay();
  } else if (request.method == "ui.console.toggle") {
    app->ToggleConsoleOverlay();
  } else if (handler->tab_manager_) {
    const int tab_id = app->GetCurrentTabId();
    if (tab_id >= 0) {
      handler->tab_manager_->SetFindVisible(tab_id, true);
      app->RestoreFindSessionForTab(tab_id, true);
    }
  }

  Success(callback, request);
  return true;
}

}  // namespace otf
