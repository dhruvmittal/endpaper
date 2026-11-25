#include "database.h"
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
    teardown();

    std::cout << "\n✓ All database books tests passed!\n";
    return 0;
}
