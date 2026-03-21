#include "epub_builder.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>

#include <zip.h>

namespace mail2book {

namespace fs = std::filesystem;

// ── UUID generator ────────────────────────────────────────────────────────

std::string EpubBuilder::generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    auto hex = [&](int n) {
        std::ostringstream ss;
        ss << std::hex << std::setfill('0') << std::setw(n) << (dist(gen) >> (32 - n * 4));
        return ss.str();
    };

    return hex(8) + "-" + hex(4) + "-4" + hex(3) + "-" +
           std::string(1, "89ab"[dist(gen) % 4]) + hex(3) + "-" + hex(8) + hex(4);
}

// ── EPUB template generators ─────────────────────────────────────────────

std::string EpubBuilder::make_container_xml() {
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
)";
}

std::string EpubBuilder::make_content_opf(const EpubMetadata& meta,
                                          int chapter_count) {
    // Current timestamp for dcterms:modified
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");

    std::string uuid = meta.uuid.empty() ? generate_uuid() : meta.uuid;

    std::ostringstream opf;
    opf << R"(<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="BookId">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="BookId">urn:uuid:)" << uuid << R"(</dc:identifier>
    <dc:title>)" << meta.title << R"(</dc:title>
    <dc:language>)" << meta.language << R"(</dc:language>
    <dc:creator>)" << meta.author << R"(</dc:creator>
    <dc:publisher>)" << meta.publisher << R"(</dc:publisher>
    <meta property="dcterms:modified">)" << ts.str() << R"(</meta>
  </metadata>
  <manifest>
    <item id="toc" href="toc.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="style" href="style.css" media-type="text/css"/>
)";

    for (int i = 0; i < chapter_count; ++i) {
        opf << "    <item id=\"chapter_" << i
            << "\" href=\"chapter_" << i
            << ".xhtml\" media-type=\"application/xhtml+xml\"/>\n";
    }

    opf << R"(  </manifest>
  <spine>
)";

    for (int i = 0; i < chapter_count; ++i) {
        opf << "    <itemref idref=\"chapter_" << i << "\"/>\n";
    }

    opf << R"(  </spine>
</package>
)";

    return opf.str();
}

std::string EpubBuilder::make_toc_xhtml(
    const std::string& title,
    const std::vector<EpubChapter>& chapters,
    int start_index) {
    std::ostringstream toc;
    toc << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head>
  <title>)" << title << R"( — Table of Contents</title>
  <link rel="stylesheet" type="text/css" href="style.css"/>
</head>
<body>
  <nav epub:type="toc" id="toc">
    <h1>Table of Contents</h1>
    <ol>
)";

    for (size_t i = 0; i < chapters.size(); ++i) {
        int idx = start_index + static_cast<int>(i);
        toc << "      <li><a href=\"chapter_" << idx << ".xhtml\">"
            << chapters[i].title << "</a></li>\n";
    }

    toc << R"(    </ol>
  </nav>
</body>
</html>
)";

    return toc.str();
}

std::string EpubBuilder::make_chapter_xhtml(const EpubChapter& chapter,
                                            [[maybe_unused]] int index) {
    std::ostringstream ch;
    ch << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
  <title>)" << chapter.title << R"(</title>
  <link rel="stylesheet" type="text/css" href="style.css"/>
</head>
<body>
  <header class="email-header">
    <h1>)" << chapter.title << R"(</h1>
    <p class="meta">From: )" << chapter.author << R"(</p>
  </header>
  <article class="email-body">
)" << chapter.body_html << R"(
  </article>
</body>
</html>
)";

    return ch.str();
}

std::string EpubBuilder::make_stylesheet() {
    return R"(/* Mail2Book EPUB Stylesheet */
body {
  font-family: Georgia, "Times New Roman", serif;
  margin: 1em;
  line-height: 1.6;
  color: #222;
}

h1 {
  font-size: 1.4em;
  margin-bottom: 0.3em;
  border-bottom: 1px solid #ccc;
  padding-bottom: 0.2em;
}

.email-header {
  margin-bottom: 1.5em;
}

.meta {
  font-size: 0.9em;
  color: #666;
  font-style: italic;
}

.email-body {
  margin-top: 1em;
}

blockquote {
  border-left: 3px solid #ccc;
  margin-left: 0;
  padding-left: 1em;
  color: #555;
}

a {
  color: #2a6496;
  text-decoration: none;
}

pre, code {
  font-family: "Courier New", monospace;
  font-size: 0.9em;
  background: #f4f4f4;
  padding: 0.2em 0.4em;
}

pre {
  padding: 0.8em;
  overflow-x: auto;
}

table {
  border-collapse: collapse;
  width: 100%;
  margin: 1em 0;
}

td, th {
  border: 1px solid #ddd;
  padding: 0.4em 0.6em;
  text-align: left;
}
)";
}

// ── libzip helper: add a string as a file entry ──────────────────────────

static void zip_add_string(zip_t* archive, const char* entry_name,
                            const std::string& content,
                            bool store = false) {
    // We must copy the data because zip_source_buffer with freep=0
    // does NOT copy — but libzip defers the actual write to zip_close,
    // by which time the caller's string may be out of scope.
    void* buf = malloc(content.size());
    if (!buf) {
        throw std::runtime_error("malloc failed for zip entry");
    }
    std::memcpy(buf, content.data(), content.size());

    zip_source_t* src = zip_source_buffer(archive, buf, content.size(), 1);
    if (!src) {
        free(buf);
        throw std::runtime_error(
            std::string("zip_source_buffer failed: ") + zip_strerror(archive));
    }

    zip_int64_t idx = zip_file_add(archive, entry_name, src,
                                    ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        zip_source_free(src);
        throw std::runtime_error(
            std::string("zip_file_add failed: ") + zip_strerror(archive));
    }

    if (store) {
        // mimetype must be stored uncompressed
        zip_set_file_compression(archive, static_cast<zip_uint64_t>(idx),
                                  ZIP_CM_STORE, 0);
    }
}

// ── Build new EPUB ────────────────────────────────────────────────────────

void EpubBuilder::create_epub_zip(const fs::path& zip_path,
                                  const EpubMetadata& meta,
                                  const std::vector<EpubChapter>& chapters) {
    int err = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!archive) {
        throw std::runtime_error("Cannot create EPUB zip: " +
                                  zip_path.string());
    }

    try {
        // 1. mimetype (must be first entry, uncompressed)
        std::string mimetype = "application/epub+zip";
        zip_add_string(archive, "mimetype", mimetype, true);

        // 2. META-INF/container.xml
        zip_add_string(archive, "META-INF/container.xml",
                       make_container_xml());

        // 3. OEBPS/content.opf
        zip_add_string(archive, "OEBPS/content.opf",
                       make_content_opf(meta,
                                        static_cast<int>(chapters.size())));

        // 4. OEBPS/toc.xhtml
        zip_add_string(archive, "OEBPS/toc.xhtml",
                       make_toc_xhtml(meta.title, chapters));

        // 5. OEBPS/style.css
        zip_add_string(archive, "OEBPS/style.css", make_stylesheet());

        // 6. Chapter files
        for (size_t i = 0; i < chapters.size(); ++i) {
            std::string filename = "OEBPS/chapter_" + std::to_string(i) +
                                   ".xhtml";
            zip_add_string(archive, filename.c_str(),
                          make_chapter_xhtml(chapters[i],
                                             static_cast<int>(i)));
        }

        if (zip_close(archive) != 0) {
            throw std::runtime_error(
                std::string("zip_close failed: ") + zip_strerror(archive));
        }
    } catch (...) {
        zip_discard(archive);
        throw;
    }
}

void EpubBuilder::build(const fs::path& output_path,
                        const EpubMetadata& meta,
                        const std::vector<EpubChapter>& chapters) {
    if (chapters.empty()) {
        throw std::runtime_error("Cannot build EPUB with no chapters");
    }

    // Atomic write: build to .tmp, then rename
    fs::path tmp_path = output_path;
    tmp_path += ".tmp";

    create_epub_zip(tmp_path, meta, chapters);
    fs::rename(tmp_path, output_path);
}

// ── Append to existing EPUB ───────────────────────────────────────────────

void EpubBuilder::append(const fs::path& epub_path,
                         const std::vector<EpubChapter>& new_chapters) {
    if (new_chapters.empty()) return;
    if (!fs::exists(epub_path)) {
        throw std::runtime_error("EPUB does not exist: " +
                                  epub_path.string());
    }

    // 1. Open existing EPUB and read its contents
    int err = 0;
    zip_t* archive = zip_open(epub_path.c_str(), 0, &err);
    if (!archive) {
        throw std::runtime_error("Cannot open EPUB: " + epub_path.string());
    }

    // Helper: read a file from the zip into a string
    auto read_zip_entry = [&](const char* name) -> std::string {
        zip_stat_t stat;
        if (zip_stat(archive, name, 0, &stat) != 0) {
            return "";
        }
        zip_file_t* f = zip_fopen(archive, name, 0);
        if (!f) return "";
        std::string buf(stat.size, '\0');
        zip_fread(f, buf.data(), stat.size);
        zip_fclose(f);
        return buf;
    };

    // 2. Parse content.opf to find existing chapter count
    std::string opf_content = read_zip_entry("OEBPS/content.opf");
    if (opf_content.empty()) {
        zip_close(archive);
        throw std::runtime_error("Cannot read content.opf from EPUB");
    }

    // Count existing chapters by looking for chapter_N patterns in manifest
    int existing_count = 0;
    {
        std::regex chapter_re(R"RE(id="chapter_(\d+)")RE");
        auto begin = std::sregex_iterator(opf_content.begin(),
                                           opf_content.end(), chapter_re);
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            int n = std::stoi((*it)[1].str());
            existing_count = std::max(existing_count, n + 1);
        }
    }

    // 3. Read existing TOC to merge
    std::string toc_content = read_zip_entry("OEBPS/toc.xhtml");

    // 4. Collect all existing zip entries
    struct ZipEntry {
        std::string name;
        std::string data;
        bool is_mimetype;
    };
    std::vector<ZipEntry> entries;

    zip_int64_t num = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < num; ++i) {
        const char* name = zip_get_name(archive, static_cast<zip_uint64_t>(i), 0);
        if (!name) continue;

        std::string sname(name);
        // We'll regenerate content.opf and toc.xhtml
        if (sname == "OEBPS/content.opf" || sname == "OEBPS/toc.xhtml") {
            continue;
        }

        zip_stat_t stat;
        zip_stat_index(archive, static_cast<zip_uint64_t>(i), 0, &stat);
        zip_file_t* f = zip_fopen_index(archive, static_cast<zip_uint64_t>(i), 0);
        std::string data;
        if (f && stat.size > 0) {
            data.resize(stat.size);
            zip_fread(f, data.data(), stat.size);
            zip_fclose(f);
        }

        entries.push_back({sname, data, sname == "mimetype"});
    }

    zip_close(archive);

    // 5. Build updated content.opf
    int total_chapters = existing_count + static_cast<int>(new_chapters.size());

    // Extract metadata from existing OPF (simple regex extraction)
    auto extract_tag = [&](const std::string& tag) -> std::string {
        std::regex re("<dc:" + tag + ">(.*?)</dc:" + tag + ">",
                      std::regex::icase);
        std::smatch m;
        if (std::regex_search(opf_content, m, re)) return m[1].str();
        return "";
    };

    EpubMetadata meta;
    meta.title = extract_tag("title");
    meta.author = extract_tag("creator");
    meta.publisher = extract_tag("publisher");
    meta.language = extract_tag("language");

    // Extract UUID
    {
        std::regex uuid_re(R"RE(urn:uuid:([a-f0-9\-]+))RE", std::regex::icase);
        std::smatch m;
        if (std::regex_search(opf_content, m, uuid_re)) {
            meta.uuid = m[1].str();
        }
    }

    std::string new_opf = make_content_opf(meta, total_chapters);

    // 6. Build updated TOC — collect all chapter titles
    std::vector<EpubChapter> all_toc_chapters;
    // Existing chapters: extract titles from the old TOC
    {
        std::regex toc_entry_re(
            R"RE(<a href="chapter_\d+\.xhtml">(.*?)</a>)RE",
            std::regex::icase);
        auto begin = std::sregex_iterator(toc_content.begin(),
                                           toc_content.end(), toc_entry_re);
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            EpubChapter dummy;
            dummy.title = (*it)[1].str();
            all_toc_chapters.push_back(dummy);
        }
    }
    // Add new chapter titles
    for (const auto& ch : new_chapters) {
        all_toc_chapters.push_back(ch);
    }
    std::string new_toc = make_toc_xhtml(meta.title, all_toc_chapters);

    // 7. Create new EPUB with everything combined
    fs::path tmp_path = epub_path;
    tmp_path += ".tmp";

    int zerr = 0;
    zip_t* new_archive = zip_open(tmp_path.c_str(),
                                   ZIP_CREATE | ZIP_TRUNCATE, &zerr);
    if (!new_archive) {
        throw std::runtime_error("Cannot create temp EPUB: " +
                                  tmp_path.string());
    }

    try {
        // Re-add all existing entries
        for (const auto& entry : entries) {
            zip_add_string(new_archive, entry.name.c_str(), entry.data,
                          entry.is_mimetype);
        }

        // Add updated content.opf and toc
        zip_add_string(new_archive, "OEBPS/content.opf", new_opf);
        zip_add_string(new_archive, "OEBPS/toc.xhtml", new_toc);

        // Add new chapter files
        for (size_t i = 0; i < new_chapters.size(); ++i) {
            int idx = existing_count + static_cast<int>(i);
            std::string filename = "OEBPS/chapter_" + std::to_string(idx) +
                                   ".xhtml";
            zip_add_string(new_archive, filename.c_str(),
                          make_chapter_xhtml(new_chapters[i], idx));
        }

        if (zip_close(new_archive) != 0) {
            throw std::runtime_error(
                std::string("zip_close failed: ") +
                zip_strerror(new_archive));
        }
    } catch (...) {
        zip_discard(new_archive);
        throw;
    }

    // Atomic rename
    fs::rename(tmp_path, epub_path);
}

} // namespace mail2book
