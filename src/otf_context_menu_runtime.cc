#include "otf_context_menu_runtime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>

#include "include/cef_browser.h"
#include "include/cef_command_ids.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_menu_model.h"
#include "otf_app.h"
#include "otf_handler.h"
#include "otf_split_runtime.h"
#include "otf_utils.h"

#if !defined(_WIN32) && !defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace otf {
namespace {

constexpr int kMenuIdOpenInNewTab = 10001;
constexpr int kMenuIdSearchSelection = 10002;
constexpr int kMenuIdPreviewImage = 10003;
constexpr int kMenuIdTabClose = 10004;
constexpr int kMenuIdTabCloseOthers = 10005;
constexpr int kMenuIdTabNew = 10006;
constexpr int kMenuIdTabMute = 10007;
constexpr int kMenuIdTabUnmute = 10008;
constexpr int kMenuIdCopyEmail = 10009;
constexpr int kMenuIdTabNewPrivate = 10010;
constexpr int kMenuIdTabPin = 10011;
constexpr int kMenuIdTabUnpin = 10012;
constexpr int kMenuIdPreviewDoc = 10013;
constexpr int kMenuIdPasteGo = 10014;
constexpr int kMenuIdTabAddToSplit = 10015;
constexpr int kMenuIdReload = 10016;

constexpr std::array<int, 4> kBlockedContextMenuCommandIds = {
    IDC_VIEW_SOURCE,
    IDC_CONTENT_CONTEXT_VIEWFRAMESOURCE,
    IDC_CONTENT_CONTEXT_OPENLINKNEWTAB,
    IDC_CONTENT_CONTEXT_OPENLINKNEWWINDOW,
};

std::string TrimWhitespaceCopy(const std::string& value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

std::string NormalizeMenuLabel(std::string label) {
  std::string normalized;
  normalized.reserve(label.size());
  bool previous_space = false;
  for (char c : label) {
    if (c == '&') {
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!previous_space) {
        normalized.push_back(' ');
        previous_space = true;
      }
      continue;
    }
    normalized.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    previous_space = false;
  }
  return TrimWhitespaceCopy(normalized);
}

bool IsSourceViewMenuItem(int command_id, const std::string& label) {
  if (std::find(kBlockedContextMenuCommandIds.begin(),
                kBlockedContextMenuCommandIds.end(),
                command_id) != kBlockedContextMenuCommandIds.end()) {
    return true;
  }

  const std::string normalized_label = NormalizeMenuLabel(label);
  return normalized_label.find("view") != std::string::npos &&
         normalized_label.find("source") != std::string::npos;
}

void RemoveCommandEverywhere(CefRefPtr<CefMenuModel> model, int command_id) {
  if (!model) {
    return;
  }

  for (int index = static_cast<int>(model->GetCount()) - 1; index >= 0; --index) {
    CefRefPtr<CefMenuModel> sub_menu =
        model->GetSubMenuAt(static_cast<size_t>(index));
    if (sub_menu) {
      RemoveCommandEverywhere(sub_menu, command_id);
    }
    if (model->GetCommandIdAt(static_cast<size_t>(index)) == command_id) {
      model->RemoveAt(static_cast<size_t>(index));
    }
  }
}

void RemoveLabeledSourceItemsEverywhere(CefRefPtr<CefMenuModel> model) {
  if (!model) {
    return;
  }

  for (int index = static_cast<int>(model->GetCount()) - 1; index >= 0; --index) {
    CefRefPtr<CefMenuModel> sub_menu =
        model->GetSubMenuAt(static_cast<size_t>(index));
    if (sub_menu) {
      RemoveLabeledSourceItemsEverywhere(sub_menu);
    }

    const std::string label =
        model->GetLabelAt(static_cast<size_t>(index)).ToString();
    const int command_id = model->GetCommandIdAt(static_cast<size_t>(index));
    if (IsSourceViewMenuItem(command_id, label)) {
      model->RemoveAt(static_cast<size_t>(index));
    }
  }
}

#if !defined(_WIN32) && !defined(__APPLE__)
bool HasCommand(const char* command) {
  if (!command || command[0] == '\0') {
    return false;
  }

  if (strchr(command, '/') != nullptr) {
    struct stat buffer;
    return (stat(command, &buffer) == 0) &&
           S_ISREG(buffer.st_mode) &&
           (buffer.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
  }

  const char* path_env = getenv("PATH");
  if (!path_env) {
    return false;
  }

  std::string path_str(path_env);
  size_t start = 0;
  size_t end = path_str.find(':');

  while (end != std::string::npos) {
    std::string dir = path_str.substr(start, end - start);
    if (!dir.empty()) {
      std::string full_path = dir + "/" + command;
      struct stat buffer;
      if (stat(full_path.c_str(), &buffer) == 0 &&
          S_ISREG(buffer.st_mode) &&
          (buffer.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        return true;
      }
    }
    start = end + 1;
    end = path_str.find(':', start);
  }

  std::string dir = path_str.substr(start);
  if (!dir.empty()) {
    std::string full_path = dir + "/" + command;
    struct stat buffer;
    if (stat(full_path.c_str(), &buffer) == 0 &&
        S_ISREG(buffer.st_mode) &&
        (buffer.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
      return true;
    }
  }

  return false;
}
#endif

std::string BuildTabPropertyEvent(int tab_id,
                                  const std::string& key,
                                  bool value) {
  return JsonObjectBuilder()
      .AddInt("id", tab_id)
      .AddString("key", key)
      .AddBool("value", value)
      .Build();
}

void WriteToClipboard(const std::string& text) {
#if defined(_WIN32)
  if (OpenClipboard(nullptr)) {
    EmptyClipboard();
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(WCHAR));
    if (hg) {
      WCHAR* buffer = static_cast<WCHAR*>(GlobalLock(hg));
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buffer, len);
      GlobalUnlock(hg);
      SetClipboardData(CF_UNICODETEXT, hg);
    }
    CloseClipboard();
  }
#elif defined(__APPLE__)
  FILE* pipe = popen("pbcopy", "w");
  if (pipe) {
    fputs(text.c_str(), pipe);
    pclose(pipe);
  }
#else
  FILE* pipe = nullptr;
  if (HasCommand("wl-copy")) {
    pipe = popen("wl-copy", "w");
  } else if (HasCommand("xclip")) {
    pipe = popen("xclip -selection clipboard", "w");
  } else if (HasCommand("xsel")) {
    pipe = popen("xsel --clipboard --input", "w");
  }
  if (pipe) {
    fputs(text.c_str(), pipe);
    pclose(pipe);
  }
#endif
}

bool HandleTabContextCommand(OtfHandler* handler,
                             int command_id,
                             int tab_id) {
  OtfApp* app = OtfApp::GetInstance();
  if (!app || !handler->tab_manager_) {
    return true;
  }

  if (command_id == kMenuIdTabClose) {
    handler->CloseTabAndNotify(tab_id);
    return true;
  }
  if (command_id == kMenuIdTabCloseOthers) {
    std::vector<int> ids =
        handler->tab_manager_->GetTabIdsForWorkspace(handler->active_workspace_id_);
    for (int id : ids) {
      if (id != tab_id && !handler->tab_manager_->IsPinned(id)) {
        handler->CloseTabAndNotify(id);
      }
    }
    return true;
  }
  if (command_id == kMenuIdTabMute || command_id == kMenuIdTabUnmute) {
    CefRefPtr<CefBrowser> browser = handler->tab_manager_->GetBrowser(tab_id);
    if (!browser) {
      return true;
    }
    const bool muted = command_id == kMenuIdTabMute;
    browser->GetHost()->SetAudioMuted(muted);
    handler->tab_manager_->SetMuted(tab_id, muted);
    handler->SendEvent(BuildTabPropertyEvent(tab_id, "muted", muted));
    return true;
  }
  if (command_id == kMenuIdTabPin || command_id == kMenuIdTabUnpin) {
    const bool pinned = command_id == kMenuIdTabPin;
    handler->tab_manager_->SetPinned(tab_id, pinned);
    handler->SendEvent(BuildTabPropertyEvent(tab_id, "pinned", pinned));
    handler->PersistWorkspaceForTab(tab_id);
    return true;
  }
  if (command_id == kMenuIdTabAddToSplit) {
    auto state = handler->GetSplitViewState(handler->active_workspace_id_);
    if (state.enabled && !handler->IsSplitTab(tab_id)) {
      const bool left_is_placeholder =
          IsSplitPlaceholderTab(handler->tab_manager_, state.left_tab_id);
      const bool right_is_placeholder =
          IsSplitPlaceholderTab(handler->tab_manager_, state.right_tab_id);
      const bool replace_right =
          right_is_placeholder ? true :
          left_is_placeholder ? false :
          state.active_tab_id != state.right_tab_id;
      const int replaced_tab_id =
          replace_right ? state.right_tab_id : state.left_tab_id;
      const int next_left = replace_right ? state.left_tab_id : tab_id;
      const int next_right = replace_right ? tab_id : state.right_tab_id;
      app->OpenSplitView(next_left, next_right, tab_id);
      handler->SetSplitViewTabs(handler->active_workspace_id_, next_left,
                                next_right, tab_id);
      app->ActivateSplitPane(tab_id, true);
      if (IsSplitPlaceholderTab(handler->tab_manager_, replaced_tab_id)) {
        handler->CloseTabAndNotify(replaced_tab_id);
      }
      handler->PersistWorkspaceForTab(tab_id);
      handler->NotifySplitStateChanged(handler->active_workspace_id_);
    }
    return true;
  }
  return true;
}

bool HandlePasteAndGo(CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    return true;
  }
#if defined(__APPLE__)
  const int kModifier = EVENTFLAG_COMMAND_DOWN;
#else
  const int kModifier = EVENTFLAG_CONTROL_DOWN;
#endif
  CefKeyEvent ev;
  ev.type = KEYEVENT_RAWKEYDOWN;
  ev.modifiers = kModifier;
  ev.windows_key_code = 86;
  browser->GetHost()->SendKeyEvent(ev);
  ev.type = KEYEVENT_CHAR;
  ev.modifiers = kModifier;
  ev.windows_key_code = 22;
  browser->GetHost()->SendKeyEvent(ev);
  ev.type = KEYEVENT_KEYUP;
  ev.modifiers = kModifier;
  ev.windows_key_code = 86;
  browser->GetHost()->SendKeyEvent(ev);
  browser->GetMainFrame()->ExecuteJavaScript(
      "setTimeout(function(){"
      "  var el=document.activeElement;"
      "  if(el)el.dispatchEvent(new KeyboardEvent('keydown',"
      "    {key:'Enter',code:'Enter',bubbles:true,cancelable:true}));"
      "},30);",
      browser->GetMainFrame()->GetURL(), 0);
  return true;
}

}  // namespace

bool IsBlockedContextMenuCommand(int command_id) {
  return std::find(kBlockedContextMenuCommandIds.begin(),
                   kBlockedContextMenuCommandIds.end(),
                   command_id) != kBlockedContextMenuCommandIds.end();
}

void SanitizeContextMenu(CefRefPtr<CefMenuModel> model) {
  for (int command_id : kBlockedContextMenuCommandIds) {
    RemoveCommandEverywhere(model, command_id);
  }
  RemoveLabeledSourceItemsEverywhere(model);
}

bool HandleContextMenuCommand(OtfHandler* handler,
                              CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefContextMenuParams> params,
                              int command_id) {
  if (!handler || !params) {
    return false;
  }

  if (IsBlockedContextMenuCommand(command_id)) {
    return true;
  }

  if (command_id == kMenuIdTabClose ||
      command_id == kMenuIdTabCloseOthers ||
      command_id == kMenuIdTabMute ||
      command_id == kMenuIdTabUnmute ||
      command_id == kMenuIdTabPin ||
      command_id == kMenuIdTabUnpin ||
      command_id == kMenuIdTabAddToSplit) {
    const std::string link_url = params->GetLinkUrl().ToString();
    if (link_url.rfind("tab-context-menu:", 0) == 0) {
      const auto tab_id_opt = ParseIntStrict(link_url.substr(17));
      if (!tab_id_opt) {
        return true;
      }
      return HandleTabContextCommand(handler, command_id, *tab_id_opt);
    }
  }

  if (command_id == kMenuIdTabNew) {
    if (OtfApp* app = OtfApp::GetInstance()) {
      const int new_id = app->CreateTab("browser://newtab", -1);
      handler->NotifyNewTab(new_id, -1);
      app->SwitchTab(new_id);
      handler->PersistWorkspaceForTab(new_id);
      return true;
    }
  }

  if (command_id == kMenuIdTabNewPrivate) {
    if (OtfApp* app = OtfApp::GetInstance()) {
      const int new_id = app->CreateTab("browser://newtab", -1, true);
      handler->NotifyNewTab(new_id, -1);
      app->SwitchTab(new_id);
      return true;
    }
  }

  if (command_id == kMenuIdOpenInNewTab) {
    const std::string url = params->GetLinkUrl().ToString();
    OtfApp* app = OtfApp::GetInstance();
    if (!app || !handler->tab_manager_) {
      return false;
    }
    const int parent_id = handler->tab_manager_->GetId(browser);
    const int new_id =
        app->CreateTab(url, parent_id, handler->tab_manager_->IsPrivate(parent_id));
    if (url.rfind("browser://", 0) == 0) {
      handler->tab_manager_->SetSchemeUrl(new_id, url);
    }
    handler->NotifyNewTab(new_id, parent_id);
    app->FocusCurrentTabContent();
    return true;
  }

  if (command_id == kMenuIdPreviewImage) {
    std::string image_url = params->GetSourceUrl().ToString();
    if (image_url.empty()) {
      image_url = params->GetLinkUrl().ToString();
    }
    if (!image_url.empty()) {
      if (OtfApp* app = OtfApp::GetInstance()) {
        const int tab_id = app->GetCurrentTabId();
        if (handler->tab_manager_) {
          handler->tab_manager_->SetSchemeUrl(tab_id, "");
          handler->tab_manager_->SetImagePreviewMode(tab_id,
                                                     ImagePreviewMode::kInline);
        }
        handler->SetImagePreviewUrlForTab(tab_id, image_url);
        app->ShowImagePreviewOverlay();
        handler->ScheduleImagePreviewPushForTab(tab_id);
      }
    }
    return true;
  }

  if (command_id == kMenuIdPreviewDoc) {
    const std::string doc_url = params->GetLinkUrl().ToString();
    if (!doc_url.empty()) {
      if (OtfApp* app = OtfApp::GetInstance()) {
        const int tab_id = app->GetCurrentTabId();
        handler->ResetDocPreviewFetchStateForTab(tab_id);
        if (handler->tab_manager_) {
          handler->tab_manager_->SetSchemeUrl(tab_id, "");
          handler->tab_manager_->SetDocPreviewMode(tab_id,
                                                   DocPreviewMode::kInline);
        }
        handler->SetDocPreviewUrlForTab(tab_id, doc_url);
        app->ShowDocPreviewOverlay();
        handler->ScheduleDocPreviewPushForTab(tab_id);
        handler->ScheduleDocPreviewFetchForTab(tab_id, doc_url);
      }
    }
    return true;
  }

  if (command_id == kMenuIdSearchSelection) {
    OtfApp* app = OtfApp::GetInstance();
    if (!app || !handler->tab_manager_) {
      return false;
    }

    const std::string selection_text =
        TrimWhitespaceCopy(params->GetSelectionText().ToString());
    const std::optional<std::string> search_engine_id =
        otf::GetCurrentSearchEngineId();
    if (selection_text.empty() || !search_engine_id.has_value()) {
      return false;
    }

    const std::string search_url =
        otf::BuildSearchUrl(*search_engine_id, selection_text);
    if (search_url.empty()) {
      return false;
    }

    const int parent_id = handler->tab_manager_->GetId(browser);
    const int new_id = app->CreateTab(
        search_url, parent_id, handler->tab_manager_->IsPrivate(parent_id));
    handler->NotifyNewTab(new_id, parent_id);
    app->SwitchTab(new_id);
    return true;
  }

  if (command_id == IDC_CONTENT_CONTEXT_COPYLINKLOCATION) {
    WriteToClipboard(
        StripTrackingParamsFromUrl(params->GetLinkUrl().ToString()));
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->ShowToast("copy", "Link copied");
    }
    return true;
  }

  if (command_id == kMenuIdPasteGo) {
    return HandlePasteAndGo(browser);
  }

  if (command_id == kMenuIdCopyEmail) {
    std::string link_url = params->GetLinkUrl().ToString();
    if (link_url.rfind("mailto:", 0) == 0) {
      std::string email = link_url.substr(7);
      size_t qpos = email.find('?');
      if (qpos != std::string::npos) {
        email = email.substr(0, qpos);
      }
      WriteToClipboard(email);
    }
    if (OtfApp* app = OtfApp::GetInstance()) {
      app->ShowToast("copy", "Email copied");
    }
    return true;
  }

  if (command_id == kMenuIdReload) {
    browser->Reload();
    return true;
  }

  return false;
}

}  // namespace otf
