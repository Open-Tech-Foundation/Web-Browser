#include "otf_request_context_runtime.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "include/cef_request_context.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_store.h"
#include "otf_utils.h"

namespace otf {
namespace {

std::filesystem::path ResolveWorkspaceCachePath(int workspace_id) {
  std::filesystem::path base = otf::GetWorkspaceCefCacheDir(workspace_id);
  if (!base.empty()) {
    return base;
  }
  return std::filesystem::temp_directory_path() / "otf-browser" / "cache" /
         "cef" / "workspaces" / std::to_string(workspace_id);
}

CefRefPtr<CefRequestContext> CreateRequestContext(
    const std::filesystem::path& cache_path = std::filesystem::path()) {
  CefRequestContextSettings settings;
  if (!cache_path.empty()) {
#if defined(_WIN32)
    CefString(&settings.cache_path) = cache_path.wstring();
#else
    CefString(&settings.cache_path).FromString(cache_path.string());
#endif
    settings.persist_session_cookies = true;
  }
  return CefRequestContext::CreateContext(settings, nullptr);
}

void InitializeRequestContext(OtfHandler* handler,
                              CefRefPtr<CefRequestContext> ctx) {
  if (!ctx) {
    return;
  }
  if (OtfApp* app = OtfApp::GetInstance()) {
    app->RegisterBrowserSchemeForContext(ctx);
  }
  handler->ApplyAlwaysOnPrivacyPreferences(ctx);
}

std::string BuildGuestSessionChangedEvent(bool active) {
  return JsonObjectBuilder()
      .AddString("key", "guest-session-changed")
      .AddBool("active", active)
      .Build();
}

std::string BuildWorkspacesUpdatedEvent() {
  return JsonObjectBuilder().AddString("key", "workspaces-updated").Build();
}

std::string BuildWorkspaceChangedEvent(int workspace_id) {
  return JsonObjectBuilder()
      .AddString("key", "workspace-changed")
      .AddInt("id", workspace_id)
      .Build();
}

void ClearGuestDownloadState(OtfHandler* handler) {
  for (auto it = handler->downloads_.begin(); it != handler->downloads_.end();) {
    if (it->first < 0) {
      it = handler->downloads_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = handler->runtime_download_ids_.begin();
       it != handler->runtime_download_ids_.end();) {
    if (it->second < 0) {
      it = handler->runtime_download_ids_.erase(it);
    } else {
      ++it;
    }
  }
}

int ResolvePostGuestTab(OtfHandler* handler, int workspace_id, int preferred_tab_id) {
  if (!handler->tab_manager_ || preferred_tab_id < 0) {
    return -1;
  }
  const auto tabs = handler->tab_manager_->GetTabIdsForWorkspace(workspace_id);
  if (std::find(tabs.begin(), tabs.end(), preferred_tab_id) != tabs.end()) {
    return preferred_tab_id;
  }
  return tabs.empty() ? -1 : tabs.front();
}

void RestoreTabAfterGuestSession(OtfHandler* handler,
                                 OtfApp* app,
                                 int restore_tab_id) {
  if (!app) {
    return;
  }
  if (restore_tab_id >= 0) {
    app->SwitchTab(restore_tab_id);
    return;
  }

  const int new_tab_id = app->CreateTab("browser://newtab");
  handler->NotifyNewTab(new_tab_id, -1);
  app->SwitchTab(new_tab_id);
}

}  // namespace

CefRefPtr<CefRequestContext> OtfHandler::GetActiveWorkspaceRequestContext() {
  if (guest_session_active_) {
    return GetGuestRequestContext();
  }
  return GetWorkspaceRequestContext(active_workspace_id_);
}

CefRefPtr<CefRequestContext> OtfHandler::GetWorkspaceRequestContext(
    int workspace_id) {
  // The default workspace shares the global context so we don't have to
  // migrate existing on-disk state, and so that "no workspaces created"
  // behaves identically to the pre-workspaces build.
  if (workspace_id <= 1) {
    return nullptr;
  }
  auto it = workspace_contexts_.find(workspace_id);
  if (it != workspace_contexts_.end() && it->second) {
    return it->second;
  }
  std::filesystem::path base = ResolveWorkspaceCachePath(workspace_id);
  std::error_code ec;
  std::filesystem::create_directories(base, ec);

  CefRefPtr<CefRequestContext> ctx = CreateRequestContext(base);
  InitializeRequestContext(this, ctx);
  workspace_contexts_[workspace_id] = ctx;
  return ctx;
}

CefRefPtr<CefRequestContext> OtfHandler::GetGuestRequestContext() {
  CEF_REQUIRE_UI_THREAD();
  if (guest_context_) {
    return guest_context_;
  }

  CefRefPtr<CefRequestContext> ctx = CreateRequestContext();
  InitializeRequestContext(this, ctx);
  guest_context_ = ctx;
  return ctx;
}

bool OtfHandler::IsGuestTab(int tab_id) const {
  if (tab_id < 0) {
    return guest_session_active_;
  }
  return tab_manager_ && tab_manager_->GetWorkspaceId(tab_id) == 0;
}

void OtfHandler::StartGuestSession() {
  CEF_REQUIRE_UI_THREAD();
  OtfApp* app = OtfApp::GetInstance();
  if (!app) return;
  if (guest_session_active_) {
    const auto guest_tabs =
        tab_manager_ ? tab_manager_->GetTabIdsForWorkspace(0) : std::vector<int>{};
    if (!guest_tabs.empty()) {
      app->SwitchTab(guest_tabs.front());
      return;
    }
  }

  pre_guest_workspace_id_ = active_workspace_id_;
  pre_guest_tab_id_ = app->GetCurrentTabId();
  PersistWorkspaceTabs(pre_guest_workspace_id_);
  if (pre_guest_tab_id_ >= 0) {
    workspace_last_active_tab_[pre_guest_workspace_id_] = pre_guest_tab_id_;
  }
  app->ClearSplitView();

  guest_session_active_ = true;
  const int tab_id = app->CreateTab("browser://newtab");
  if (tab_manager_) tab_manager_->SetWorkspaceId(tab_id, 0);
  NotifyNewTab(tab_id, -1);
  app->SwitchTab(tab_id);
  SendEvent(BuildGuestSessionChangedEvent(true));
  SendEvent(BuildWorkspacesUpdatedEvent());
}

void OtfHandler::EndGuestSession(bool restore_normal_tabs) {
  CEF_REQUIRE_UI_THREAD();
  if (!guest_session_active_) return;
  OtfApp* app = OtfApp::GetInstance();

  guest_session_active_ = false;
  if (tab_manager_) {
    tab_manager_->ClearWorkspaceOriginZooms(0);
    tab_manager_->ClearPrivateWorkspaceOriginZooms(0);
  }
  ClearGuestDownloadState(this);
  guest_context_ = nullptr;

  active_workspace_id_ = pre_guest_workspace_id_ > 0 ? pre_guest_workspace_id_ : 1;
  if (store_) store_->SetActiveWorkspace(active_workspace_id_);

  if (restore_normal_tabs) {
    const int restore_tab =
        ResolvePostGuestTab(this, active_workspace_id_, pre_guest_tab_id_);
    RestoreTabAfterGuestSession(this, app, restore_tab);
  }

  pre_guest_tab_id_ = -1;
  SendEvent(BuildGuestSessionChangedEvent(false));
  SendEvent(BuildWorkspacesUpdatedEvent());
  SendEvent(BuildWorkspaceChangedEvent(active_workspace_id_));
}

CefRefPtr<CefRequestContext> OtfHandler::GetPrivateRequestContext() {
  CEF_REQUIRE_UI_THREAD();
  if (private_context_) {
    return private_context_;
  }
  // Empty cache_path => in-memory only. Nothing is written to disk and the
  // session is destroyed when the context is released.
  CefRefPtr<CefRequestContext> ctx = CreateRequestContext();
  InitializeRequestContext(this, ctx);
  private_context_ = ctx;
  return ctx;
}

void OtfHandler::MaybeReleasePrivateContext() {
  CEF_REQUIRE_UI_THREAD();
  if (private_context_ && tab_manager_ && !tab_manager_->HasPrivateTabs()) {
    private_context_ = nullptr;
    tab_manager_->ClearPrivateOriginZooms();
  }
}

void OtfHandler::ApplyAlwaysOnPrivacyPreferences(
    CefRefPtr<CefRequestContext> ctx) {
  if (!ctx) return;
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefValue> val = CefValue::Create();
  val->SetBool(true);
  CefString error;
  ctx->SetPreference("enable_do_not_track", val, error);

  CefRefPtr<CefValue> block_third_party = CefValue::Create();
  block_third_party->SetBool(true);
  ctx->SetPreference("profile.block_third_party_cookies",
                     block_third_party, error);
}

}  // namespace otf
