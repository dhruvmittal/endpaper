#include "akonadi_fetcher.h"
#include "database.h"
#include <sqlite3.h>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using namespace endpaper;

#define TEST(name)                                                 \
    do {                                                           \
        std::cout << "  TEST: " << #name << "... ";                \
        name();                                                    \
        std::cout << "PASS\n";                                     \
    } while (0)

static const fs::path TEST_DB = fs::temp_directory_path() / "test_books.db";

void setup() {
    if (fs::exists(TEST_DB)) fs::remove(TEST_DB);
}

void teardown() {
    if (fs::exists(TEST_DB)) fs::remove(TEST_DB);
}

void test_renumbering() {
    Database db(TEST_DB);
    db.init();

    int64_t book_id = db.create_book("Test Book");

    // Add chapters with non-continuous orders
    db.add_chapter(book_id, "Ch 1", "A", "<p>1</p>", 10);
    db.add_chapter(book_id, "Ch 2", "B", "<p>2</p>", 20);
    db.add_chapter(book_id, "Ch 3", "C", "<p>3</p>", 30);

    auto chapters = db.get_chapters(book_id);
    assert(chapters.size() == 3);
    assert(chapters[0].sort_order == 10);

    // Renumber
    db.renumber_chapters(book_id);

    chapters = db.get_chapters(book_id);
    assert(chapters[0].sort_order == 1);
    assert(chapters[1].sort_order == 2);
    assert(chapters[2].sort_order == 3);

    // Add another chapter and renumber
    db.add_chapter(book_id, "Ch 4", "D", "<p>4</p>", 100);
    db.renumber_chapters(book_id);
    chapters = db.get_chapters(book_id);
    assert(chapters.size() == 4);
    assert(chapters[3].sort_order == 4);

    // Delete a chapter and renumber
    db.delete_chapter(chapters[1].id); // Delete Ch 2
    db.renumber_chapters(book_id);
    chapters = db.get_chapters(book_id);
    assert(chapters.size() == 3);
    assert(chapters[0].subject == "Ch 1");
    assert(chapters[1].subject == "Ch 3");
    assert(chapters[0].sort_order == 1);
    assert(chapters[1].sort_order == 2);
    assert(chapters[2].sort_order == 3);
}

void test_reversal() {
    Database db(TEST_DB);
    db.init();

    int64_t book_id = db.create_book("Reverse Book");
    db.add_chapter(book_id, "A", "A", "A", 1);
    db.add_chapter(book_id, "B", "B", "B", 2);
    db.add_chapter(book_id, "C", "C", "C", 3);

    db.reverse_chapter_order(book_id);

    auto chapters = db.get_chapters(book_id);
    assert(chapters.size() == 3);
    assert(chapters[0].subject == "C");
    assert(chapters[1].subject == "B");
    assert(chapters[2].subject == "A");
    
    // Check if renumbering was applied (should be 1, 2, 3)
    assert(chapters[0].sort_order == 1);
    assert(chapters[1].sort_order == 2);
    assert(chapters[2].sort_order == 3);
}

void test_auto_sort_and_clear_and_count() {
    Database db(TEST_DB);
    db.init();

    int64_t book_id = db.create_book("Sort Book");
    assert(db.chapter_count(book_id) == 0);

    db.add_chapter(book_id, "Part 10: Ten", "A", "<p>10</p>", 1);
    db.add_chapter(book_id, "Part 2: Two", "A", "<p>2</p>", 2);
    db.add_chapter(book_id, "Part 1: One", "A", "<p>1</p>", 3);
    db.add_chapter(book_id, "Zebra Appendix", "A", "<p>z</p>", 4);
    assert(db.chapter_count(book_id) == 4);

    db.auto_sort_chapters(book_id);
    auto chapters = db.get_chapters(book_id);
    assert(chapters.size() == 4);
    assert(chapters[0].subject == "Part 1: One" && chapters[0].sort_order == 1);
    assert(chapters[1].subject == "Part 2: Two" && chapters[1].sort_order == 2);
    assert(chapters[2].subject == "Part 10: Ten" && chapters[2].sort_order == 3);
    assert(chapters[3].subject == "Zebra Appendix" && chapters[3].sort_order == 4);

    db.delete_all_chapters(book_id);
    assert(db.chapter_count(book_id) == 0);
    assert(db.get_chapters(book_id).empty());
}

void test_list_books_chapter_count_and_move_chapter() {
    Database db(TEST_DB);
    db.init();

    int64_t b1 = db.create_book("First Book");
    int64_t b2 = db.create_book("Empty Book");
    db.add_chapter(b1, "A", "Author", "<p>A</p>", 1);
    db.add_chapter(b1, "B", "Author", "<p>B</p>", 2);
    db.add_chapter(b1, "C", "Author", "<p>C</p>", 3);

    auto books = db.list_books();
    int found = 0;
    for (const auto& b : books) {
        if (b.id == b1) { assert(b.chapter_count == 3); ++found; }
        if (b.id == b2) { assert(b.chapter_count == 0); ++found; }
    }
    assert(found == 2);

    auto chs = db.get_chapters(b1);
    assert(chs.size() == 3);
    int64_t id_a = chs[0].id;

    assert(db.move_chapter(b1, id_a, +1) == true);
    chs = db.get_chapters(b1);
    assert(chs[0].subject == "B" && chs[1].subject == "A" && chs[2].subject == "C");

    assert(db.move_chapter(b1, id_a, -1) == true);
    chs = db.get_chapters(b1);
    assert(chs[0].subject == "A" && chs[1].subject == "B" && chs[2].subject == "C");

    assert(db.move_chapter(b1, id_a, -1) == false);
    chs = db.get_chapters(b1);
    assert(chs[0].subject == "A" && chs[1].subject == "B" && chs[2].subject == "C");

    // Transactional batch reorder
    db.reorder_chapters({chs[2].id, chs[0].id, chs[1].id});
    chs = db.get_chapters(b1);
    assert(chs.size() == 3);
    assert(chs[0].subject == "C" && chs[0].sort_order == 1);
    assert(chs[1].subject == "A" && chs[1].sort_order == 2);
    assert(chs[2].subject == "B" && chs[2].sort_order == 3);
}

void test_folder_hierarchy_migration_and_prefs() {
    // 1. Path resolution + dangling parent + cycle guard + system folder classification
    std::vector<FolderInfo> raw = {
        {10, -1, "IMAP (user@gmail.com)", ""},
        {11, 10, "Newsletters", ""},
        {12, 11, "Tech", ""},
        {13, 999, "Orphan", ""},     // dangling parent -> root
        {20, 21, "CycleA", ""},     // cycle -> must terminate cleanly
        {21, 20, "CycleB", ""},
    };
    populate_folder_paths(raw);
    assert(raw[0].full_path == "IMAP (user@gmail.com)");
    assert(raw[1].full_path == "IMAP (user@gmail.com)/Newsletters");
    assert(raw[2].full_path == "IMAP (user@gmail.com)/Newsletters/Tech");
    assert(raw[3].parent_id == -1 && raw[3].full_path == "Orphan");
    assert(!raw[4].full_path.empty() && !raw[5].full_path.empty());

    assert(is_system_mail_folder("Trash"));
    assert(is_system_mail_folder("  sent-mail  "));
    assert(is_system_mail_folder("Drafts"));
    assert(is_system_mail_folder("Junk E-mail"));
    assert(!is_system_mail_folder("Inbox"));
    assert(!is_system_mail_folder("Newsletters"));

    // 2. Legacy folder_cache schema without parent_id migrates safely
    {
        sqlite3* raw_db = nullptr;
        assert(sqlite3_open(TEST_DB.c_str(), &raw_db) == SQLITE_OK);
        const char* legacy_sql =
            "CREATE TABLE folder_cache ("
            "  collection_id INTEGER PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  full_path TEXT NOT NULL"
            ");"
            "INSERT INTO folder_cache (collection_id, name, full_path) "
            "VALUES (1, 'Legacy', 'Legacy');";
        assert(sqlite3_exec(raw_db, legacy_sql, nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw_db);
    }

    Database db(TEST_DB);
    db.init();
    auto migrated = db.load_folder_cache();
    assert(migrated.size() == 1 && migrated[0].parent_id == -1 && migrated[0].name == "Legacy");

    // 3. Save & reload with parent_id + folder_prefs persistence
    db.save_folder_cache({
        {10, -1, "IMAP (user@gmail.com)", "IMAP (user@gmail.com)"},
        {11, 10, "Newsletters", "IMAP (user@gmail.com)/Newsletters"},
        {12, 10, "Trash", "IMAP (user@gmail.com)/Trash"},
    });
    auto reloaded = db.load_folder_cache();
    assert(reloaded.size() == 3);

    db.set_folder_pinned(11, true);
    db.set_folder_pinned(12, true);
    FolderPrefs p = db.load_folder_prefs();
    assert(p.pinned.count(11) == 1 && p.pinned.count(12) == 1);

    // Hiding a folder clears its pinned bit automatically
    db.set_folders_hidden({12}, true);
    p = db.load_folder_prefs();
    assert(p.hidden.count(12) == 1 && p.pinned.count(12) == 0 && p.pinned.count(11) == 1);

    // Replacing hidden folders preserves existing pins on non-hidden items
    db.replace_hidden_folders({10, 11});
    p = db.load_folder_prefs();
    assert(p.hidden.count(10) == 1 && p.hidden.count(11) == 1 && p.hidden.count(12) == 0);
    assert(p.pinned.count(11) == 0);

    db.set_folder_pinned(11, true);
    db.replace_hidden_folders({12});
    p = db.load_folder_prefs();
    assert(p.hidden.count(12) == 1 && p.hidden.count(11) == 0);
    assert(p.pinned.count(11) == 1);
}

int main() {
    std::cout << "╔════════════════════════════════════════════════════╗\n";
    std::cout << "║  Endpaper — Database Books Tests                   ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";

    setup();
    TEST(test_renumbering);
    setup(); // reset
    TEST(test_reversal);
    setup(); // reset
    TEST(test_auto_sort_and_clear_and_count);
    setup(); // reset
    TEST(test_list_books_chapter_count_and_move_chapter);
    setup(); // reset
    TEST(test_folder_hierarchy_migration_and_prefs);
    teardown();

    std::cout << "\n✓ All database books tests passed!\n";
    return 0;
}
