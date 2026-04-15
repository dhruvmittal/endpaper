/// Endpaper — EPUB Builder Tests
/// Verifies EPUB 3.0 structure, content, and append functionality.

#include "epub_builder.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <zip.h>
#include <pugixml.hpp>

namespace fs = std::filesystem;
using namespace endpaper;

#define TEST(name)                                                 \
    do {                                                           \
        std::cout << "  TEST: " << #name << "... ";                \
        name();                                                    \
        std::cout << "PASS\n";                                     \
    } while (0)

// ── Helpers ───────────────────────────────────────────────────────────────

/// Read a file from a zip archive into a string.
static std::string read_zip_file(const fs::path& zip_path,
                                  const char* entry_name) {
    int err = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    assert(archive && "Failed to open zip");

    zip_stat_t stat;
    int rc = zip_stat(archive, entry_name, 0, &stat);
    if (rc != 0) {
        zip_close(archive);
        return "";
    }

    zip_file_t* f = zip_fopen(archive, entry_name, 0);
    assert(f && "Failed to open entry in zip");

    std::string buf(stat.size, '\0');
    zip_fread(f, buf.data(), stat.size);
    zip_fclose(f);
    zip_close(archive);
    return buf;
}

/// Check if an entry exists in a zip archive.
static bool zip_has_entry(const fs::path& zip_path, const char* name) {
    int err = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    if (!archive) return false;
    zip_int64_t idx = zip_name_locate(archive, name, 0);
    zip_close(archive);
    return idx >= 0;
}

/// Count entries in a zip.
static int zip_entry_count(const fs::path& zip_path) {
    int err = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    if (!archive) return -1;
    int count = static_cast<int>(zip_get_num_entries(archive, 0));
    zip_close(archive);
    return count;
}

// ── Tests ─────────────────────────────────────────────────────────────────

static const fs::path TEST_DIR = fs::temp_directory_path() / "endpaper_test";

void setup() {
    fs::create_directories(TEST_DIR);
}

void teardown() {
    fs::remove_all(TEST_DIR);
}

void test_build_basic_epub() {
    fs::path out = TEST_DIR / "test_basic.epub";

    EpubMetadata meta;
    meta.title = "Test Book";
    meta.author = "Test Author";
    meta.publisher = "Test Publisher";
    meta.language = "en";

    std::vector<EpubChapter> chapters = {
        {"Chapter 1", "sender1@test.com", "<p>First chapter content</p>"},
        {"Chapter 2", "sender2@test.com", "<p>Second chapter content</p>"},
    };

    EpubBuilder::build(out, meta, chapters);

    assert(fs::exists(out));
    assert(!fs::exists(fs::path(out.string() + ".tmp")));
}

void test_mimetype_content() {
    fs::path out = TEST_DIR / "test_mimetype.epub";

    EpubMetadata meta{.title = "Mimetype Test", .author = "Author"};
    std::vector<EpubChapter> chapters = {
        {"Ch1", "a@b.com", "<p>content</p>"},
    };

    EpubBuilder::build(out, meta, chapters);

    std::string mimetype = read_zip_file(out, "mimetype");
    assert(mimetype == "application/epub+zip");
}

void test_container_xml_exists() {
    fs::path out = TEST_DIR / "test_container.epub";

    EpubMetadata meta{.title = "Container Test", .author = "Author"};
    std::vector<EpubChapter> chapters = {
        {"Ch1", "a@b.com", "<p>content</p>"},
    };

    EpubBuilder::build(out, meta, chapters);

    assert(zip_has_entry(out, "META-INF/container.xml"));

    std::string xml = read_zip_file(out, "META-INF/container.xml");
    assert(xml.find("OEBPS/content.opf") != std::string::npos);
}

void test_content_opf_structure() {
    fs::path out = TEST_DIR / "test_opf.epub";

    EpubMetadata meta{.title = "OPF Test", .author = "Author",
                      .publisher = "Publisher"};
    std::vector<EpubChapter> chapters = {
        {"Email A", "sender@test.com", "<p>body A</p>"},
        {"Email B", "sender@test.com", "<p>body B</p>"},
        {"Email C", "sender@test.com", "<p>body C</p>"},
    };

    EpubBuilder::build(out, meta, chapters);

    std::string opf = read_zip_file(out, "OEBPS/content.opf");

    // Check metadata
    assert(opf.find("<dc:title>OPF Test</dc:title>") != std::string::npos);
    assert(opf.find("<dc:creator>Author</dc:creator>") != std::string::npos);
    assert(opf.find("<dc:publisher>Publisher</dc:publisher>") !=
           std::string::npos);
    assert(opf.find("version=\"3.0\"") != std::string::npos);

    // Check manifest items
    assert(opf.find("id=\"chapter_0\"") != std::string::npos);
    assert(opf.find("id=\"chapter_1\"") != std::string::npos);
    assert(opf.find("id=\"chapter_2\"") != std::string::npos);

    // Check spine
    assert(opf.find("idref=\"chapter_0\"") != std::string::npos);
    assert(opf.find("idref=\"chapter_1\"") != std::string::npos);
    assert(opf.find("idref=\"chapter_2\"") != std::string::npos);
}

void test_chapter_files_exist() {
    fs::path out = TEST_DIR / "test_chapters.epub";

    EpubMetadata meta{.title = "Chapters Test", .author = "Author"};
    std::vector<EpubChapter> chapters = {
        {"Ch1", "a@b.com", "<p>one</p>"},
        {"Ch2", "c@d.com", "<p>two</p>"},
    };

    EpubBuilder::build(out, meta, chapters);

    assert(zip_has_entry(out, "OEBPS/chapter_0.xhtml"));
    assert(zip_has_entry(out, "OEBPS/chapter_1.xhtml"));
    assert(zip_has_entry(out, "OEBPS/toc.xhtml"));
    assert(zip_has_entry(out, "OEBPS/style.css"));

    // Verify chapter content
    std::string ch0 = read_zip_file(out, "OEBPS/chapter_0.xhtml");
    assert(ch0.find("Ch1") != std::string::npos);
    assert(ch0.find("<p>one</p>") != std::string::npos);
    assert(ch0.find("a@b.com") != std::string::npos);
}

void test_toc_structure() {
    fs::path out = TEST_DIR / "test_toc.epub";

    EpubMetadata meta{.title = "TOC Test", .author = "Author"};
    std::vector<EpubChapter> chapters = {
        {"First Email", "a@b.com", "<p>one</p>"},
        {"Second Email", "c@d.com", "<p>two</p>"},
    };

    EpubBuilder::build(out, meta, chapters);

    std::string toc = read_zip_file(out, "OEBPS/toc.xhtml");
    assert(toc.find("epub:type=\"toc\"") != std::string::npos);
    assert(toc.find("First Email") != std::string::npos);
    assert(toc.find("Second Email") != std::string::npos);
    assert(toc.find("chapter_0.xhtml") != std::string::npos);
    assert(toc.find("chapter_1.xhtml") != std::string::npos);
}

void test_append_to_epub() {
    fs::path out = TEST_DIR / "test_append.epub";

    // Build initial EPUB with 2 chapters
    EpubMetadata meta{.title = "Append Test", .author = "Author"};
    std::vector<EpubChapter> initial = {
        {"Email 1", "a@b.com", "<p>first</p>"},
        {"Email 2", "c@d.com", "<p>second</p>"},
    };

    EpubBuilder::build(out, meta, initial);

    int initial_count = zip_entry_count(out);

    // Append 2 more chapters
    std::vector<EpubChapter> appended = {
        {"Email 3", "e@f.com", "<p>third</p>"},
        {"Email 4", "g@h.com", "<p>fourth</p>"},
    };

    EpubBuilder::append(out, appended);

    // Verify new chapters exist
    assert(zip_has_entry(out, "OEBPS/chapter_2.xhtml"));
    assert(zip_has_entry(out, "OEBPS/chapter_3.xhtml"));

    // Verify OPF updated
    std::string opf = read_zip_file(out, "OEBPS/content.opf");
    assert(opf.find("id=\"chapter_2\"") != std::string::npos);
    assert(opf.find("id=\"chapter_3\"") != std::string::npos);
    assert(opf.find("idref=\"chapter_2\"") != std::string::npos);
    assert(opf.find("idref=\"chapter_3\"") != std::string::npos);

    // Original chapters still there
    assert(opf.find("id=\"chapter_0\"") != std::string::npos);
    assert(opf.find("id=\"chapter_1\"") != std::string::npos);

    // Verify TOC updated
    std::string toc = read_zip_file(out, "OEBPS/toc.xhtml");
    assert(toc.find("Email 1") != std::string::npos);
    assert(toc.find("Email 3") != std::string::npos);
    assert(toc.find("Email 4") != std::string::npos);

    // More entries in the zip
    assert(zip_entry_count(out) > initial_count);
}

void test_atomic_write() {
    fs::path out = TEST_DIR / "test_atomic.epub";
    fs::path tmp = fs::path(out.string() + ".tmp");

    EpubMetadata meta{.title = "Atomic Test", .author = "Author"};
    std::vector<EpubChapter> chapters = {
        {"Ch1", "a@b.com", "<p>content</p>"},
    };

    EpubBuilder::build(out, meta, chapters);

    // .tmp file should not exist after successful build
    assert(!fs::exists(tmp));
    assert(fs::exists(out));
}

void test_empty_chapters_throws() {
    fs::path out = TEST_DIR / "test_empty.epub";

    EpubMetadata meta{.title = "Empty Test", .author = "Author"};
    std::vector<EpubChapter> chapters;

    bool threw = false;
    try {
        EpubBuilder::build(out, meta, chapters);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_image_inclusion() {
    fs::path out = TEST_DIR / "test_images.epub";

    EpubMetadata meta{.title = "Images Test", .author = "Author"};
    std::vector<EpubChapter> chapters = {
        {"Ch1", "a@b.com", "<p>one</p><img src=\"images/test.jpg\"/>"},
    };
    std::map<std::string, std::string> images = {
        {"images/test.jpg", "fake_jpeg_data"}
    };

    EpubBuilder::build(out, meta, chapters, images);

    assert(zip_has_entry(out, "OEBPS/images/test.jpg"));
    std::string opf = read_zip_file(out, "OEBPS/content.opf");
    assert(opf.find("media-type=\"image/jpeg\"") != std::string::npos);
    assert(opf.find("href=\"images/test.jpg\"") != std::string::npos);
}

// ── XHTML validity tests ──────────────────────────────────────────────────
// These guard against regressions where generated chapter files are not
// well-formed XML and therefore cannot be opened by strict e-readers.

/// Parse an XHTML string with pugixml and assert it is well-formed XML.
static void assert_valid_xml(const std::string& xml, const char* label) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xml.c_str());
    if (!result) {
        std::cerr << "\n  XML parse error in " << label << ": "
                  << result.description()
                  << " at offset " << result.offset << "\n";
        std::cerr << "  Snippet: "
                  << xml.substr(static_cast<size_t>(std::max(0L, (long)result.offset - 40)),
                                80)
                  << "\n";
    }
    assert(result && "Chapter must be well-formed XML");
}

void test_chapters_are_valid_xml() {
    // Build an EPUB with content that historically caused XML failures:
    //   - &nbsp; (undefined in XML)
    //   - <img> tags inside <figure> (not self-closed by Lexbor)
    //   - <br> (not self-closed)
    // The builder must produce well-formed XHTML that a strict parser accepts.
    fs::path out = TEST_DIR / "test_xml_validity.epub";

    EpubMetadata meta{.title = "XML Validity Test", .author = "Author"};
    std::vector<EpubChapter> chapters = {
        // Chapter 0: bare <img> and <br>
        {"Chapter With Images", "a@b.com",
         "<p>Text</p>"
         "<figure><img src=\"images/img_1.jpg\"><figcaption>Caption</figcaption></figure>"
         "<p>After<br>image</p>"},
        // Chapter 1: &nbsp; entities
        {"Chapter With Nbsp", "b@c.com",
         "<p>Word&nbsp;word&nbsp;word</p>"},
    };
    std::map<std::string, std::string> images = {
        {"images/img_1.jpg", "fake"}
    };

    EpubBuilder::build(out, meta, chapters, images);

    // Verify every chapter parses as valid XML
    for (int i = 0; i < (int)chapters.size(); ++i) {
        std::string entry = "OEBPS/chapter_" + std::to_string(i) + ".xhtml";
        std::string content = read_zip_file(out, entry.c_str());
        assert(!content.empty() && "Chapter file must exist in EPUB");
        assert_valid_xml(content, entry.c_str());
    }

    // Spot-check: no bare &nbsp; in any chapter
    for (int i = 0; i < (int)chapters.size(); ++i) {
        std::string entry = "OEBPS/chapter_" + std::to_string(i) + ".xhtml";
        std::string content = read_zip_file(out, entry.c_str());
        assert(content.find("&nbsp;") == std::string::npos &&
               "&nbsp; must be replaced with &#160;");
    }
}

// ── Main ──────────────────────────────────────────────────────────────────

int main() {
    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║  Endpaper — EPUB Builder Tests      ║\n";
    std::cout << "╚══════════════════════════════════════╝\n\n";

    setup();

    TEST(test_build_basic_epub);
    TEST(test_mimetype_content);
    TEST(test_container_xml_exists);
    TEST(test_content_opf_structure);
    TEST(test_chapter_files_exist);
    TEST(test_toc_structure);
    TEST(test_append_to_epub);
    TEST(test_atomic_write);
    TEST(test_empty_chapters_throws);
    TEST(test_image_inclusion);
    TEST(test_chapters_are_valid_xml);

    teardown();

    std::cout << "\n✓ All EPUB builder tests passed!\n";
    return 0;
}
