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

int main() {
    std::cout << "╔════════════════════════════════════════════════════╗\n";
    std::cout << "║  Endpaper — Database Books Tests                   ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";

    setup();
    TEST(test_renumbering);
    setup(); // reset
    TEST(test_reversal);
    teardown();

    std::cout << "\n✓ All database books tests passed!\n";
    return 0;
}
