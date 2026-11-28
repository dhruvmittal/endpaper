#include "build_pipeline.h"
#include "epub_builder.h"
#include "html_sanitizer.h"
#include "http_client.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <lexbor/html/interface.h>
#include <lexbor/html/node.h>
#include <lexbor/html/parser.h>
#include <lexbor/html/serialize.h>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace endpaper {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxImageDownloadBytes = 20ULL * 1024ULL * 1024ULL;

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

lxb_status_t serialization_callback(const lxb_char_t* data, size_t len, void* ctx) {
    auto* result = static_cast<std::string*>(ctx);
    result->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

std::string serialize_inner_html(lxb_dom_node_t* parent) {
    std::string result;
    lxb_html_serialize_deep_cb(parent, serialization_callback, &result);
    return result;
}

std::optional<std::string> download_url_bytes(const std::string& url,
                                              const std::atomic<bool>* cancel_flag) {
    try {
        HttpOptions opts;
        opts.timeout_secs = 15;
        opts.max_bytes = kMaxImageDownloadBytes;
        opts.cancel_flag = cancel_flag;
        HttpResponse resp = http_get(url, opts);
        if (resp.status_code == 200 && !resp.body.empty()) {
            return resp.body;
        }
    } catch (...) {
    }
    return std::nullopt;
}

void process_images_in_dom(
    lxb_dom_node_t* node,
    const BuildOptions& opts,
    std::unordered_map<std::string, std::optional<std::string>>& url_cache,
    std::map<std::string, std::string>& epub_images,
    int& image_counter,
    BuildResult& result,
    const std::function<void(const std::string&)>& progress_cb,
    const std::atomic<bool>* cancel_flag) {
    if (!node) return;

    lxb_dom_node_t* child = lxb_dom_node_first_child(node);
    while (child != nullptr) {
        if (cancel_flag && cancel_flag->load()) {
            throw std::runtime_error("Build canceled");
        }
        lxb_dom_node_t* next = lxb_dom_node_next(child);

        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT &&
            lxb_dom_node_tag_id(child) == LXB_TAG_IMG) {
            lxb_dom_element_t* el = lxb_dom_interface_element(child);
            size_t src_len = 0;
            const lxb_char_t* src_val = lxb_dom_element_get_attribute(
                el, reinterpret_cast<const lxb_char_t*>("src"), 3, &src_len);

            std::string src = (src_val && src_len > 0)
                ? decode_html_entities_in_url(std::string(reinterpret_cast<const char*>(src_val), src_len))
                : "";
            std::string lower_src = to_lower(src);

            if (lower_src.rfind("http://", 0) == 0 || lower_src.rfind("https://", 0) == 0) {
                auto cached = url_cache.find(src);
                if (cached == url_cache.end()) {
                    if (progress_cb) {
                        progress_cb("Downloading image " + std::to_string(image_counter + 1) + "...");
                    }
                    std::optional<std::string> local_path;
                    auto raw = download_url_bytes(src, cancel_flag);
                    if (cancel_flag && cancel_flag->load()) {
                        throw std::runtime_error("Build canceled");
                    }
                    if (raw) {
                        auto optimized = optimize_image_for_ereader(*raw, opts.max_image_width, opts.jpeg_quality);
                        if (optimized) {
                            std::string path = "images/img_" + std::to_string(++image_counter) + ".jpg";
                            epub_images[path] = std::move(*optimized);
                            local_path = path;
                            result.images_embedded++;
                        }
                    }
                    if (!local_path) {
                        result.images_failed++;
                    }
                    cached = url_cache.emplace(src, std::move(local_path)).first;
                }

                if (cached->second) {
                    const std::string& local = *cached->second;
                    lxb_dom_element_set_attribute(
                        el,
                        reinterpret_cast<const lxb_char_t*>("src"), 3,
                        reinterpret_cast<const lxb_char_t*>(local.data()), local.size());
                } else {
                    lxb_dom_node_remove(child);
                    lxb_dom_node_destroy(child);
                }
            } else if (lower_src.rfind("images/", 0) != 0) {
                lxb_dom_node_remove(child);
                lxb_dom_node_destroy(child);
            }
        } else {
            process_images_in_dom(child, opts, url_cache, epub_images, image_counter, result, progress_cb, cancel_flag);
        }

        child = next;
    }
}

} // namespace

StageResult stage_articles(
    Database& db,
    int64_t book_id,
    const std::vector<StageItem>& items,
    const StageOptions& opts,
    const StubResolver& resolver,
    const std::function<void(const std::string&)>& progress_cb,
    const std::atomic<bool>* cancel_flag) {
    StageResult res;
    if (items.empty()) return res;

    int base_order = db.max_sort_order(book_id);
    const size_t total = items.size();

    for (size_t idx = 0; idx < total; ++idx) {
        if (cancel_flag && cancel_flag->load()) break;

        const auto& item = items[idx];
        std::string content = item.html_body;
        bool can_resolve = opts.resolve_stubs && resolver &&
                           content.size() < opts.stub_char_threshold &&
                           (item.source_id > 0 || !item.url.empty());

        if (can_resolve) {
            if (progress_cb) {
                progress_cb("Staging " + std::to_string(res.staged + 1) + "/" +
                            std::to_string(total) + ": Resolving stub for " + item.title + "...");
            }

            if (res.staged > 0 && opts.request_delay_ms > 0) {
                int remaining = opts.request_delay_ms;
                while (remaining > 0 && (!cancel_flag || !cancel_flag->load())) {
                    int step = std::min(remaining, 100);
                    std::this_thread::sleep_for(std::chrono::milliseconds(step));
                    remaining -= step;
                }
                if (cancel_flag && cancel_flag->load()) break;
            }

            try {
                std::string fetched = resolver(item);
                if (!fetched.empty()) {
                    content = std::move(fetched);
                } else {
                    res.stub_failures++;
                }
            } catch (...) {
                res.stub_failures++;
            }
        } else if (progress_cb) {
            progress_cb("Staging " + std::to_string(res.staged + 1) + "/" +
                        std::to_string(total) + ": " + item.title);
        }

        db.add_chapter(
            book_id,
            item.title,
            item.author,
            sanitize_html(content, true),
            base_order + res.staged + 1);
        res.staged++;
    }

    db.renumber_chapters(book_id);
    return res;
}

std::string slugify(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (std::isspace(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            if (!result.empty() && result.back() != '-') {
                result += '-';
            }
        }
    }
    while (!result.empty() && result.back() == '-') result.pop_back();
    return result.empty() ? "book" : result;
}

std::string resolve_epub_output_path(const std::string& book_title,
                                     const std::string& explicit_path,
                                     const std::string& default_folder) {
    std::string out_path = default_folder;
    if (!explicit_path.empty()) {
        out_path = explicit_path;
    }

    if (out_path == "~" || out_path.rfind("~/", 0) == 0) {
        const char* home = std::getenv("HOME");
        if (home) {
            out_path = std::string(home) + out_path.substr(1);
        }
    }

    if (out_path.empty()) {
        QString doc_path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        out_path = doc_path.isEmpty() ? "." : doc_path.toStdString();
    }

    fs::path p(out_path);
    std::error_code ec;
    if (fs::is_directory(p, ec) || out_path.back() == '/' || p.extension() != ".epub") {
        if (!out_path.empty() && out_path.back() != '/') {
            out_path += '/';
        }
        out_path += slugify(book_title) + ".epub";
    }
    return out_path;
}

std::string decode_html_entities_in_url(const std::string& url) {
    std::string out = url;
    static const std::pair<const char*, const char*> replacements[] = {
        {"&amp;", "&"},
        {"&AMP;", "&"},
        {"&#038;", "&"},
        {"&#38;", "&"},
        {"&#x26;", "&"},
        {"&#X26;", "&"},
    };
    for (const auto& [from, to] : replacements) {
        size_t pos = 0;
        size_t from_len = std::char_traits<char>::length(from);
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, from_len, to);
            pos += 1;
        }
    }
    return out;
}

std::optional<std::string> optimize_image_for_ereader(
    std::string_view raw_data,
    int max_width,
    int jpeg_quality) {
    if (raw_data.empty()) return std::nullopt;

    QByteArray qbytes = QByteArray::fromRawData(raw_data.data(), static_cast<int>(raw_data.size()));
    QImage img;
    if (!img.loadFromData(qbytes) || img.isNull()) {
        return std::nullopt;
    }

    if (img.hasAlphaChannel()) {
        QImage bg(img.size(), QImage::Format_RGB32);
        bg.fill(Qt::white);
        QPainter painter(&bg);
        painter.drawImage(0, 0, img);
        painter.end();
        img = std::move(bg);
    }

    if (max_width > 0 && img.width() > max_width) {
        img = img.scaledToWidth(max_width, Qt::SmoothTransformation);
    }

    QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
    QByteArray jpeg_data;
    QBuffer buffer(&jpeg_data);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return std::nullopt;
    }
    if (!gray.save(&buffer, "JPEG", jpeg_quality)) {
        return std::nullopt;
    }

    return std::string(jpeg_data.constData(), static_cast<size_t>(jpeg_data.size()));
}

CompiledArticleFilter::CompiledArticleFilter(const std::string& query, bool fuzzy_search)
    : fuzzy_(fuzzy_search) {
    size_t first = query.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        empty_ = true;
        return;
    }
    empty_ = false;
    size_t last = query.find_last_not_of(" \t\r\n");
    std::string trimmed = query.substr(first, last - first + 1);

    if (fuzzy_) {
        std::istringstream iss(trimmed);
        std::string term;
        while (iss >> term) {
            terms_.push_back(to_lower(term));
        }
        if (terms_.empty()) empty_ = true;
        return;
    }

    try {
        regex_.emplace(trimmed, std::regex::icase);
    } catch (const std::regex_error&) {
        fallback_needle_ = to_lower(trimmed);
    }
}

bool CompiledArticleFilter::matches(const std::string& title, const std::string& author) const {
    if (empty_) return true;

    if (fuzzy_) {
        std::string haystack = to_lower(title + " " + author);
        for (const auto& term : terms_) {
            if (haystack.find(term) == std::string::npos) {
                return false;
            }
        }
        return true;
    }

    if (regex_) {
        return std::regex_search(title, *regex_) || std::regex_search(author, *regex_);
    }
    return to_lower(title).find(fallback_needle_) != std::string::npos ||
           to_lower(author).find(fallback_needle_) != std::string::npos;
}

bool matches_article_filter(const std::string& title,
                            const std::string& author,
                            const std::string& query,
                            bool fuzzy_search) {
    return CompiledArticleFilter(query, fuzzy_search).matches(title, author);
}

BuildResult build_book_epub(
    const std::string& book_title,
    const std::vector<Chapter>& chapters,
    const BuildOptions& opts,
    const std::function<void(const std::string&)>& progress_cb,
    const std::atomic<bool>* cancel_flag) {
    if (chapters.empty()) {
        throw std::runtime_error("No chapters to build.");
    }

    BuildResult result;
    result.output_path = resolve_epub_output_path(book_title, opts.explicit_path, opts.default_folder);

    std::map<std::string, std::string> epub_images;
    std::unordered_map<std::string, std::optional<std::string>> url_cache;
    std::vector<EpubChapter> epub_chapters;
    epub_chapters.reserve(chapters.size());
    int image_counter = 0;

    for (size_t i = 0; i < chapters.size(); ++i) {
        if (cancel_flag && cancel_flag->load()) {
            throw std::runtime_error("Build canceled");
        }
        const auto& c = chapters[i];
        if (progress_cb) {
            progress_cb("Processing chapter " + std::to_string(i + 1) + "/" +
                        std::to_string(chapters.size()) + ": " + c.subject);
        }

        // Sanitize/simplify BEFORE downloading images so images in stripped
        // <header>/<footer>/<nav>/<aside> blocks are not downloaded or orphaned.
        std::string content = opts.simplify
            ? simplify_html(c.content_html, opts.keep_images)
            : sanitize_html(c.content_html, opts.keep_images);

        if (opts.keep_images && !content.empty()) {
            lxb_html_document_t* doc = lxb_html_document_create();
            if (doc) {
                lxb_status_t st = lxb_html_document_parse(
                    doc,
                    reinterpret_cast<const lxb_char_t*>(content.data()),
                    content.size());
                if (st == LXB_STATUS_OK && doc->body) {
                    lxb_dom_node_t* body = lxb_dom_interface_node(doc->body);
                    process_images_in_dom(body, opts, url_cache, epub_images, image_counter, result, progress_cb, cancel_flag);
                    content = serialize_inner_html(body);
                }
                lxb_html_document_destroy(doc);
            }
        }

        epub_chapters.push_back(EpubChapter{c.subject, c.sender, std::move(content)});
    }

    fs::path out_fs(result.output_path);
    if (out_fs.has_parent_path() && !out_fs.parent_path().empty()) {
        fs::create_directories(out_fs.parent_path());
    }

    EpubMetadata meta;
    meta.title = book_title;
    meta.author = opts.author.empty() ? "Unknown" : opts.author;
    meta.publisher = opts.publisher.empty() ? "Endpaper" : opts.publisher;

    if (opts.append && fs::exists(out_fs)) {
        EpubBuilder::append(out_fs, epub_chapters, epub_images);
    } else {
        EpubBuilder::build(out_fs, meta, epub_chapters, epub_images);
    }

    std::error_code ec;
    result.file_size_bytes = fs::file_size(out_fs, ec);
    result.chapters_built = static_cast<int>(epub_chapters.size());
    return result;
}

std::string format_build_summary(const BuildResult& res, bool keep_images) {
    std::ostringstream oss;
    oss << "Built: " << res.output_path << " (" << res.chapters_built << " ch";
    if (keep_images) {
        oss << ", " << res.images_embedded << " img";
        if (res.images_failed > 0) {
            oss << ", " << res.images_failed << " failed";
        }
    }
    if (res.file_size_bytes > 0) {
        if (res.file_size_bytes >= 1024ULL * 1024ULL) {
            double mb = static_cast<double>(res.file_size_bytes) / (1024.0 * 1024.0);
            oss << ", " << std::fixed << std::setprecision(1) << mb << " MB";
        } else {
            uintmax_t kb = std::max<uintmax_t>(1, res.file_size_bytes / 1024ULL);
            oss << ", " << kb << " KB";
        }
    }
    oss << ")";
    return oss.str();
}

} // namespace endpaper
