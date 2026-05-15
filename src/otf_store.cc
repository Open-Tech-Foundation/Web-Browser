#include "otf_store.h"

#include <ctime>
#include <filesystem>
#include <sqlite3.h>

#include "otf_utils.h"

namespace otf {

namespace {

std::string GetDatabasePath() {
  std::filesystem::path base;
  const std::string home = GetHomeDir();
  if (!home.empty()) {
    base = std::filesystem::path(home) / ".otf-browser";
  } else {
    base = std::filesystem::temp_directory_path() / "otf-browser";
  }
  std::filesystem::create_directories(base);
  return (base / "browser.db").string();
}

std::string NormalizeBookmarkKey(const std::string& url) {
  return NormalizeBookmarkUrl(url);
}

bool IsInternalBrowserHistoryUrl(const std::string& url) {
  if (url.rfind("browser://", 0) == 0) {
    return true;
  }
  const char* kInternalPages[] = {
      "/newtab.html",     "/settings.html",  "/findbar.html",
      "/downloads.html",   "/downloadsbar.html", "/zoombar.html",
      "/history.html",    "/bookmarks.html",  "/fingerprints.html",
      "/pdfviewer.html"};
  for (const char* suffix : kInternalPages) {
    if (url.find(suffix) != std::string::npos) {
      return true;
    }
  }
  return false;
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

}  // namespace otf
