#include "epub_builder.h"
#include "html_sanitizer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <ctime>
#include <unordered_set>
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
                                          const std::vector<ManifestItem>& extra_items) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&time, &tm_buf);
    std::ostringstream ts;
    ts << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");

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

    for (const auto& item : extra_items) {
        opf << "    <item id=\"" << xml_escape(item.id)
            << "\" href=\"" << xml_escape(item.href)
            << "\" media-type=\"" << xml_escape(item.media_type) << "\"";
        if (!item.properties.empty()) {
            opf << " properties=\"" << xml_escape(item.properties) << "\"";
        }
        opf << "/>\n";
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
    (void)index;
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
)";
    if (!chapter.author.empty()) {
        ch << "    <p class=\"meta\">From: " << xml_escape(chapter.author) << "</p>\n";
    }
    ch << R"(  </header>
  <article class="email-body">
)" << ensure_xhtml(chapter.body_html) << R"(
  </article>
</body>
</html>
)";

    return ch.str();
}

std::string EpubBuilder::make_stylesheet() {
    return R"(/* Endpaper EPUB Stylesheet (optimized for e-ink) */
body {
  font-family: Georgia, "Times New Roman", serif;
  margin: 0.5em;
  line-height: 1.5;
  color: #000;
}

h1 {
  font-size: 1.35em;
  margin-bottom: 0.3em;
  border-bottom: 1px solid #000;
  padding-bottom: 0.2em;
}

.email-header {
  margin-bottom: 1.2em;
}

.meta {
  font-size: 0.9em;
  color: #444;
  font-style: italic;
  margin-top: 0.2em;
}

.email-body {
  margin-top: 0.8em;
}

blockquote {
  border-left: 3px solid #444;
  margin-left: 0;
  padding-left: 0.8em;
  color: #222;
}

a {
  color: #000;
  text-decoration: underline;
  overflow-wrap: break-word;
}

pre, code {
  font-family: "Courier New", monospace;
  font-size: 0.85em;
  background: #eee;
  padding: 0.15em 0.35em;
  overflow-wrap: break-word;
}

pre {
  padding: 0.6em;
  border: 1px solid #444;
  white-space: pre-wrap;
}

table {
  border-collapse: collapse;
  width: 100%;
  margin: 0.8em 0;
}

td, th {
  border: 1px solid #444;
  padding: 0.35em 0.5em;
  text-align: left;
}

img {
  max-width: 100%;
  height: auto;
  display: block;
  margin: 0.8em auto;
}
)";
}

// ── libzip helper: add a string as a file entry ──────────────────────────

static std::string make_manifest_id(const std::string& href,
                                    std::unordered_set<std::string>& used_ids) {
    std::string base = href;
    std::replace(base.begin(), base.end(), '/', '_');
    std::replace(base.begin(), base.end(), '.', '_');
    if (base.empty()) base = "item";
    std::string candidate = base;
    int suffix = 2;
    while (!used_ids.insert(candidate).second) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

static void zip_add_string(zip_t* archive, const char* entry_name,
                            const std::string& content,
                            bool store = false) {
    char* buf = static_cast<char*>(malloc(std::max<size_t>(1, content.size())));
    if (!buf) {
        throw std::runtime_error("malloc failed for zip entry");
    }
    if (!content.empty()) {
        std::memcpy(buf, content.data(), content.size());
    }

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
        
        std::vector<ManifestItem> extra_items;
        extra_items.reserve(images.size());
        std::unordered_set<std::string> used_ids;
        for (const auto& kv : images) {
            extra_items.push_back(ManifestItem{
                make_manifest_id(kv.first, used_ids), kv.first, "image/jpeg", ""
            });
        }

        zip_add_string(archive, "OEBPS/content.opf",
                       make_content_opf(meta, static_cast<int>(chapters.size()), extra_items));
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

        // Count existing chapters and preserve non-chapter manifest items (images, etc.)
        pugi::xml_node manifest = pkg.child("manifest");
        int existing_chapters = 0;
        std::vector<ManifestItem> all_extra_items;
        std::unordered_set<std::string> existing_images;
        std::unordered_set<std::string> used_ids;
        int next_img_idx = 0;

        for (pugi::xml_node item : manifest.children("item")) {
            std::string href = item.attribute("href").value();
            if (href.rfind("chapter_", 0) == 0) {
                existing_chapters++;
            } else if (href != "toc.xhtml" && href != "style.css") {
                std::string id = item.attribute("id").as_string();
                if (id.empty() || !used_ids.insert(id).second) {
                    id = make_manifest_id(href, used_ids);
                }
                std::string media_type = item.attribute("media-type").as_string();
                if (media_type.empty()) media_type = "application/octet-stream";
                all_extra_items.push_back(ManifestItem{
                    std::move(id),
                    href,
                    std::move(media_type),
                    item.attribute("properties").as_string()
                });
                existing_images.insert(href);
                // Track highest images/img_<N>.jpg index to avoid filename collisions
                const std::string prefix = "images/img_";
                const std::string suffix = ".jpg";
                if (href.rfind(prefix, 0) == 0 && href.size() > prefix.size() + suffix.size() &&
                    href.compare(href.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    try {
                        int n = std::stoi(href.substr(prefix.size(), href.size() - prefix.size() - suffix.size()));
                        next_img_idx = std::max(next_img_idx, n);
                    } catch (...) {}
                }
            }
        }

        // Reserve existing and incoming names before allocating new names so no
        // rename can collide with a subsequent incoming image.
        std::unordered_set<std::string> reserved = existing_images;
        for (const auto& kv : images) {
            reserved.insert(kv.first);
        }

        std::vector<std::pair<std::string, std::string>> renames;
        std::vector<std::pair<std::string, std::string>> entries_to_add;
        for (const auto& kv : images) {
            std::string target_path = kv.first;
            if (existing_images.count(target_path)) {
                do {
                    target_path = "images/img_" + std::to_string(++next_img_idx) + ".jpg";
                } while (reserved.count(target_path));
                reserved.insert(target_path);
                renames.emplace_back(kv.first, target_path);
            }
            existing_images.insert(target_path);
            all_extra_items.push_back(ManifestItem{
                make_manifest_id(target_path, used_ids), target_path, "image/jpeg", ""
            });
            entries_to_add.emplace_back(target_path, kv.second);
        }

        std::vector<EpubChapter> chapters_copy = new_chapters;
        if (!renames.empty()) {
            auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
                size_t pos = 0;
                while ((pos = str.find(from, pos)) != std::string::npos) {
                    str.replace(pos, from.size(), to);
                    pos += to.size();
                }
            };
            // Two-phase replacement prevents cascading img_1->img_2->img_3 collisions
            for (size_t idx = 0; idx < renames.size(); ++idx) {
                std::string token = "__ENDPAPER_IMG_REMAP_" + std::to_string(idx) + "__";
                for (auto& ch : chapters_copy) {
                    replace_all(ch.body_html, "\"" + renames[idx].first + "\"", "\"" + token + "\"");
                    replace_all(ch.body_html, "'" + renames[idx].first + "'", "'" + token + "'");
                }
            }
            for (size_t idx = 0; idx < renames.size(); ++idx) {
                std::string token = "__ENDPAPER_IMG_REMAP_" + std::to_string(idx) + "__";
                for (auto& ch : chapters_copy) {
                    replace_all(ch.body_html, "\"" + token + "\"", "\"" + renames[idx].second + "\"");
                    replace_all(ch.body_html, "'" + token + "'", "'" + renames[idx].second + "'");
                }
            }
        }

        for (const auto& [target_path, bytes] : entries_to_add) {
            zip_add_string(archive, ("OEBPS/" + target_path).c_str(), bytes, false);
        }

        // Add new chapters
        for (size_t i = 0; i < chapters_copy.size(); ++i) {
            int idx = existing_chapters + static_cast<int>(i);
            std::string filename = "OEBPS/chapter_" + std::to_string(idx) + ".xhtml";
            zip_add_string(archive, filename.c_str(), make_chapter_xhtml(chapters_copy[i], idx));
        }

        // Update content.opf and toc.xhtml
        int total_chapters = existing_chapters + static_cast<int>(chapters_copy.size());
        zip_add_string(archive, "OEBPS/content.opf", make_content_opf(meta, total_chapters, all_extra_items));

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
        for (const auto& ch : chapters_copy) all_toc_chapters.push_back(ch);
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
