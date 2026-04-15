#include "epub_builder.h"
#include "html_sanitizer.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <zip.h>
#include <regex>

namespace fs = std::filesystem;
using namespace endpaper;

#define TEST(name)                                                 \
    do {                                                           \
        std::cout << "  TEST: " << #name << "... ";                \
        name();                                                    \
        std::cout << "PASS\n";                                     \
    } while (0)

// ── Helpers ───────────────────────────────────────────────────────────────

static std::string read_zip_file(const fs::path& zip_path, const char* entry_name) {
    int err = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    if (!archive) return "";

    zip_stat_t stat;
    if (zip_stat(archive, entry_name, 0, &stat) != 0) {
        zip_close(archive);
        return "";
    }

    zip_file_t* f = zip_fopen(archive, entry_name, 0);
    if (!f) {
        zip_close(archive);
        return "";
    }

    std::string buf(stat.size, '\0');
    zip_fread(f, buf.data(), stat.size);
    zip_fclose(f);
    zip_close(archive);
    return buf;
}

static bool zip_has_entry(const fs::path& zip_path, const char* name) {
    int err = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    if (!archive) return false;
    zip_int64_t idx = zip_name_locate(archive, name, 0);
    zip_close(archive);
    return idx >= 0;
}

static int zip_entry_count(const fs::path& zip_path) {
    int err = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    if (!archive) return -1;
    int count = static_cast<int>(zip_get_num_entries(archive, 0));
    zip_close(archive);
    return count;
}

static std::string extract_xml_tag(const std::string& xml, const std::string& tag) {
    std::regex re("<" + tag + ">(.*?)</" + tag + ">", std::regex::icase);
    std::smatch m;
    if (std::regex_search(xml, m, re)) return m[1].str();
    return "";
}

// ── Tests ─────────────────────────────────────────────────────────────────

static const fs::path TEST_DIR = fs::temp_directory_path() / "endpaper_append_test";

void setup() {
    fs::create_directories(TEST_DIR);
}

void teardown() {
    fs::remove_all(TEST_DIR);
}

void test_comprehensive_append() {
    fs::path epub = TEST_DIR / "comprehensive.epub";

    // 1. Initial build: 1 chapter
    EpubMetadata meta{
        .title = "Original Title",
        .author = "Original Author",
        .publisher = "Original Publisher",
        .language = "en",
        .uuid = "12345678-1234-1234-1234-123456781234"
    };

    std::vector<EpubChapter> initial = {
        {"Chapter 1", "author1@test.com", "<p>Content 1</p>"}
    };

    EpubBuilder::build(epub, meta, initial);
    assert(fs::exists(epub));

    // 2. First append: 2 chapters
    std::vector<EpubChapter> first_append = {
        {"Chapter 2", "author2@test.com", "<p>Content 2</p>"},
        {"Chapter 3", "author3@test.com", "<p>Content 3</p>"}
    };

    EpubBuilder::append(epub, first_append);

    // Verify first append results
    assert(zip_has_entry(epub, "OEBPS/chapter_0.xhtml"));
    assert(zip_has_entry(epub, "OEBPS/chapter_1.xhtml"));
    assert(zip_has_entry(epub, "OEBPS/chapter_2.xhtml"));

    std::string opf = read_zip_file(epub, "OEBPS/content.opf");
    assert(opf.find("id=\"chapter_0\"") != std::string::npos);
    assert(opf.find("id=\"chapter_1\"") != std::string::npos);
    assert(opf.find("id=\"chapter_2\"") != std::string::npos);

    // Verify metadata preserved
    assert(extract_xml_tag(opf, "dc:title") == "Original Title");
    assert(extract_xml_tag(opf, "dc:creator") == "Original Author");
    assert(opf.find("urn:uuid:12345678-1234-1234-1234-123456781234") != std::string::npos);

    // 3. Second append: 1 chapter
    std::vector<EpubChapter> second_append = {
        {"Chapter 4", "author4@test.com", "<p>Content 4</p>"}
    };

    EpubBuilder::append(epub, second_append);

    // Verify second append results
    assert(zip_has_entry(epub, "OEBPS/chapter_3.xhtml"));
    
    std::string toc = read_zip_file(epub, "OEBPS/toc.xhtml");
    assert(toc.find("Chapter 1") != std::string::npos);
    assert(toc.find("Chapter 2") != std::string::npos);
    assert(toc.find("Chapter 3") != std::string::npos);
    assert(toc.find("Chapter 4") != std::string::npos);

    // Verify TOC order (naive check)
    size_t p1 = toc.find("Chapter 1");
    size_t p2 = toc.find("Chapter 2");
    size_t p3 = toc.find("Chapter 3");
    size_t p4 = toc.find("Chapter 4");
    assert(p1 < p2 && p2 < p3 && p3 < p4);

    // 4. Verify content of an appended chapter
    std::string ch2 = read_zip_file(epub, "OEBPS/chapter_1.xhtml");
    assert(ch2.find("Chapter 2") != std::string::npos);
    assert(ch2.find("<p>Content 2</p>") != std::string::npos);
    assert(ch2.find("author2@test.com") != std::string::npos);
}

void test_append_with_special_chars() {
    fs::path epub = TEST_DIR / "special_chars.epub";

    EpubMetadata meta{.title = "Special & Chars <Test>", .author = "Author"};
    std::vector<EpubChapter> initial = {{"Ch 1", "a@b.com", "<p>C1</p>"}};
    EpubBuilder::build(epub, meta, initial);

    std::vector<EpubChapter> appended = {
        {"Append & Special", "s@p.com", sanitize_html("<p>Special characters: & < ></p>")}
    };
    EpubBuilder::append(epub, appended);

    std::string toc = read_zip_file(epub, "OEBPS/toc.xhtml");
    assert(toc.find("Append &amp; Special") != std::string::npos);

    std::string ch1 = read_zip_file(epub, "OEBPS/chapter_1.xhtml");
    assert(ch1.find("Special characters: &amp; &lt; &gt;") != std::string::npos);
}

void test_append_to_missing_file_throws() {
    fs::path epub = TEST_DIR / "non_existent.epub";
    std::vector<EpubChapter> chapters = {{"Ch1", "a@b.com", "<p>C1</p>"}};

    bool threw = false;
    try {
        EpubBuilder::append(epub, chapters);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_append_empty_is_noop() {
    fs::path epub = TEST_DIR / "noop.epub";
    EpubMetadata meta{.title = "Noop Test", .author = "Author"};
    std::vector<EpubChapter> initial = {{"Ch 1", "a@b.com", "<p>C1</p>"}};
    EpubBuilder::build(epub, meta, initial);

    int count_before = zip_entry_count(epub);
    std::vector<EpubChapter> empty;
    EpubBuilder::append(epub, empty);
    int count_after = zip_entry_count(epub);

    assert(count_before == count_after);
}

void test_mimetype_first_and_stored() {
    fs::path epub = TEST_DIR / "mimetype_check.epub";
    EpubMetadata meta{.title = "Mimetype Check", .author = "Author"};
    std::vector<EpubChapter> initial = {{"Ch 1", "a@b.com", "<p>C1</p>"}};
    EpubBuilder::build(epub, meta, initial);
    EpubBuilder::append(epub, initial);

    int err = 0;
    zip_t* archive = zip_open(epub.c_str(), ZIP_RDONLY, &err);
    assert(archive);

    // Verify first entry is "mimetype"
    const char* first_name = zip_get_name(archive, 0, 0);
    assert(std::string(first_name) == "mimetype");

    // Verify mimetype is uncompressed (ZIP_CM_STORE)
    zip_stat_t stat;
    zip_stat_index(archive, 0, 0, &stat);
    assert(stat.comp_method == ZIP_CM_STORE);

    zip_close(archive);
}

int main() {
    std::cout << "╔════════════════════════════════════════════════════╗\n";
    std::cout << "║  Endpaper — Comprehensive Append Tests             ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";

    setup();

    TEST(test_comprehensive_append);
    TEST(test_append_with_special_chars);
    TEST(test_append_to_missing_file_throws);
    TEST(test_append_empty_is_noop);
    TEST(test_mimetype_first_and_stored);

    teardown();

    std::cout << "\n✓ All comprehensive append tests passed!\n";
    return 0;
}
