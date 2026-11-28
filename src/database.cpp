#include "database.h"

#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace endpaper {

// ── Helpers ───────────────────────────────────────────────────────────────

static void check(int rc, sqlite3* db, const char* context) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string msg = std::string(context) + ": " +
                          (db ? sqlite3_errmsg(db) : "unknown error");
        throw std::runtime_error(msg);
    }
}

static std::string col_text(sqlite3_stmt* stmt, int col) {
    auto* p = sqlite3_column_text(stmt, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

/// RAII wrapper for sqlite3_stmt
struct StmtGuard {
    sqlite3_stmt* stmt = nullptr;
    ~StmtGuard() { if (stmt) sqlite3_finalize(stmt); }
};

// ── Constructor / Destructor ──────────────────────────────────────────────

Database::Database(const std::filesystem::path& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    check(rc, db_, "sqlite3_open");

    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void Database::exec(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("SQL exec: " + msg);
    }
}

// ── Schema ────────────────────────────────────────────────────────────────

void Database::init() {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    exec(R"(
        CREATE TABLE IF NOT EXISTS books (
            id         INTEGER PRIMARY KEY,
            title      TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS chapters (
            id           INTEGER PRIMARY KEY,
            book_id      INTEGER NOT NULL,
            subject      TEXT,
            sender       TEXT,
            content_html TEXT,
            sort_order   INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY(book_id) REFERENCES books(id) ON DELETE CASCADE
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS rss_feeds (
            id         INTEGER PRIMARY KEY,
            title      TEXT NOT NULL,
            url        TEXT NOT NULL UNIQUE,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS folder_cache (
            collection_id INTEGER PRIMARY KEY,
            parent_id     INTEGER NOT NULL DEFAULT -1,
            name          TEXT NOT NULL,
            full_path     TEXT NOT NULL
        );
    )");

    // Migrate legacy folder_cache schemas lacking parent_id
    {
        StmtGuard sg;
        check(sqlite3_prepare_v2(db_, "PRAGMA table_info(folder_cache);", -1, &sg.stmt, nullptr), db_, "prepare");
        bool has_parent_id = false;
        while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
            if (col_text(sg.stmt, 1) == "parent_id") {
                has_parent_id = true;
                break;
            }
        }
        if (!has_parent_id) {
            exec("ALTER TABLE folder_cache ADD COLUMN parent_id INTEGER NOT NULL DEFAULT -1;");
        }
    }

    exec(R"(
        CREATE TABLE IF NOT EXISTS folder_prefs (
            collection_id INTEGER PRIMARY KEY,
            hidden        INTEGER NOT NULL DEFAULT 0,
            pinned        INTEGER NOT NULL DEFAULT 0
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS category_cache (
            id           INTEGER PRIMARY KEY,
            title        TEXT NOT NULL,
            unread_count INTEGER NOT NULL DEFAULT 0
        );
    )");
}

// ── Book CRUD ─────────────────────────────────────────────────────────────

int64_t Database::create_book(const std::string& title) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "INSERT INTO books (title) VALUES (?);";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_text(sg.stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    check(sqlite3_step(sg.stmt), db_, "step");
    return sqlite3_last_insert_rowid(db_);
}

std::vector<Book> Database::list_books() {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql =
        "SELECT b.id, b.title, b.created_at, COUNT(c.id) "
        "FROM books b LEFT JOIN chapters c ON c.book_id = b.id "
        "GROUP BY b.id ORDER BY b.id;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

    std::vector<Book> books;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        Book b;
        b.id = sqlite3_column_int64(sg.stmt, 0);
        b.title = col_text(sg.stmt, 1);
        b.created_at = col_text(sg.stmt, 2);
        b.chapter_count = sqlite3_column_int(sg.stmt, 3);
        books.push_back(std::move(b));
    }
    return books;
}

void Database::delete_book(int64_t book_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    exec("BEGIN TRANSACTION;");
    try {
        const char* sql_ch = "DELETE FROM chapters WHERE book_id = ?;";
        StmtGuard sg1;
        check(sqlite3_prepare_v2(db_, sql_ch, -1, &sg1.stmt, nullptr),
              db_, "prepare");
        sqlite3_bind_int64(sg1.stmt, 1, book_id);
        check(sqlite3_step(sg1.stmt), db_, "step");

        const char* sql_bk = "DELETE FROM books WHERE id = ?;";
        StmtGuard sg2;
        check(sqlite3_prepare_v2(db_, sql_bk, -1, &sg2.stmt, nullptr),
              db_, "prepare");
        sqlite3_bind_int64(sg2.stmt, 1, book_id);
        check(sqlite3_step(sg2.stmt), db_, "step");

        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

// ── Chapter CRUD ──────────────────────────────────────────────────────────

void Database::add_chapter(int64_t book_id, const std::string& subject,
                           const std::string& sender,
                           const std::string& content_html, int sort_order) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql =
        "INSERT INTO chapters (book_id, subject, sender, content_html, "
        "sort_order) VALUES (?, ?, ?, ?, ?);";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, book_id);
    sqlite3_bind_text(sg.stmt, 2, subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(sg.stmt, 3, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(sg.stmt, 4, content_html.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(sg.stmt, 5, sort_order);
    check(sqlite3_step(sg.stmt), db_, "step");
}

std::vector<Chapter> Database::get_chapters(int64_t book_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql =
        "SELECT id, book_id, subject, sender, content_html, sort_order "
        "FROM chapters WHERE book_id = ? ORDER BY sort_order, id;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, book_id);

    std::vector<Chapter> chapters;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        Chapter c;
        c.id = sqlite3_column_int64(sg.stmt, 0);
        c.book_id = sqlite3_column_int64(sg.stmt, 1);
        c.subject = col_text(sg.stmt, 2);
        c.sender = col_text(sg.stmt, 3);
        c.content_html = col_text(sg.stmt, 4);
        c.sort_order = sqlite3_column_int(sg.stmt, 5);
        chapters.push_back(std::move(c));
    }
    return chapters;
}

void Database::update_sort_order(int64_t chapter_id, int new_order) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "UPDATE chapters SET sort_order = ? WHERE id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int(sg.stmt, 1, new_order);
    sqlite3_bind_int64(sg.stmt, 2, chapter_id);
    check(sqlite3_step(sg.stmt), db_, "step");
}

void Database::reorder_chapters(const std::vector<int64_t>& chapter_ids) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (chapter_ids.empty()) return;

    exec("BEGIN TRANSACTION;");
    try {
        for (size_t i = 0; i < chapter_ids.size(); ++i) {
            update_sort_order(chapter_ids[i], static_cast<int>(i) + 1);
        }
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

bool Database::move_chapter(int64_t book_id, int64_t chapter_id, int delta) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    auto chapters = get_chapters(book_id);
    int idx = -1;
    for (int i = 0; i < static_cast<int>(chapters.size()); ++i) {
        if (chapters[i].id == chapter_id) {
            idx = i;
            break;
        }
    }
    int target = idx + delta;
    if (idx < 0 || target < 0 || target >= static_cast<int>(chapters.size()) || delta == 0) {
        return false;
    }

    std::swap(chapters[idx], chapters[target]);
    exec("BEGIN TRANSACTION;");
    try {
        for (size_t i = 0; i < chapters.size(); ++i) {
            update_sort_order(chapters[i].id, static_cast<int>(i) + 1);
        }
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
    return true;
}

void Database::delete_chapter(int64_t chapter_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "DELETE FROM chapters WHERE id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, chapter_id);
    check(sqlite3_step(sg.stmt), db_, "step");
}

void Database::delete_all_chapters(int64_t book_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "DELETE FROM chapters WHERE book_id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, book_id);
    check(sqlite3_step(sg.stmt), db_, "step");
}

void Database::reverse_chapter_order(int64_t book_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    auto chapters = get_chapters(book_id);
    if (chapters.size() < 2) return;

    exec("BEGIN TRANSACTION;");
    try {
        const size_t n = chapters.size();
        for (size_t i = 0; i < n; ++i) {
            update_sort_order(chapters[i].id, static_cast<int>(n - i));
        }
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

void Database::renumber_chapters(int64_t book_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    auto chapters = get_chapters(book_id);
    if (chapters.empty()) return;

    exec("BEGIN TRANSACTION;");
    try {
        for (size_t i = 0; i < chapters.size(); ++i) {
            update_sort_order(chapters[i].id, static_cast<int>(i) + 1);
        }
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

void Database::auto_sort_chapters(int64_t book_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    auto chapters = get_chapters(book_id);
    if (chapters.size() < 2) return;

    struct ChapterNum {
        Chapter ch;
        int num = -1;
    };
    std::vector<ChapterNum> sorted_list;
    sorted_list.reserve(chapters.size());

    auto first_number = [](const std::string& s) -> int {
        size_t i = 0;
        while (i < s.size() && !std::isdigit(static_cast<unsigned char>(s[i]))) i++;
        if (i == s.size()) return -1;
        size_t j = i;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) j++;
        try {
            return std::stoi(s.substr(i, j - i));
        } catch (...) {
            return -1;
        }
    };

    for (auto& ch : chapters) {
        int num = first_number(ch.subject);
        sorted_list.push_back(ChapterNum{std::move(ch), num});
    }

    // Sort: numbered items first (in order), then by title
    std::sort(sorted_list.begin(), sorted_list.end(),
              [](const ChapterNum& a, const ChapterNum& b) {
                  if (a.num != b.num) {
                      if (a.num == -1) return false;
                      if (b.num == -1) return true;
                      return a.num < b.num;
                  }
                  return a.ch.subject < b.ch.subject;
              });

    exec("BEGIN TRANSACTION;");
    try {
        for (size_t i = 0; i < sorted_list.size(); ++i) {
            update_sort_order(sorted_list[i].ch.id, static_cast<int>(i) + 1);
        }
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

int Database::max_sort_order(int64_t book_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql =
        "SELECT COALESCE(MAX(sort_order), 0) FROM chapters WHERE book_id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, book_id);
    if (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        return sqlite3_column_int(sg.stmt, 0);
    }
    return 0;
}

int Database::chapter_count(int64_t book_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "SELECT COUNT(*) FROM chapters WHERE book_id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, book_id);
    if (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        return sqlite3_column_int(sg.stmt, 0);
    }
    return 0;
}

// ── RSS operations ───────────────────────────────────────────────────────

bool Database::add_rss_feed(const std::string& title, const std::string& url) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "INSERT OR IGNORE INTO rss_feeds (title, url) VALUES (?, ?);";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_text(sg.stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(sg.stmt, 2, url.c_str(), -1, SQLITE_TRANSIENT);
    check(sqlite3_step(sg.stmt), db_, "step");
    return sqlite3_changes(db_) > 0;
}

std::vector<RssFeed> Database::list_rss_feeds() {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "SELECT id, title, url, created_at FROM rss_feeds ORDER BY title;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

    std::vector<RssFeed> feeds;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        RssFeed f;
        f.id = sqlite3_column_int64(sg.stmt, 0);
        f.title = col_text(sg.stmt, 1);
        f.url = col_text(sg.stmt, 2);
        f.created_at = col_text(sg.stmt, 3);
        feeds.push_back(std::move(f));
    }
    return feeds;
}

void Database::delete_rss_feed(int64_t feed_id) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "DELETE FROM rss_feeds WHERE id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, feed_id);
    check(sqlite3_step(sg.stmt), db_, "step");
}

// ── Cache operations ────────────────────────────────────────────────

void Database::save_folder_cache(const std::vector<FolderInfo>& folders) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    exec("BEGIN TRANSACTION;");
    try {
        exec("DELETE FROM folder_cache;");
        const char* sql =
            "INSERT INTO folder_cache (collection_id, parent_id, name, full_path) VALUES (?, ?, ?, ?);";
        StmtGuard sg;
        check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

        for (const auto& f : folders) {
            sqlite3_bind_int64(sg.stmt, 1, f.collection_id);
            sqlite3_bind_int64(sg.stmt, 2, f.parent_id);
            sqlite3_bind_text(sg.stmt, 3, f.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(sg.stmt, 4, f.full_path.c_str(), -1, SQLITE_TRANSIENT);
            check(sqlite3_step(sg.stmt), db_, "step");
            sqlite3_reset(sg.stmt);
        }
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

std::vector<FolderInfo> Database::load_folder_cache() {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql =
        "SELECT collection_id, parent_id, name, full_path FROM folder_cache "
        "ORDER BY full_path COLLATE NOCASE;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

    std::vector<FolderInfo> folders;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        FolderInfo f;
        f.collection_id = sqlite3_column_int64(sg.stmt, 0);
        f.parent_id = sqlite3_column_int64(sg.stmt, 1);
        f.name = col_text(sg.stmt, 2);
        f.full_path = col_text(sg.stmt, 3);
        folders.push_back(std::move(f));
    }
    return folders;
}

FolderPrefs Database::load_folder_prefs() {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "SELECT collection_id, hidden, pinned FROM folder_prefs;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

    FolderPrefs prefs;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        int64_t cid = sqlite3_column_int64(sg.stmt, 0);
        if (sqlite3_column_int(sg.stmt, 1) != 0) prefs.hidden.insert(cid);
        if (sqlite3_column_int(sg.stmt, 2) != 0) prefs.pinned.insert(cid);
    }
    return prefs;
}

void Database::set_folder_hidden(int64_t collection_id, bool hidden) {
    set_folders_hidden({collection_id}, hidden);
}

void Database::set_folders_hidden(const std::vector<int64_t>& collection_ids, bool hidden) {
    if (collection_ids.empty()) return;
    std::lock_guard<std::recursive_mutex> lock(mu_);
    exec("BEGIN TRANSACTION;");
    try {
        const char* sql = hidden
            ? "INSERT INTO folder_prefs (collection_id, hidden, pinned) VALUES (?, 1, 0) "
              "ON CONFLICT(collection_id) DO UPDATE SET hidden = 1, pinned = 0;"
            : "INSERT INTO folder_prefs (collection_id, hidden, pinned) VALUES (?, 0, 0) "
              "ON CONFLICT(collection_id) DO UPDATE SET hidden = 0;";
        StmtGuard sg;
        check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
        for (int64_t cid : collection_ids) {
            sqlite3_bind_int64(sg.stmt, 1, cid);
            check(sqlite3_step(sg.stmt), db_, "step");
            sqlite3_reset(sg.stmt);
        }
        exec("DELETE FROM folder_prefs WHERE hidden = 0 AND pinned = 0;");
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

void Database::replace_hidden_folders(const std::unordered_set<int64_t>& hidden_ids) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    exec("BEGIN TRANSACTION;");
    try {
        exec("UPDATE folder_prefs SET hidden = 0;");
        const char* sql =
            "INSERT INTO folder_prefs (collection_id, hidden, pinned) VALUES (?, 1, 0) "
            "ON CONFLICT(collection_id) DO UPDATE SET hidden = 1, pinned = 0;";
        StmtGuard sg;
        check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
        for (int64_t cid : hidden_ids) {
            sqlite3_bind_int64(sg.stmt, 1, cid);
            check(sqlite3_step(sg.stmt), db_, "step");
            sqlite3_reset(sg.stmt);
        }
        exec("DELETE FROM folder_prefs WHERE hidden = 0 AND pinned = 0;");
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

void Database::set_folder_pinned(int64_t collection_id, bool pinned) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    exec("BEGIN TRANSACTION;");
    try {
        const char* sql =
            "INSERT INTO folder_prefs (collection_id, hidden, pinned) VALUES (?, 0, ?) "
            "ON CONFLICT(collection_id) DO UPDATE SET pinned = excluded.pinned;";
        StmtGuard sg;
        check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
        sqlite3_bind_int64(sg.stmt, 1, collection_id);
        sqlite3_bind_int(sg.stmt, 2, pinned ? 1 : 0);
        check(sqlite3_step(sg.stmt), db_, "step");
        exec("DELETE FROM folder_prefs WHERE hidden = 0 AND pinned = 0;");
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

void Database::save_category_cache(const std::vector<MinifluxCategory>& categories) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    exec("BEGIN TRANSACTION;");
    try {
        exec("DELETE FROM category_cache;");
        const char* sql = "INSERT INTO category_cache (id, title, unread_count) VALUES (?, ?, ?);";
        StmtGuard sg;
        check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

        for (const auto& c : categories) {
            if (c.id == -1) continue;
            sqlite3_bind_int64(sg.stmt, 1, c.id);
            sqlite3_bind_text(sg.stmt, 2, c.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(sg.stmt, 3, c.unread_count);
            check(sqlite3_step(sg.stmt), db_, "step");
            sqlite3_reset(sg.stmt);
        }
        exec("COMMIT;");
    } catch (...) {
        try { exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

std::vector<MinifluxCategory> Database::load_category_cache() {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    const char* sql = "SELECT id, title, unread_count FROM category_cache ORDER BY title;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

    std::vector<MinifluxCategory> categories;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        MinifluxCategory c;
        c.id = sqlite3_column_int64(sg.stmt, 0);
        c.title = col_text(sg.stmt, 1);
        c.unread_count = sqlite3_column_int(sg.stmt, 2);
        categories.push_back(std::move(c));
    }
    return categories;
}

} // namespace endpaper
