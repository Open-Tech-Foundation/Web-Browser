#include "otf_store.h"
#include "otf_utils.h"

#undef NDEBUG
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace {
static void SetEnv(const char* name, const char* value) {
#if defined(_WIN32)
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}
static void UnsetEnv(const char* name) {
#if defined(_WIN32)
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}
} // namespace

namespace fs = std::filesystem;

namespace {

void ResetTempHome(const std::string& name) {
  const fs::path temp_home = fs::temp_directory_path() / name;
  fs::remove_all(temp_home);
  fs::create_directories(temp_home);
  SetEnv("OTF_TEST_HOME", temp_home.string().c_str());
  UnsetEnv("OTF_DEV_MODE");
}

void TestHistoryPersistenceAndFiltering() {
  ResetTempHome("otf-store-history-test");
  otf::OtfStore store;
  assert(store.IsReady());
  assert(store.IsSecureDeleteEnabled());

  assert(store.RecordVisit("https://example.com", "Example", "link"));
  assert(store.RecordVisit("https://example.com", "Example 2", "reload"));
  assert(store.RecordVisit("browser://settings", "Settings", "link"));
  assert(store.RecordVisit("http://localhost:3000/appmenu.html", "App", "link"));
  assert(store.RecordVisit("http://localhost:3000/blockedpopup.html", "Popup", "link"));
  assert(store.RecordVisit("http://localhost:3000/downloadrequest.html", "Download", "link"));
  assert(store.UpdateHistoryTitle("https://example.com", "Example Final"));

  const auto history = store.GetHistory();
  assert(history.size() == 1);
  assert(history[0].url == "https://example.com");
  assert(history[0].title == "Example Final");
  assert(history[0].visit_count == 2);

  const int second_workspace = store.CreateWorkspace("History WS");
  assert(second_workspace > 1);
  assert(store.RecordVisit("https://example.com", "Workspace Example", "link",
                           second_workspace));
  assert(store.UpdateHistoryTitle("https://example.com", "Workspace Final",
                                  second_workspace));
  const auto second_history = store.GetHistory(200, second_workspace);
  assert(second_history.size() == 1);
  assert(second_history[0].workspace_id == second_workspace);
  assert(second_history[0].url == "https://example.com");
  assert(second_history[0].title == "Workspace Final");
  assert(second_history[0].visit_count == 1);
  assert(store.GetHistory()[0].title == "Example Final");
  assert(store.ClearHistory(second_workspace));
  assert(store.GetHistory(200, second_workspace).empty());
  assert(store.GetHistory().size() == 1);

  assert(store.DeleteHistoryItem(history[0].id));
  assert(store.GetHistory().empty());
}

void TestDownloadsLifecycle() {
  ResetTempHome("otf-store-downloads-test");
  otf::OtfStore store;
  assert(store.IsReady());

  const int id = store.CreateDownload("https://example.com/file.zip",
                                      "https://example.com/file.zip",
                                      "/tmp/file.zip",
                                      "file.zip",
                                      "application/zip",
                                      "starting");
  assert(id > 0);

  otf::PersistedDownload item;
  item.id = id;
  item.url = "https://example.com/file.zip";
  item.original_url = "https://example.com/file.zip";
  item.target_path = "/tmp/file.zip";
  item.filename = "file.zip";
  item.mime_type = "application/zip";
  item.total_bytes = 2048;
  item.received_bytes = 1024;
  item.status = "in_progress";
  assert(store.UpdateDownload(item));

  auto downloads = store.GetDownloads();
  assert(downloads.size() == 1);
  assert(downloads[0].received_bytes == 1024);
  assert(downloads[0].ended_at == 0);

  item.received_bytes = 2048;
  item.status = "completed";
  assert(store.UpdateDownload(item));
  downloads = store.GetDownloads();
  assert(downloads.size() == 1);
  assert(downloads[0].status == "completed");
  assert(downloads[0].ended_at > 0);

  assert(store.DeleteFinishedDownloads());
  assert(store.GetDownloads().empty());
}

void TestBookmarksLifecycle() {
  ResetTempHome("otf-store-bookmarks-test");
  otf::OtfStore store;
  assert(store.IsReady());

  assert(store.AddBookmark("https://example.com/path/", "Example", "fav-a"));
  assert(store.IsBookmarked("https://example.com/path"));
  auto bookmarks = store.GetBookmarks();
  assert(bookmarks.size() == 1);
  assert(bookmarks[0].url == "https://example.com/path");
  assert(bookmarks[0].favicon_url == "fav-a");

  assert(store.AddBookmark("https://example.com/path", "Renamed", ""));
  bookmarks = store.GetBookmarks();
  assert(bookmarks.size() == 1);
  assert(bookmarks[0].title == "Renamed");
  assert(bookmarks[0].favicon_url == "fav-a");

  assert(store.UpdateBookmark(bookmarks[0].id,
                              "https://example.com/other/",
                              "Other",
                              "fav-b"));
  assert(store.IsBookmarked("https://example.com/other"));
  assert(store.RemoveBookmarkByUrl("https://example.com/other/"));
  assert(store.GetBookmarks().empty());
}

void TestWorkspaceLifecycleAndTabs() {
  ResetTempHome("otf-store-workspaces-test");
  otf::OtfStore store;
  assert(store.IsReady());

  auto workspaces = store.GetWorkspaces();
  assert(workspaces.size() == 1);
  assert(workspaces[0].id == 1);
  assert(!workspaces[0].uuid.empty());
  assert(workspaces[0].name == "Default");
  assert(store.GetActiveWorkspace() == 0);

  const int work_id = store.CreateWorkspace("Research", "#123456");
  assert(work_id > 1);
  assert(store.RenameWorkspace(work_id, "Docs"));
  assert(store.SetWorkspaceColor(work_id, "#abcdef"));
  assert(store.SetActiveWorkspace(work_id));
  assert(store.GetActiveWorkspace() == work_id);
  assert(store.SetWorkspaceOriginZoom(work_id, "https://example.com", 125));
  auto zooms = store.GetWorkspaceOriginZooms(work_id);
  assert(zooms.size() == 1);
  assert(zooms["https://example.com"] == 125);
  assert(store.SetWorkspaceOriginZoom(work_id, "https://example.com", 100));
  assert(store.GetWorkspaceOriginZooms(work_id).empty());
  assert(store.SetWorkspaceOriginZoom(work_id, "https://example.com", 999));
  zooms = store.GetWorkspaceOriginZooms(work_id);
  assert(zooms["https://example.com"] == 500);

  workspaces = store.GetWorkspaces();
  assert(workspaces.size() == 2);
  std::set<std::string> uuids;
  for (const auto& workspace : workspaces) {
    assert(!workspace.uuid.empty());
    assert(uuids.insert(workspace.uuid).second);
  }

  std::vector<otf::WorkspaceTab> tabs;
  otf::WorkspaceTab first;
  first.workspace_id = work_id;
  first.url = "https://example.com";
  first.title = "Example";
  first.favicon = "fav";
  first.was_active = true;
  first.pinned = true;
  tabs.push_back(first);

  otf::WorkspaceTab second;
  second.workspace_id = work_id;
  second.url = "https://example.com/image.tiff";
  second.preview_local_path = "/tmp/image.tiff";
  second.title = "Image";
  second.is_image_preview = true;
  second.preview_page = 2;
  tabs.push_back(second);

  assert(store.ReplaceWorkspaceTabs(work_id, tabs));
  const auto restored = store.GetWorkspaceTabs(work_id);
  assert(restored.size() == 2);
  assert(restored[0].position == 0);
  assert(restored[0].was_active);
  assert(restored[0].pinned);
  assert(restored[1].position == 1);
  assert(restored[1].is_image_preview);
  assert(!restored[1].pinned);
  assert(restored[1].preview_local_path == "/tmp/image.tiff");
  assert(restored[1].preview_page == 2);

  assert(store.DeleteWorkspace(work_id));
  assert(store.GetWorkspaceTabs(work_id).empty());
  assert(!store.DeleteWorkspace(1));
}

}  // namespace

int main() {
  TestHistoryPersistenceAndFiltering();
  TestDownloadsLifecycle();
  TestBookmarksLifecycle();
  TestWorkspaceLifecycleAndTabs();
  return 0;
}
