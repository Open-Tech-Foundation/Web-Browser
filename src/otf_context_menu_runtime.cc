#include "otf_context_menu_runtime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "include/cef_command_ids.h"
#include "include/cef_menu_model.h"

namespace otf {
namespace {

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

}  // namespace otf
