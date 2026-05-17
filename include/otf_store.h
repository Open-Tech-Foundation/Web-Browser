#ifndef OTF_BROWSER_STORE_H_
#define OTF_BROWSER_STORE_H_

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace otf {

struct HistoryEntry {
  int id = 0;
  std::string url;
  std::string title;
  int visit_count = 0;
  int64_t last_visit_at = 0;
  int64_t created_at = 0;
};

struct PersistedDownload {
  int id = 0;
  std::string url;
  std::string original_url;
  std::string target_path;
  std::string filename;
  std::string mime_type;
  int64_t total_bytes = 0;
  int64_t received_bytes = 0;
  std::string status;
  int started_at = 0;
  int ended_at = 0;
};

struct BookmarkEntry {
  int id = 0;
  std::string title;
  std::string url;
  std::string favicon_url;
  int position = 0;
  int64_t created_at = 0;
  int64_t updated_at = 0;
};

class OtfStore {
 public:
  OtfStore();
  ~OtfStore();

  bool IsReady() const;

  bool RecordVisit(const std::string& url,
                   const std::string& title,
                   const std::string& transition);
  bool UpdateHistoryTitle(const std::string& url, const std::string& title);
  std::vector<HistoryEntry> GetHistory(int limit = 200) const;
  bool DeleteHistoryItem(int id);
  bool ClearHistory();
  bool ClearBookmarks();

  int CreateDownload(const std::string& url,
                     const std::string& original_url,
                     const std::string& target_path,
                     const std::string& filename,
                     const std::string& mime_type,
                     const std::string& status);
  bool UpdateDownload(const PersistedDownload& download);
  std::vector<PersistedDownload> GetDownloads(int limit = 200) const;
  bool ClearDownloads();
  bool DeleteFinishedDownloads();

  bool AddBookmark(const std::string& url, const std::string& title, const std::string& favicon_url = "");
  bool UpdateBookmark(int id, const std::string& url, const std::string& title, const std::string& favicon_url = "");
  bool RemoveBookmark(int id);
  bool RemoveBookmarkByUrl(const std::string& url);
  bool IsBookmarked(const std::string& url) const;
  std::vector<BookmarkEntry> GetBookmarks() const;

  // Test-facing: returns whether secure_delete is on for this connection.
  // SQLite pragmas are per-connection, not stored in the file, so the only
  // way to verify our Open() set it is to query the same connection.
  bool IsSecureDeleteEnabled() const;

 private:
  bool Open();
  bool RunMigrations();
  bool Exec(const std::string& sql) const;
  int64_t Now() const;

  sqlite3* db_;
};

}  // namespace otf

#endif  // OTF_BROWSER_STORE_H_
