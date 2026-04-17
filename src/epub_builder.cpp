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
#include <pugixml.hpp>

#include <zip.h>

namespace endpaper {

namespace {

std::string xml_escape(const std::string& data) {
    std::string buffer;
    buffer.reserve(data.size());
    for (size_t pos = 0; pos != data.size(); ++pos) {
        switch (data[pos]) {
            case '&':  buffer.append("&amp;");       break;
            case '\"': buffer.append("&quot;");      break;
            case '\'': buffer.append("&apos;");      break;
            case '<':  buffer.append("&lt;");        break;
            case '>':  buffer.append("&gt;");        break;
            default:   buffer.append(1, data[pos]); break;
        }
    }
    return buffer;
}

} // namespace

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
                                          int chapter_count,
                                          const std::vector<std::string>& image_paths) {
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
    <dc:title>)" << xml_escape(meta.title.empty() ? "Untitled" : meta.title) << R"(</dc:title>
    <dc:language>)" << xml_escape(meta.language.empty() ? "en" : meta.language) << R"(</dc:language>
    <dc:creator>)" << xml_escape(meta.author) << R"(</dc:creator>
    <dc:publisher>)" << xml_escape(meta.publisher) << R"(</dc:publisher>
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

    for (const auto& path : image_paths) {
        std::string id = path;
        std::replace(id.begin(), id.end(), '/', '_');
        std::replace(id.begin(), id.end(), '.', '_');
        opf << "    <item id=\"" << id << "\" href=\"" << path << "\" media-type=\"image/jpeg\"/>\n";
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
  <title>)" << xml_escape(title.empty() ? "Table of Contents" : title + " — Table of Contents") << R"(</title>
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
            << xml_escape(chapters[i].title) << "</a></li>\n";
    }

    toc << R"(    </ol>
  </nav>
</body>
</html>
)";

    return toc.str();
}

std::string EpubBuilder::make_chapter_xhtml(const EpubChapter& chapter, int index) {
    std::ostringstream ch;
    ch << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
  <title>)" << xml_escape(chapter.title) << R"(</title>
  <link rel="stylesheet" type="text/css" href="style.css"/>
</head>
<body>
  <header class="email-header">
    <h1>)" << xml_escape(chapter.title) << R"(</h1>
    <p class="meta">From: )" << xml_escape(chapter.author) << R"(</p>
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
    return R"(/* Endpaper EPUB Stylesheet */
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

img {
  max-width: 100%;
  height: auto;
  display: block;
  margin: 1em auto;
}
)";
}

// ── libzip helper: add a string as a file entry ──────────────────────────

static void zip_add_string(zip_t* archive, const char* entry_name,
                            const std::string& content,
                            bool store = false) {
    char* buf = static_cast<char*>(malloc(content.size()));
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
        zip_set_file_compression(archive, static_cast<zip_uint64_t>(idx),
                                  ZIP_CM_STORE, 0);
    }
}

// ── Build new EPUB ────────────────────────────────────────────────────────

void EpubBuilder::build(const fs::path& output_path,
                        const EpubMetadata& meta,
                        const std::vector<EpubChapter>& chapters,
                        const std::map<std::string, std::string>& images) {
    if (chapters.empty()) {
        throw std::runtime_error("Cannot build EPUB with no chapters");
    }

    fs::path tmp_path = output_path;
    tmp_path += ".tmp";

    int err = 0;
    zip_t* archive = zip_open(tmp_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!archive) {
        throw std::runtime_error("Cannot create EPUB zip: " + tmp_path.string());
    }

    try {
        zip_add_string(archive, "mimetype", "application/epub+zip", true);
        zip_add_string(archive, "META-INF/container.xml", make_container_xml());
        
        std::vector<std::string> image_paths;
        for (const auto& kv : images) {
            image_paths.push_back(kv.first);
        }

        zip_add_string(archive, "OEBPS/content.opf",
                       make_content_opf(meta, static_cast<int>(chapters.size()), image_paths));
        zip_add_string(archive, "OEBPS/toc.xhtml", make_toc_xhtml(meta.title, chapters));
        zip_add_string(archive, "OEBPS/style.css", make_stylesheet());

        for (size_t i = 0; i < chapters.size(); ++i) {
            std::string filename = "OEBPS/chapter_" + std::to_string(i) + ".xhtml";
            zip_add_string(archive, filename.c_str(), make_chapter_xhtml(chapters[i], static_cast<int>(i)));
        }

        for (const auto& kv : images) {
            std::string safe_name = "OEBPS/" + kv.first;
            zip_add_string(archive, safe_name.c_str(), kv.second, false);
        }

        if (zip_close(archive) != 0) {
            throw std::runtime_error(std::string("zip_close failed: ") + zip_strerror(archive));
        }
    } catch (...) {
        zip_discard(archive);
        throw;
    }

    fs::rename(tmp_path, output_path);
}

// ── Append to existing EPUB ───────────────────────────────────────────────

void EpubBuilder::append(const fs::path& epub_path,
                         const std::vector<EpubChapter>& new_chapters,
                         const std::map<std::string, std::string>& images) {
    if (new_chapters.empty()) return;

    // Use libzip to open in update mode
    int err = 0;
    zip_t* archive = zip_open(epub_path.c_str(), 0, &err);
    if (!archive) {
        throw std::runtime_error("Cannot open EPUB for append: " + epub_path.string());
    }

    try {
        auto read_entry = [&](const char* name) -> std::string {
            zip_stat_t st;
            if (zip_stat(archive, name, 0, &st) != 0) return "";
            zip_file_t* f = zip_fopen(archive, name, 0);
            if (!f) return "";
            std::string buf(st.size, '\0');
            zip_fread(f, buf.data(), st.size);
            zip_fclose(f);
            return buf;
        };

        std::string opf_str = read_entry("OEBPS/content.opf");
        if (opf_str.empty()) throw std::runtime_error("Missing content.opf");

        pugi::xml_document opf_doc;
        if (!opf_doc.load_string(opf_str.c_str())) throw std::runtime_error("Malformed content.opf");

        // Use XPath to find metadata
        pugi::xml_node pkg = opf_doc.child("package");
        pugi::xml_node meta_node = pkg.child("metadata");
        EpubMetadata meta;
        meta.title = meta_node.child("dc:title").text().get();
        meta.author = meta_node.child("dc:creator").text().get();
        meta.publisher = meta_node.child("dc:publisher").text().get();
        meta.language = meta_node.child("dc:language").text().get();
        
        std::string id_str = meta_node.child("dc:identifier").text().get();
        if (id_str.find("urn:uuid:") == 0) meta.uuid = id_str.substr(9);

        // Count existing chapters and images
        pugi::xml_node manifest = pkg.child("manifest");
        int existing_chapters = 0;
        std::vector<std::string> all_image_paths;
        
        for (pugi::xml_node item : manifest.children("item")) {
            std::string href = item.attribute("href").value();
            if (href.find("chapter_") == 0) {
                existing_chapters++;
            } else if (item.attribute("media-type").as_string() == std::string("image/jpeg")) {
                all_image_paths.push_back(href);
            }
        }

        // Add new images
        for (const auto& kv : images) {
            std::string safe_name = kv.first;
            if (std::find(all_image_paths.begin(), all_image_paths.end(), safe_name) == all_image_paths.end()) {
                all_image_paths.push_back(safe_name);
                zip_add_string(archive, ("OEBPS/" + safe_name).c_str(), kv.second, false);
            }
        }

        // Add new chapters
        for (size_t i = 0; i < new_chapters.size(); ++i) {
            int idx = existing_chapters + static_cast<int>(i);
            std::string filename = "OEBPS/chapter_" + std::to_string(idx) + ".xhtml";
            zip_add_string(archive, filename.c_str(), make_chapter_xhtml(new_chapters[i], idx));
        }

        // Update content.opf and toc.xhtml
        int total_chapters = existing_chapters + static_cast<int>(new_chapters.size());
        zip_add_string(archive, "OEBPS/content.opf", make_content_opf(meta, total_chapters, all_image_paths));

        // Read old TOC to get titles
        std::string toc_str = read_entry("OEBPS/toc.xhtml");
        std::vector<EpubChapter> all_toc_chapters;
        if (!toc_str.empty()) {
            pugi::xml_document toc_doc;
            toc_doc.load_string(toc_str.c_str());
            // Extract existing titles from <nav epub:type="toc"><ol><li><a ...>Title</a>
            for (pugi::xpath_node xnode : toc_doc.select_nodes("//*[@epub:type='toc']//ol/li/a")) {
                pugi::xml_node a = xnode.node();
                EpubChapter ch;
                ch.title = a.text().get();
                all_toc_chapters.push_back(std::move(ch));
            }
        }
        for (const auto& ch : new_chapters) all_toc_chapters.push_back(ch);
        zip_add_string(archive, "OEBPS/toc.xhtml", make_toc_xhtml(meta.title, all_toc_chapters));

        if (zip_close(archive) != 0) {
            throw std::runtime_error(std::string("zip_close failed: ") + zip_strerror(archive));
        }
    } catch (...) {
        zip_discard(archive);
        throw;
    }
}

} // namespace endpaper
