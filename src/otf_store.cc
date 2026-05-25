#include "otf_store.h"

#include <ctime>
#include <filesystem>
#include <sqlite3.h>

#include "otf_utils.h"

namespace otf {

namespace {

std::string GetDatabasePath() {
  std::filesystem::path base = GetAppDataDir();
  if (base.empty()) {
    // GetAppDataDir already tried create_directories; fall back to a temp dir
    // so the store can at least open in degraded environments.
    base = std::filesystem::temp_directory_path() / "otf-browser";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
  }
  return (base / "browser.db").string();
}

std::string NormalizeBookmarkKey(const std::string& url) {
  return NormalizeBookmarkUrl(url);
}

// Previously this kept its own copy of the internal-pages allowlist, which
// drifted from the canonical list in otf_utils.cc — newer pages
// (appmenu, certificate, bookmarkbar, imagepreview) leaked into history.
// Defer to the unified IsInternalUiUrl which is scheme-agnostic so dev
// URLs (http://localhost:3000/...) are also filtered.
bool IsInternalBrowserHistoryUrl(const std::string& url) {
  return otf::IsInternalUiUrl(url);
}

std::string NormalizeOrigin(const std::string& origin) {
  // Strip default ports so https://example.com:443 and https://example.com match.
  if (origin.rfind("http://", 0) == 0) {
    size_t port_start = origin.find(':', 7);
    if (port_start != std::string::npos) {
      std::string port_str = origin.substr(port_start + 1);
      if (port_str == "80") return origin.substr(0, port_start);
    }
  } else if (origin.rfind("https://", 0) == 0) {
    size_t port_start = origin.find(':', 8);
    if (port_start != std::string::npos) {
      std::string port_str = origin.substr(port_start + 1);
      if (port_str == "443") return origin.substr(0, port_start);
    }
  }
  return origin;
}

}  // namespace

OtfStore::OtfStore() : db_(nullptr) {
  Open();
}

OtfStore::~OtfStore() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool OtfStore::IsReady() const {
  return db_ != nullptr;
}

bool OtfStore::Open() {
  if (db_) {
    return true;
  }
  if (sqlite3_open(GetDatabasePath().c_str(), &db_) != SQLITE_OK) {
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return false;
  }
  Exec("PRAGMA foreign_keys = ON;");
  // Zero deleted content on disk so cleared history / bookmarks / downloads
  // can't be recovered by carving the SQLite file. Slight write cost; this
  // database is tiny so the tradeoff is fine.
  Exec("PRAGMA secure_delete = ON;");
  return RunMigrations();
}

bool OtfStore::RunMigrations() {
  bool ok = Exec(
             "CREATE TABLE IF NOT EXISTS schema_migrations ("
             "version INTEGER PRIMARY KEY"
             ");") &&
         Exec(
             "CREATE TABLE IF NOT EXISTS history ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "url TEXT NOT NULL UNIQUE,"
             "title TEXT NOT NULL DEFAULT '',"
             "visit_count INTEGER NOT NULL DEFAULT 0,"
             "last_visit_at INTEGER NOT NULL,"
             "created_at INTEGER NOT NULL"
             ");") &&
         Exec(
             "CREATE TABLE IF NOT EXISTS visits ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "history_id INTEGER NOT NULL,"
             "url TEXT NOT NULL,"
             "title TEXT NOT NULL DEFAULT '',"
             "visited_at INTEGER NOT NULL,"
             "transition TEXT NOT NULL DEFAULT 'link',"
             "FOREIGN KEY(history_id) REFERENCES history(id) ON DELETE CASCADE"
             ");") &&
         Exec(
             "CREATE TABLE IF NOT EXISTS downloads ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "url TEXT NOT NULL,"
             "original_url TEXT NOT NULL DEFAULT '',"
             "target_path TEXT NOT NULL DEFAULT '',"
             "filename TEXT NOT NULL DEFAULT '',"
             "total_bytes INTEGER NOT NULL DEFAULT 0,"
             "received_bytes INTEGER NOT NULL DEFAULT 0,"
             "status TEXT NOT NULL DEFAULT 'pending',"
             "started_at INTEGER NOT NULL,"
             "ended_at INTEGER NOT NULL DEFAULT 0,"
             "mime_type TEXT NOT NULL DEFAULT ''"
             ");") &&
         Exec(
             "CREATE TABLE IF NOT EXISTS bookmarks ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "title TEXT NOT NULL DEFAULT '',"
             "url TEXT NOT NULL UNIQUE,"
             "favicon_url TEXT NOT NULL DEFAULT '',"
             "position INTEGER NOT NULL DEFAULT 0,"
             "created_at INTEGER NOT NULL,"
             "updated_at INTEGER NOT NULL"
             ");") &&
         Exec("CREATE INDEX IF NOT EXISTS idx_history_last_visit ON history(last_visit_at DESC);") &&
         Exec("CREATE INDEX IF NOT EXISTS idx_visits_history_id ON visits(history_id);") &&
         Exec("CREATE INDEX IF NOT EXISTS idx_downloads_started_at ON downloads(started_at DESC);") &&
         Exec("CREATE INDEX IF NOT EXISTS idx_bookmarks_position ON bookmarks(position ASC, updated_at DESC);");

  if (!ok) return false;

  // Migration v1: Add favicon_url to bookmarks if missing
  Exec("ALTER TABLE bookmarks ADD COLUMN favicon_url TEXT NOT NULL DEFAULT '';");
  Exec("INSERT OR IGNORE INTO schema_migrations (version) VALUES (1);");

  // Migration v2: workspaces + workspace_tabs. The default workspace
  // (id=1) is inserted so existing users have a home for their tabs
  // without any explicit migration of tab state.
  bool ws_ok =
      Exec(
          "CREATE TABLE IF NOT EXISTS workspaces ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "name TEXT NOT NULL DEFAULT '',"
          "color TEXT NOT NULL DEFAULT '',"
          "position INTEGER NOT NULL DEFAULT 0,"
          "created_at INTEGER NOT NULL"
          ");") &&
      Exec(
          "CREATE TABLE IF NOT EXISTS workspace_tabs ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "workspace_id INTEGER NOT NULL,"
          "position INTEGER NOT NULL DEFAULT 0,"
          "url TEXT NOT NULL DEFAULT '',"
          "preview_local_path TEXT NOT NULL DEFAULT '',"
          "title TEXT NOT NULL DEFAULT '',"
          "favicon TEXT NOT NULL DEFAULT '',"
          "was_active INTEGER NOT NULL DEFAULT 0,"
          "is_image_preview INTEGER NOT NULL DEFAULT 0,"
          "preview_page INTEGER NOT NULL DEFAULT 0,"
          "FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE"
          ");") &&
      Exec(
          "CREATE TABLE IF NOT EXISTS workspace_state ("
          "key TEXT PRIMARY KEY,"
          "value TEXT NOT NULL DEFAULT ''"
          ");") &&
      Exec("CREATE INDEX IF NOT EXISTS idx_workspace_tabs_ws ON workspace_tabs(workspace_id, position ASC);");
  if (!ws_ok) return false;

  // Seed the default workspace if none exists.
  sqlite3_stmt* count_stmt = nullptr;
  int ws_count = 0;
  if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM workspaces;", -1, &count_stmt, nullptr) == SQLITE_OK) {
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
      ws_count = sqlite3_column_int(count_stmt, 0);
    }
  }
  sqlite3_finalize(count_stmt);
  if (ws_count == 0) {
    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "INSERT INTO workspaces(id, name, color, position, created_at) "
                           "VALUES(1, 'Default', '', 0, ?);",
                           -1, &ins, nullptr) == SQLITE_OK) {
      sqlite3_bind_int64(ins, 1, Now());
      sqlite3_step(ins);
    }
    sqlite3_finalize(ins);
  }

  // Migration v3: persist whether a workspace tab is a dedicated TIFF preview
  // shell and which page it was on when saved.
  Exec("ALTER TABLE workspace_tabs ADD COLUMN is_image_preview INTEGER NOT NULL DEFAULT 0;");
  Exec("ALTER TABLE workspace_tabs ADD COLUMN preview_page INTEGER NOT NULL DEFAULT 0;");
  Exec("INSERT OR IGNORE INTO schema_migrations (version) VALUES (3);");

  // Migration v4: image preview tabs now keep the trusted local file path in a
  // backend-only column instead of overloading the public tab URL.
  Exec("ALTER TABLE workspace_tabs ADD COLUMN preview_local_path TEXT NOT NULL DEFAULT '';");
  Exec("INSERT OR IGNORE INTO schema_migrations (version) VALUES (4);");

  // Migration v5: site permissions table
  bool perm_ok =
      Exec(
          "CREATE TABLE IF NOT EXISTS site_permissions ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "origin TEXT NOT NULL,"
          "permission TEXT NOT NULL,"
          "setting TEXT NOT NULL DEFAULT 'ask',"
          "created_at INTEGER NOT NULL,"
          "updated_at INTEGER NOT NULL,"
          "UNIQUE(origin, permission)"
          ");") &&
      Exec("CREATE INDEX IF NOT EXISTS idx_site_permissions_origin "
           "ON site_permissions(origin);");
  if (!perm_ok) return false;
  Exec("INSERT OR IGNORE INTO schema_migrations (version) VALUES (5);");

  return true;
}

bool OtfStore::Exec(const std::string& sql) const {
  if (!db_) {
    return false;
  }
  char* error = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
  if (error) {
    sqlite3_free(error);
  }
  return rc == SQLITE_OK;
}

int64_t OtfStore::Now() const {
  return static_cast<int64_t>(std::time(nullptr));
}

bool OtfStore::RecordVisit(const std::string& url,
                           const std::string& title,
                           const std::string& transition) {
  if (!db_) {
    return false;
  }

  sqlite3_stmt* upsert = nullptr;
  const char* upsert_sql =
      "INSERT INTO history(url, title, visit_count, last_visit_at, created_at) "
      "VALUES(?, ?, 1, ?, ?) "
      "ON CONFLICT(url) DO UPDATE SET "
      "title=excluded.title, "
      "visit_count=history.visit_count + 1, "
      "last_visit_at=excluded.last_visit_at;";
  if (sqlite3_prepare_v2(db_, upsert_sql, -1, &upsert, nullptr) != SQLITE_OK) {
    return false;
  }

  const int64_t now = Now();
  sqlite3_bind_text(upsert, 1, url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(upsert, 2, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(upsert, 3, now);
  sqlite3_bind_int64(upsert, 4, now);
  const bool upsert_ok = sqlite3_step(upsert) == SQLITE_DONE;
  sqlite3_finalize(upsert);
  if (!upsert_ok) {
    return false;
  }

  sqlite3_stmt* select = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT id FROM history WHERE url = ?;", -1, &select,
                         nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(select, 1, url.c_str(), -1, SQLITE_TRANSIENT);
  int history_id = 0;
  if (sqlite3_step(select) == SQLITE_ROW) {
    history_id = sqlite3_column_int(select, 0);
  }
  sqlite3_finalize(select);
  if (history_id <= 0) {
    return false;
  }

  sqlite3_stmt* visit = nullptr;
  const char* visit_sql =
      "INSERT INTO visits(history_id, url, title, visited_at, transition) "
      "VALUES(?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_, visit_sql, -1, &visit, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int(visit, 1, history_id);
  sqlite3_bind_text(visit, 2, url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(visit, 3, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(visit, 4, now);
  sqlite3_bind_text(visit, 5, transition.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(visit) == SQLITE_DONE;
  sqlite3_finalize(visit);
  return ok;
}

bool OtfStore::UpdateHistoryTitle(const std::string& url, const std::string& title) {
  if (!db_) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "UPDATE history SET title = ? WHERE url = ?;", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, url.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

std::vector<HistoryEntry> OtfStore::GetHistory(int limit) const {
  std::vector<HistoryEntry> out;
  if (!db_) {
    return out;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "SELECT id, url, title, visit_count, last_visit_at, created_at "
          "FROM history ORDER BY last_visit_at DESC LIMIT ?;",
          -1, &stmt, nullptr) != SQLITE_OK) {
    return out;
  }
  sqlite3_bind_int(stmt, 1, limit);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    HistoryEntry item;
    item.id = sqlite3_column_int(stmt, 0);
    item.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (IsInternalBrowserHistoryUrl(item.url)) {
      continue;
    }
    item.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    item.visit_count = sqlite3_column_int(stmt, 3);
    item.last_visit_at = sqlite3_column_int64(stmt, 4);
    item.created_at = sqlite3_column_int64(stmt, 5);
    out.push_back(item);
  }
  sqlite3_finalize(stmt);
  return out;
}

bool OtfStore::DeleteHistoryItem(int id) {
  if (!db_) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM history WHERE id = ?;", -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int(stmt, 1, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::ClearHistory() {
  return Exec("DELETE FROM visits;") && Exec("DELETE FROM history;");
}

bool OtfStore::ClearBookmarks() {
  return Exec("DELETE FROM bookmarks;");
}

bool OtfStore::ClearDownloads() {
  return Exec("DELETE FROM downloads;");
}

int OtfStore::CreateDownload(const std::string& url,
                             const std::string& original_url,
                             const std::string& target_path,
                             const std::string& filename,
                             const std::string& mime_type,
                             const std::string& status) {
  if (!db_) {
    return 0;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO downloads(url, original_url, target_path, filename, total_bytes, "
      "received_bytes, status, started_at, ended_at, mime_type) "
      "VALUES(?, ?, ?, ?, 0, 0, ?, ?, 0, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }
  const int64_t now = Now();
  sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, original_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, target_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, filename.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 6, now);
  sqlite3_bind_text(stmt, 7, mime_type.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  if (!ok) {
    return 0;
  }
  return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool OtfStore::UpdateDownload(const PersistedDownload& download) {
  if (!db_ || download.id <= 0) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "UPDATE downloads SET "
      "url = ?, original_url = ?, target_path = ?, filename = ?, total_bytes = ?, "
      "received_bytes = ?, status = ?, ended_at = ?, mime_type = ? "
      "WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  const bool finished = download.status == "completed" ||
                        download.status == "canceled" ||
                        download.status == "interrupted";
  sqlite3_bind_text(stmt, 1, download.url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, download.original_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, download.target_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, download.filename.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, download.total_bytes);
  sqlite3_bind_int64(stmt, 6, download.received_bytes);
  sqlite3_bind_text(stmt, 7, download.status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 8, download.ended_at > 0 ? download.ended_at
                                                   : (finished ? Now() : 0));
  sqlite3_bind_text(stmt, 9, download.mime_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 10, download.id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

std::vector<PersistedDownload> OtfStore::GetDownloads(int limit) const {
  std::vector<PersistedDownload> out;
  if (!db_) {
    return out;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "SELECT id, url, original_url, target_path, filename, total_bytes, "
          "received_bytes, status, started_at, ended_at, mime_type "
          "FROM downloads ORDER BY started_at DESC LIMIT ?;",
          -1, &stmt, nullptr) != SQLITE_OK) {
    return out;
  }
  sqlite3_bind_int(stmt, 1, limit);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    PersistedDownload item;
    item.id = sqlite3_column_int(stmt, 0);
    item.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    item.original_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    item.target_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    item.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    item.total_bytes = sqlite3_column_int64(stmt, 5);
    item.received_bytes = sqlite3_column_int64(stmt, 6);
    item.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    item.started_at = sqlite3_column_int(stmt, 8);
    item.ended_at = sqlite3_column_int(stmt, 9);
    item.mime_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    out.push_back(item);
  }
  sqlite3_finalize(stmt);
  return out;
}

bool OtfStore::DeleteFinishedDownloads() {
  return Exec(
      "DELETE FROM downloads WHERE status IN ('completed', 'canceled', 'interrupted');");
}

bool OtfStore::AddBookmark(const std::string& url, const std::string& title, const std::string& favicon_url) {
  if (!db_) {
    return false;
  }
  sqlite3_stmt* next = nullptr;
  int position = 0;
  if (sqlite3_prepare_v2(db_, "SELECT COALESCE(MAX(position), 0) + 1 FROM bookmarks;", -1,
                         &next, nullptr) == SQLITE_OK) {
    if (sqlite3_step(next) == SQLITE_ROW) {
      position = sqlite3_column_int(next, 0);
    }
  }
  sqlite3_finalize(next);

  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO bookmarks(title, url, favicon_url, position, created_at, updated_at) "
      "VALUES(?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(url) DO UPDATE SET "
      "title = excluded.title, "
      "favicon_url = COALESCE(NULLIF(excluded.favicon_url, ''), bookmarks.favicon_url), "
      "updated_at = excluded.updated_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  const int64_t now = Now();
  sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
  const std::string canonical_url = NormalizeBookmarkKey(url);
  sqlite3_bind_text(stmt, 2, canonical_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, favicon_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, position);
  sqlite3_bind_int64(stmt, 5, now);
  sqlite3_bind_int64(stmt, 6, now);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::UpdateBookmark(int id, const std::string& url, const std::string& title, const std::string& favicon_url) {
  if (!db_) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "UPDATE bookmarks SET title = ?, url = ?, "
          "favicon_url = COALESCE(NULLIF(?, ''), favicon_url), "
          "updated_at = ? WHERE id = ?;",
          -1,
          &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  const std::string canonical_url = NormalizeBookmarkKey(url);
  sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, canonical_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, favicon_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 4, Now());
  sqlite3_bind_int(stmt, 5, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::RemoveBookmark(int id) {
  if (!db_) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM bookmarks WHERE id = ?;", -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int(stmt, 1, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::RemoveBookmarkByUrl(const std::string& url) {
  if (!db_) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM bookmarks WHERE url = ? OR url = ?;", -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return false;
  }
  const std::string canonical_url = NormalizeBookmarkKey(url);
  sqlite3_bind_text(stmt, 1, canonical_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, url.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::IsBookmarked(const std::string& url) const {
  if (!db_) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT 1 FROM bookmarks WHERE url = ? LIMIT 1;", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    return false;
  }
  const std::string canonical_url = NormalizeBookmarkKey(url);
  sqlite3_bind_text(stmt, 1, canonical_url.c_str(), -1, SQLITE_TRANSIENT);
  bool found = sqlite3_step(stmt) == SQLITE_ROW;
  sqlite3_finalize(stmt);
  if (found) {
    return true;
  }

  if (canonical_url != url) {
    if (sqlite3_prepare_v2(db_, "SELECT 1 FROM bookmarks WHERE url = ? LIMIT 1;", -1, &stmt,
                           nullptr) != SQLITE_OK) {
      return false;
    }
    sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
  }
  return found;
}

bool OtfStore::ClearSitePermissions(const std::string& origin) {
  if (!db_ || origin.empty()) return false;
  const std::string norm = NormalizeOrigin(origin);
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM site_permissions WHERE origin = ?;", -1,
                         &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, norm.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::ClearAllSitePermissions() {
  if (!db_) return false;
  return Exec("DELETE FROM site_permissions;");
}

std::string OtfStore::GetSitePermissionsJson(const std::string& origin) const {
  if (!db_ || origin.empty()) return "{}";
  const std::string norm = NormalizeOrigin(origin);
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT permission, setting FROM site_permissions WHERE origin = ?;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    return "{}";
  }
  sqlite3_bind_text(stmt, 1, norm.c_str(), -1, SQLITE_TRANSIENT);
  std::string json = "{";
  bool first = true;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if (!first) json += ",";
    first = false;
    const char* perm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    json += "\"" + (perm ? std::string(perm) : "") + "\":\""
          + (val ? std::string(val) : "") + "\"";
  }
  sqlite3_finalize(stmt);
  json += "}";
  return json;
}

std::string OtfStore::GetSitePermission(const std::string& origin,
                                        const std::string& permission) const {
  if (!db_ || origin.empty() || permission.empty()) return {};
  const std::lock_guard<std::mutex> lock(db_mutex_);
  const std::string norm = NormalizeOrigin(origin);
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT setting FROM site_permissions "
                         "WHERE origin = ? AND permission = ?;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    return {};
  }
  sqlite3_bind_text(stmt, 1, norm.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, permission.c_str(), -1, SQLITE_TRANSIENT);
  std::string setting;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (val) setting = val;
  }
  sqlite3_finalize(stmt);
  return setting;
}

bool OtfStore::SetSitePermission(const std::string& origin,
                                 const std::string& permission,
                                 const std::string& setting) {
  if (!db_ || origin.empty() || permission.empty()) return false;
  const std::lock_guard<std::mutex> lock(db_mutex_);
  const std::string norm = NormalizeOrigin(origin);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO site_permissions(origin, permission, setting, created_at, updated_at) "
      "VALUES(?, ?, ?, ?, ?) "
      "ON CONFLICT(origin, permission) DO UPDATE SET "
      "setting = excluded.setting, updated_at = excluded.updated_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  const int64_t now = Now();
  sqlite3_bind_text(stmt, 1, norm.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, permission.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, setting.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 4, now);
  sqlite3_bind_int64(stmt, 5, now);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::IsSecureDeleteEnabled() const {
  if (!db_) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "PRAGMA secure_delete;", -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return false;
  }
  bool enabled = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    enabled = sqlite3_column_int(stmt, 0) != 0;
  }
  sqlite3_finalize(stmt);
  return enabled;
}

std::vector<BookmarkEntry> OtfStore::GetBookmarks() const {
  std::vector<BookmarkEntry> out;
  if (!db_) {
    return out;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "SELECT id, title, url, favicon_url, position, created_at, updated_at "
          "FROM bookmarks ORDER BY position ASC, updated_at DESC;",
          -1, &stmt, nullptr) != SQLITE_OK) {
    return out;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    BookmarkEntry item;
    item.id = sqlite3_column_int(stmt, 0);
    item.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    item.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const char* favicon = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    item.favicon_url = favicon ? favicon : "";
    item.position = sqlite3_column_int(stmt, 4);
    item.created_at = sqlite3_column_int64(stmt, 5);
    item.updated_at = sqlite3_column_int64(stmt, 6);
    out.push_back(item);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<Workspace> OtfStore::GetWorkspaces() const {
  std::vector<Workspace> out;
  if (!db_) return out;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "SELECT id, name, color, position, created_at FROM workspaces "
          "ORDER BY position ASC, id ASC;",
          -1, &stmt, nullptr) != SQLITE_OK) {
    return out;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Workspace w;
    w.id = sqlite3_column_int(stmt, 0);
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    w.name = name ? name : "";
    const char* color = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    w.color = color ? color : "";
    w.position = sqlite3_column_int(stmt, 3);
    w.created_at = sqlite3_column_int64(stmt, 4);
    out.push_back(w);
  }
  sqlite3_finalize(stmt);
  return out;
}

int OtfStore::CreateWorkspace(const std::string& name, const std::string& color) {
  if (!db_) return 0;
  sqlite3_stmt* pos_stmt = nullptr;
  int position = 0;
  if (sqlite3_prepare_v2(db_, "SELECT COALESCE(MAX(position), -1) + 1 FROM workspaces;",
                         -1, &pos_stmt, nullptr) == SQLITE_OK) {
    if (sqlite3_step(pos_stmt) == SQLITE_ROW) {
      position = sqlite3_column_int(pos_stmt, 0);
    }
  }
  sqlite3_finalize(pos_stmt);

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "INSERT INTO workspaces(name, color, position, created_at) VALUES(?, ?, ?, ?);",
          -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }
  sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, color.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 3, position);
  sqlite3_bind_int64(stmt, 4, Now());
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  if (!ok) return 0;
  return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool OtfStore::RenameWorkspace(int id, const std::string& name) {
  if (!db_) return false;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "UPDATE workspaces SET name = ? WHERE id = ?;", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::SetWorkspaceColor(int id, const std::string& color) {
  if (!db_) return false;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "UPDATE workspaces SET color = ? WHERE id = ?;", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, color.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::DeleteWorkspace(int id) {
  if (!db_ || id <= 0) return false;
  // Refuse to delete the last remaining workspace — UI/handler should
  // gate this too, but the store is the last line of defense.
  sqlite3_stmt* count_stmt = nullptr;
  int count = 0;
  if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM workspaces;", -1, &count_stmt, nullptr) == SQLITE_OK) {
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(count_stmt, 0);
    }
  }
  sqlite3_finalize(count_stmt);
  if (count <= 1) return false;

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM workspaces WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int(stmt, 1, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool OtfStore::SetActiveWorkspace(int id) {
  if (!db_ || id <= 0) return false;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "INSERT INTO workspace_state(key, value) VALUES('active', ?) "
          "ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
          -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  const std::string v = std::to_string(id);
  sqlite3_bind_text(stmt, 1, v.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

int OtfStore::GetActiveWorkspace() const {
  if (!db_) return 0;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT value FROM workspace_state WHERE key = 'active';",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }
  int id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (v) id = std::atoi(v);
  }
  sqlite3_finalize(stmt);
  return id;
}

bool OtfStore::ReplaceWorkspaceTabs(int workspace_id, const std::vector<WorkspaceTab>& tabs) {
  if (!db_ || workspace_id <= 0) return false;
  if (!Exec("BEGIN IMMEDIATE;")) return false;

  sqlite3_stmt* del = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM workspace_tabs WHERE workspace_id = ?;", -1,
                         &del, nullptr) != SQLITE_OK) {
    Exec("ROLLBACK;");
    return false;
  }
  sqlite3_bind_int(del, 1, workspace_id);
  bool ok = sqlite3_step(del) == SQLITE_DONE;
  sqlite3_finalize(del);
  if (!ok) {
    Exec("ROLLBACK;");
    return false;
  }

  sqlite3_stmt* ins = nullptr;
  const char* sql =
      "INSERT INTO workspace_tabs(workspace_id, position, url, preview_local_path, title, favicon, "
      "was_active, is_image_preview, preview_page) "
      "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &ins, nullptr) != SQLITE_OK) {
    Exec("ROLLBACK;");
    return false;
  }
  int position = 0;
  for (const auto& t : tabs) {
    sqlite3_reset(ins);
    sqlite3_bind_int(ins, 1, workspace_id);
    sqlite3_bind_int(ins, 2, position++);
    sqlite3_bind_text(ins, 3, t.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 4, t.preview_local_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 5, t.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 6, t.favicon.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 7, t.was_active ? 1 : 0);
    sqlite3_bind_int(ins, 8, t.is_image_preview ? 1 : 0);
    sqlite3_bind_int(ins, 9, t.preview_page < 0 ? 0 : t.preview_page);
    if (sqlite3_step(ins) != SQLITE_DONE) {
      ok = false;
      break;
    }
  }
  sqlite3_finalize(ins);
  if (!ok) {
    Exec("ROLLBACK;");
    return false;
  }
  return Exec("COMMIT;");
}

std::vector<WorkspaceTab> OtfStore::GetWorkspaceTabs(int workspace_id) const {
  std::vector<WorkspaceTab> out;
  if (!db_ || workspace_id <= 0) return out;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "SELECT id, workspace_id, position, url, preview_local_path, title, favicon, was_active, "
          "is_image_preview, preview_page "
          "FROM workspace_tabs WHERE workspace_id = ? ORDER BY position ASC;",
          -1, &stmt, nullptr) != SQLITE_OK) {
    return out;
  }
  sqlite3_bind_int(stmt, 1, workspace_id);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    WorkspaceTab t;
    t.id = sqlite3_column_int(stmt, 0);
    t.workspace_id = sqlite3_column_int(stmt, 1);
    t.position = sqlite3_column_int(stmt, 2);
    const char* url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    t.url = url ? url : "";
    const char* preview_local_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    t.preview_local_path = preview_local_path ? preview_local_path : "";
    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    t.title = title ? title : "";
    const char* fav = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    t.favicon = fav ? fav : "";
    t.was_active = sqlite3_column_int(stmt, 7) != 0;
    t.is_image_preview = sqlite3_column_int(stmt, 8) != 0;
    t.preview_page = sqlite3_column_int(stmt, 9);
    out.push_back(t);
  }
  sqlite3_finalize(stmt);
  return out;
}

}  // namespace otf
