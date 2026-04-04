#include "database.h"

#include <sqlite3.h>
#include <algorithm>
#include <regex>
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

/// RAII wrapper for sqlite3_stmt
struct StmtGuard {
    sqlite3_stmt* stmt = nullptr;
    ~StmtGuard() { if (stmt) sqlite3_finalize(stmt); }
};

// ── Constructor / Destructor ──────────────────────────────────────────────

Database::Database(const std::filesystem::path& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    check(rc, db_, "sqlite3_open");

    // Enable WAL mode for better concurrency
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void Database::exec(const std::string& sql) {
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
            name          TEXT NOT NULL,
            full_path    TEXT NOT NULL
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
    const char* sql = "INSERT INTO books (title) VALUES (?);";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_text(sg.stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    check(sqlite3_step(sg.stmt), db_, "step");
    return sqlite3_last_insert_rowid(db_);
}

std::vector<Book> Database::list_books() {
    const char* sql = "SELECT id, title, created_at FROM books ORDER BY id;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

    std::vector<Book> books;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        Book b;
        b.id = sqlite3_column_int64(sg.stmt, 0);
        b.title = reinterpret_cast<const char*>(
            sqlite3_column_text(sg.stmt, 1));
        b.created_at = reinterpret_cast<const char*>(
            sqlite3_column_text(sg.stmt, 2));
        books.push_back(std::move(b));
    }
    return books;
}

void Database::delete_book(int64_t book_id) {
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
        exec("ROLLBACK;");
        throw;
    }
}

// ── Chapter CRUD ──────────────────────────────────────────────────────────

void Database::add_chapter(int64_t book_id, const std::string& subject,
                           const std::string& sender,
                           const std::string& content_html, int sort_order) {
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
    const char* sql =
        "SELECT id, book_id, subject, sender, content_html, sort_order "
        "FROM chapters WHERE book_id = ? ORDER BY sort_order;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, book_id);

    std::vector<Chapter> chapters;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        Chapter c;
        c.id = sqlite3_column_int64(sg.stmt, 0);
        c.book_id = sqlite3_column_int64(sg.stmt, 1);
        auto col_text = [&](int col) -> std::string {
            auto* p = sqlite3_column_text(sg.stmt, col);
            return p ? reinterpret_cast<const char*>(p) : "";
        };
        c.subject = col_text(2);
        c.sender = col_text(3);
        c.content_html = col_text(4);
        c.sort_order = sqlite3_column_int(sg.stmt, 5);
        chapters.push_back(std::move(c));
    }
    return chapters;
}

void Database::update_sort_order(int64_t chapter_id, int new_order) {
    const char* sql = "UPDATE chapters SET sort_order = ? WHERE id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int(sg.stmt, 1, new_order);
    sqlite3_bind_int64(sg.stmt, 2, chapter_id);
    check(sqlite3_step(sg.stmt), db_, "step");
}

void Database::delete_chapter(int64_t chapter_id) {
    const char* sql = "DELETE FROM chapters WHERE id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, chapter_id);
    check(sqlite3_step(sg.stmt), db_, "step");
}

void Database::delete_all_chapters(int64_t book_id) {
    const char* sql = "DELETE FROM chapters WHERE book_id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, book_id);
    check(sqlite3_step(sg.stmt), db_, "step");
}

void Database::reverse_chapter_order(int64_t book_id) {
    auto chapters = get_chapters(book_id);
    if (chapters.size() < 2) return;

    // Collect all sort orders
    std::vector<int> orders;
    for (const auto& ch : chapters) {
        orders.push_back(ch.sort_order);
    }
    
    // Sort orders should already be sorted ascending if get_chapters works correctly.
    // We want to assign the same set of orders but to chapters in reverse.
    
    exec("BEGIN TRANSACTION;");
    try {
        for (size_t i = 0; i < chapters.size(); ++i) {
            // Give chapter i the order that was originally at the opposite end
            update_sort_order(chapters[i].id, orders[orders.size() - 1 - i]);
        }
        exec("COMMIT;");
        renumber_chapters(book_id);
    } catch (...) {
        exec("ROLLBACK;");
        throw;
    }
}

void Database::renumber_chapters(int64_t book_id) {
    auto chapters = get_chapters(book_id);
    if (chapters.empty()) return;

    exec("BEGIN TRANSACTION;");
    try {
        for (size_t i = 0; i < chapters.size(); ++i) {
            update_sort_order(chapters[i].id, static_cast<int>(i) + 1);
        }
        exec("COMMIT;");
    } catch (...) {
        exec("ROLLBACK;");
        throw;
    }
}

void Database::auto_sort_chapters(int64_t book_id) {
    auto chapters = get_chapters(book_id);
    if (chapters.size() < 2) return;

    struct ChapterNum {
        Chapter ch;
        int num = -1;
    };
    std::vector<ChapterNum> sorted_list;
    std::regex num_re(R"(\d+)");

    for (const auto& ch : chapters) {
        ChapterNum cn{ch, -1};
        std::smatch m;
        if (std::regex_search(ch.subject, m, num_re)) {
            try {
                cn.num = std::stoi(m[0].str());
            } catch (...) {
                cn.num = -1;
            }
        }
        sorted_list.push_back(std::move(cn));
    }

    // Sort: numbered items first (in order), then by title
    std::sort(sorted_list.begin(), sorted_list.end(),
              [](const ChapterNum& a, const ChapterNum& b) {
                  if (a.num != b.num) {
                      if (a.num == -1) return false; // a goes later
                      if (b.num == -1) return true;  // a goes first
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
        exec("ROLLBACK;");
        throw;
    }
}

int Database::max_sort_order(int64_t book_id) {
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

// ── RSS operations ───────────────────────────────────────────────────────

void Database::add_rss_feed(const std::string& title, const std::string& url) {
    const char* sql = "INSERT OR IGNORE INTO rss_feeds (title, url) VALUES (?, ?);";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_text(sg.stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(sg.stmt, 2, url.c_str(), -1, SQLITE_TRANSIENT);
    check(sqlite3_step(sg.stmt), db_, "step");
}

std::vector<RssFeed> Database::list_rss_feeds() {
    const char* sql = "SELECT id, title, url, created_at FROM rss_feeds ORDER BY title;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

    std::vector<RssFeed> feeds;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        RssFeed f;
        f.id = sqlite3_column_int64(sg.stmt, 0);
        f.title = reinterpret_cast<const char*>(sqlite3_column_text(sg.stmt, 1));
        f.url = reinterpret_cast<const char*>(sqlite3_column_text(sg.stmt, 2));
        f.created_at = reinterpret_cast<const char*>(sqlite3_column_text(sg.stmt, 3));
        feeds.push_back(std::move(f));
    }
    return feeds;
}

void Database::delete_rss_feed(int64_t feed_id) {
    const char* sql = "DELETE FROM rss_feeds WHERE id = ?;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
    sqlite3_bind_int64(sg.stmt, 1, feed_id);
    check(sqlite3_step(sg.stmt), db_, "step");
}

// ── Cache operations ────────────────────────────────────────────────

void Database::save_folder_cache(const std::vector<FolderInfo>& folders) {
    exec("DELETE FROM folder_cache;");
    const char* sql = "INSERT INTO folder_cache (collection_id, name, full_path) VALUES (?, ?, ?);";
    for (const auto& f : folders) {
        StmtGuard sg;
        check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
        sqlite3_bind_int64(sg.stmt, 1, f.collection_id);
        sqlite3_bind_text(sg.stmt, 2, f.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(sg.stmt, 3, f.full_path.c_str(), -1, SQLITE_TRANSIENT);
        check(sqlite3_step(sg.stmt), db_, "step");
    }
}

std::vector<FolderInfo> Database::load_folder_cache() {
    const char* sql = "SELECT collection_id, name, full_path FROM folder_cache ORDER BY full_path;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

    std::vector<FolderInfo> folders;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        FolderInfo f;
        f.collection_id = sqlite3_column_int64(sg.stmt, 0);
        f.name = reinterpret_cast<const char*>(sqlite3_column_text(sg.stmt, 1));
        f.full_path = reinterpret_cast<const char*>(sqlite3_column_text(sg.stmt, 2));
        folders.push_back(std::move(f));
    }
    return folders;
}

void Database::save_category_cache(const std::vector<MinifluxCategory>& categories) {
    exec("DELETE FROM category_cache;");
    const char* sql = "INSERT INTO category_cache (id, title, unread_count) VALUES (?, ?, ?);";
    for (const auto& c : categories) {
        if (c.id == -1) continue; // Skip "All Categories" as it is added at runtime
        StmtGuard sg;
        check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");
        sqlite3_bind_int64(sg.stmt, 1, c.id);
        sqlite3_bind_text(sg.stmt, 2, c.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(sg.stmt, 3, c.unread_count);
        check(sqlite3_step(sg.stmt), db_, "step");
    }
}

std::vector<MinifluxCategory> Database::load_category_cache() {
    const char* sql = "SELECT id, title, unread_count FROM category_cache ORDER BY title;";
    StmtGuard sg;
    check(sqlite3_prepare_v2(db_, sql, -1, &sg.stmt, nullptr), db_, "prepare");

    std::vector<MinifluxCategory> categories;
    while (sqlite3_step(sg.stmt) == SQLITE_ROW) {
        MinifluxCategory c;
        c.id = sqlite3_column_int64(sg.stmt, 0);
        c.title = reinterpret_cast<const char*>(sqlite3_column_text(sg.stmt, 1));
        c.unread_count = sqlite3_column_int(sg.stmt, 2);
        categories.push_back(std::move(c));
    }
    return categories;
}

} // namespace endpaper
