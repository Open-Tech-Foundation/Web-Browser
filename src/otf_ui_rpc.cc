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

bool ReadStringParam(CefRefPtr<CefDictionaryValue> params,
                     const std::string& key,
                     std::string* value,
                     std::string* error) {
  if (!params || !params->HasKey(key) || params->GetType(key) != VTYPE_STRING) {
    if (error) *error = key + " must be a string";
    return false;
  }
  std::string parsed = params->GetString(key).ToString();
  if (parsed.empty()) {
    if (error) *error = key + " must not be empty";
    return false;
  }
  if (value) *value = std::move(parsed);
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
  const int parsed = params->GetInt(key);
  if (parsed < 0) {
    if (error) *error = key + " must be non-negative";
    return false;
  }
  if (value) *value = parsed;
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

bool HandlePopupRestoreSubscribe(CefRefPtr<Callback> callback,
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
    Failure(callback, request, "not_found", "Popup not found");
    return true;
  }
  popup->SetRestoreSubscriber(callback);
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
  if (request.method == "ui.popup.restoreSubscribe") {
    return HandlePopupRestoreSubscribe(callback, request);
  }

  if (request.method != "ui.appMenu.toggle" &&
      request.method != "ui.appMenu.hide" &&
      request.method != "ui.focus" &&
      request.method != "ui.downloadsBar.toggle" &&
      request.method != "ui.downloadsBar.hide" &&
      request.method != "ui.zoomBar.toggle" &&
      request.method != "ui.zoomBar.hide" &&
      request.method != "ui.fullscreen.toggle" &&
      request.method != "ui.bookmarkBar.hide" &&
      request.method != "ui.certificate.toggle" &&
      request.method != "ui.certificate.hide" &&
      request.method != "ui.certificate.get" &&
      request.method != "ui.console.toggle" &&
      request.method != "ui.console.show" &&
      request.method != "ui.console.hide" &&
      request.method != "ui.toast.show" &&
      request.method != "ui.qr.show" &&
      request.method != "ui.snip.start" &&
      request.method != "ui.snipPreview.hide" &&
      request.method != "ui.findbar.show") {
    return false;
  }

  std::string error;
  if (request.method == "ui.toast.show") {
    if (!HasOnlyParamKeys(request.params, {"icon", "message"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    std::string icon;
    std::string message;
    if (!ReadStringParam(request.params, "icon", &icon, &error) ||
        !ReadStringParam(request.params, "message", &message, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->ShowToast(icon, message);
    }
    Success(callback, request);
    return true;
  }

  if (request.method == "ui.qr.show") {
    if (!HasOnlyParamKeys(request.params, {"url"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    std::string url;
    if (!ReadStringParam(request.params, "url", &url, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    handler->pending_qr_url_ = otf::StripTrackingParamsFromUrl(url);
    if (OtfApp* app = OtfApp::GetInstance()) {
      otf::PopupOverlay* popup = app->GetPopup("qr");
      if (popup) {
        std::string* pending = &handler->pending_qr_url_;
        popup->SetRestoreProducer([pending]() {
          return JsonObjectBuilder().AddString("url", *pending).Build();
        });
        popup->Show();
      }
    }
    Success(callback, request);
    return true;
  }

  if (request.method == "ui.certificate.get") {
    if (!HasOnlyParamKeys(request.params, {"tabId"}, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    int tab_id = -1;
    if (!ReadIntParam(request.params, "tabId", &tab_id, &error)) {
      Failure(callback, request, "invalid_params", error);
      return true;
    }
    NativeRpcSuccessRaw(callback, request,
                        handler->GetCertificateJsonForTab(tab_id));
    return true;
  }

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
  } else if (request.method == "ui.focus") {
    if (handler->ui_browser_) {
      handler->ui_browser_->GetHost()->SetFocus(true);
    }
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
  } else if (request.method == "ui.fullscreen.toggle") {
    app->ToggleFullscreen();
  } else if (request.method == "ui.bookmarkBar.hide") {
    app->HideBookmarkOverlay();
  } else if (request.method == "ui.certificate.toggle") {
    if (app->certificate_overlay_ && app->certificate_overlay_->IsVisible()) {
      app->HideCertificateOverlay();
    } else {
      app->ShowCertificateOverlay();
    }
  } else if (request.method == "ui.certificate.hide") {
    app->HideCertificateOverlay();
  } else if (request.method == "ui.console.toggle") {
    app->ToggleConsoleOverlay();
  } else if (request.method == "ui.console.show") {
    app->ShowConsoleOverlay();
  } else if (request.method == "ui.console.hide") {
    app->HideConsoleOverlay();
  } else if (request.method == "ui.snip.start") {
    if (!handler->StartSnipCapture(true, &error)) {
      Failure(callback, request, "failed", error);
      return true;
    }
  } else if (request.method == "ui.snipPreview.hide") {
    app->HideSnipPreviewOverlay();
    app->FocusCurrentTabContent();
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
