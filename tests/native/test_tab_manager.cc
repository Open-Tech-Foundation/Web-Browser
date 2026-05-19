#include "otf_browser_shell.h"

#undef NDEBUG
#include <cassert>
#include <vector>

namespace {

void TestTabOrdering() {
  otf::TabManager tabs;
  const int first = tabs.AddTab(nullptr);
  const int second = tabs.AddTab(nullptr);
  const int child = tabs.AddTab(nullptr, first);

  assert(first == 1);
  assert(second == 2);
  assert(child == 3);
  assert((tabs.GetAllTabIds() == std::vector<int>{1, 3, 2}));
}

void TestStateDefaultsAndMutation() {
  otf::TabManager tabs;
  const int id = tabs.AddTab(nullptr);
  assert(tabs.GetTitle(id) == "New Tab");
  assert(tabs.GetUrl(id).empty());
  assert(tabs.GetZoomPercent(id) == 100);
  assert(!tabs.HasSslError(id));
  assert(tabs.IsImagePreviewInfoVisible(id));
  assert(tabs.GetImagePreviewMode(id) == otf::ImagePreviewMode::kNone);

  tabs.SetUrl(id, "https://example.com");
  tabs.SetTitle(id, "Example");
  tabs.SetFindText(id, "needle");
  tabs.SetFindCase(id, true);
  tabs.SetFindVisible(id, true);
  tabs.SetFindCount(id, 3);
  tabs.SetFindActive(id, 2);
  tabs.SetZoomPercent(id, 125);
  tabs.SetFaviconUrl(id, "https://example.com/favicon.ico");
  tabs.SetSslError(id, true);
  tabs.SetSslErrorUrl(id, "https://bad.example");
  tabs.SetWorkspaceId(id, 7);
  tabs.SetImagePreviewDimensions(id, -1, 24);
  tabs.SetImagePreviewInfoVisible(id, false);
  tabs.SetImagePreviewMode(id, otf::ImagePreviewMode::kDedicated);

  assert(tabs.GetUrl(id) == "https://example.com");
  assert(tabs.GetTitle(id) == "Example");
  assert(tabs.GetFindText(id) == "needle");
  assert(tabs.GetFindCase(id));
  assert(tabs.IsFindVisible(id));
  assert(tabs.GetFindCount(id) == 3);
  assert(tabs.GetFindActive(id) == 2);
  assert(tabs.GetZoomPercent(id) == 125);
  assert(tabs.GetFaviconUrl(id) == "https://example.com/favicon.ico");
  assert(tabs.HasSslError(id));
  assert(tabs.GetSslErrorUrl(id) == "https://bad.example");
  assert(tabs.GetWorkspaceId(id) == 7);
  assert(tabs.GetImagePreviewWidth(id) == 0);
  assert(tabs.GetImagePreviewHeight(id) == 24);
  assert(!tabs.IsImagePreviewInfoVisible(id));
  assert(tabs.GetImagePreviewMode(id) == otf::ImagePreviewMode::kDedicated);

  tabs.ClearFindState(id);
  assert(tabs.GetFindText(id).empty());
  assert(!tabs.GetFindCase(id));
  assert(!tabs.IsFindVisible(id));
  assert(tabs.GetFindCount(id) == 0);
  assert(tabs.GetFindActive(id) == 0);
}

void TestRemoveTabClearsState() {
  otf::TabManager tabs;
  const int id = tabs.AddTab(nullptr);
  tabs.SetUrl(id, "https://example.com");
  tabs.SetTitle(id, "Example");
  tabs.SetWorkspaceId(id, 1);
  tabs.SetFindText(id, "needle");
  tabs.SetImagePreviewMode(id, otf::ImagePreviewMode::kInline);
  tabs.RemoveTab(id);

  assert(tabs.GetAllTabIds().empty());
  assert(tabs.GetUrl(id).empty());
  assert(tabs.GetTitle(id) == "New Tab");
  assert(tabs.GetWorkspaceId(id) == 0);
  assert(tabs.GetFindText(id).empty());
  assert(tabs.GetImagePreviewMode(id) == otf::ImagePreviewMode::kNone);
}

void TestWorkspaceOrdering() {
  otf::TabManager tabs;
  const int a = tabs.AddTab(nullptr);
  const int b = tabs.AddTab(nullptr);
  const int c = tabs.AddTab(nullptr);
  const int other = tabs.AddTab(nullptr);
  tabs.SetWorkspaceId(a, 1);
  tabs.SetWorkspaceId(b, 1);
  tabs.SetWorkspaceId(c, 1);
  tabs.SetWorkspaceId(other, 2);

  assert((tabs.GetTabIdsForWorkspace(1) == std::vector<int>{a, b, c}));
  tabs.SetWorkspaceTabOrder(1, {c, a, b});
  assert((tabs.GetTabIdsForWorkspace(1) == std::vector<int>{c, a, b}));
  assert((tabs.GetAllTabIds() == std::vector<int>{c, a, b, other}));
}

}  // namespace

int main() {
  TestTabOrdering();
  TestStateDefaultsAndMutation();
  TestRemoveTabClearsState();
  TestWorkspaceOrdering();
  return 0;
}
