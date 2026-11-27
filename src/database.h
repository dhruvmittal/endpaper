#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
#include "akonadi_fetcher.h"
#include "miniflux_fetcher.h"

struct sqlite3;

namespace endpaper {

struct Book {
    int64_t id;
    std::string title;
    std::string created_at;
    int chapter_count = 0;
};

struct RssFeed {
    int64_t id;
    std::string title;
    std::string url;
    std::string created_at;
};

struct Chapter {
    int64_t id;
    int64_t book_id;
    std::string subject;
    std::string sender;
    std::string content_html;
    int sort_order;
};

struct FolderPrefs {
    std::unordered_set<int64_t> hidden;
    std::unordered_set<int64_t> pinned;
};

/// RAII SQLite database wrapper for the Endpaper library.
class Database {
public:
    /// Open (or create) the database at the given path.
    explicit Database(const std::filesystem::path& db_path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /// Create tables if they don't exist.
    void init();

    // ── Book operations ──────────────────────────────────────────────────
    [[nodiscard]] int64_t create_book(const std::string& title);
    [[nodiscard]] std::vector<Book> list_books();
    void delete_book(int64_t book_id);

    // ── Chapter operations ───────────────────────────────────────────────
    void add_chapter(int64_t book_id, const std::string& subject,
                     const std::string& sender,
                     const std::string& content_html, int sort_order);
    [[nodiscard]] std::vector<Chapter> get_chapters(int64_t book_id);
    void update_sort_order(int64_t chapter_id, int new_order);
    bool move_chapter(int64_t book_id, int64_t chapter_id, int delta);
    void delete_chapter(int64_t chapter_id);
    void delete_all_chapters(int64_t book_id);
    void reverse_chapter_order(int64_t book_id);
    void renumber_chapters(int64_t book_id);
    void auto_sort_chapters(int64_t book_id);
    [[nodiscard]] int max_sort_order(int64_t book_id);
    [[nodiscard]] int chapter_count(int64_t book_id);

    // ── RSS operations ───────────────────────────────────────────────────
    bool add_rss_feed(const std::string& title, const std::string& url);
    [[nodiscard]] std::vector<RssFeed> list_rss_feeds();
    void delete_rss_feed(int64_t feed_id);

    // ── Cache & folder preference operations ─────────────────────────────
    void save_folder_cache(const std::vector<FolderInfo>& folders);
    [[nodiscard]] std::vector<FolderInfo> load_folder_cache();

    [[nodiscard]] FolderPrefs load_folder_prefs();
    void set_folder_hidden(int64_t collection_id, bool hidden);
    void set_folders_hidden(const std::vector<int64_t>& collection_ids, bool hidden);
    void replace_hidden_folders(const std::unordered_set<int64_t>& hidden_ids);
    void set_folder_pinned(int64_t collection_id, bool pinned);

    void save_category_cache(const std::vector<MinifluxCategory>& categories);
    [[nodiscard]] std::vector<MinifluxCategory> load_category_cache();

private:
    void exec(const std::string& sql);
    mutable std::recursive_mutex mu_;
    sqlite3* db_ = nullptr;
};

} // namespace endpaper
