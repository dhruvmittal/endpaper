#pragma once

#include "database.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace endpaper {

/// Lightweight tracked background worker pool shared by GUI and TUI.
class WorkerPool {
public:
    ~WorkerPool() { stop_and_join(); }

    void launch(std::function<void()> fn) {
        std::vector<Handle> finished;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (shutting_down_.load()) return;

            auto it = workers_.begin();
            while (it != workers_.end()) {
                if (it->done->load()) {
                    finished.push_back(std::move(*it));
                    it = workers_.erase(it);
                } else {
                    ++it;
                }
            }

            auto done = std::make_shared<std::atomic<bool>>(false);
            std::thread t([fn = std::move(fn), done]() {
                try {
                    fn();
                } catch (...) {
                }
                done->store(true);
            });
            workers_.push_back(Handle{std::move(done), std::move(t)});
        }
        for (auto& w : finished) {
            if (w.th.joinable()) w.th.join();
        }
    }

    void request_stop() { shutting_down_.store(true); }

    bool is_shutting_down() const { return shutting_down_.load(); }

    const std::atomic<bool>* stop_flag() const { return &shutting_down_; }

    void stop_and_join() {
        shutting_down_.store(true);
        std::vector<Handle> to_join;
        {
            std::lock_guard<std::mutex> lock(mu_);
            to_join = std::move(workers_);
        }
        for (auto& w : to_join) {
            if (w.th.joinable()) w.th.join();
        }
    }

private:
    struct Handle {
        std::shared_ptr<std::atomic<bool>> done;
        std::thread th;
    };

    std::atomic<bool> shutting_down_{false};
    std::mutex mu_;
    std::vector<Handle> workers_;
};

struct StageItem {
    std::string title;
    std::string author;
    std::string html_body;
    int64_t source_id = 0;
    std::string url;
};

struct StageOptions {
    bool resolve_stubs = true;
    size_t stub_char_threshold = 1000;
    int request_delay_ms = 1000;
};

struct StageResult {
    int staged = 0;
    int stub_failures = 0;
};

using StubResolver = std::function<std::string(const StageItem&)>;

/// Stage selected articles into a book, optionally resolving short stubs.
StageResult stage_articles(
    Database& db,
    int64_t book_id,
    const std::vector<StageItem>& items,
    const StageOptions& opts,
    const StubResolver& resolver = {},
    const std::function<void(const std::string&)>& progress_cb = {},
    const std::atomic<bool>* cancel_flag = nullptr);

struct BuildOptions {
    std::string author = "Unknown";
    std::string publisher = "Endpaper";
    std::string explicit_path;
    std::string default_folder;
    bool append = false;
    bool simplify = true;
    bool keep_images = false;
    int max_image_width = 480;
    int jpeg_quality = 75;
};

struct BuildResult {
    std::string output_path;
    int chapters_built = 0;
    int images_embedded = 0;
    int images_failed = 0;
    uintmax_t file_size_bytes = 0;
};

/// Convert a book title into a filesystem-safe lowercase slug.
[[nodiscard]] std::string slugify(const std::string& s);

/// Resolve final output .epub path from explicit path, configured default folder,
/// and book title. Does not create directories on disk.
[[nodiscard]] std::string resolve_epub_output_path(const std::string& book_title,
                                                   const std::string& explicit_path,
                                                   const std::string& default_folder);

/// Decode common HTML entities (&amp;, &#38;, &#038;, &#x26;) inside image URLs.
[[nodiscard]] std::string decode_html_entities_in_url(const std::string& url);

/// Resize and convert raw image bytes into a grayscale JPEG suited for e-ink readers.
[[nodiscard]] std::optional<std::string> optimize_image_for_ereader(
    std::string_view raw_data,
    int max_width = 480,
    int jpeg_quality = 75);

/// Precompiled article search filter (fuzzy word tokens or compiled regex).
class CompiledArticleFilter {
public:
    CompiledArticleFilter() = default;
    CompiledArticleFilter(const std::string& query, bool fuzzy_search);

    [[nodiscard]] bool matches(const std::string& title, const std::string& author) const;

private:
    bool empty_ = true;
    bool fuzzy_ = true;
    std::vector<std::string> terms_;
    std::optional<std::regex> regex_;
    std::string fallback_needle_;
};

/// Match an article's title/author against a search query in fuzzy or regex mode.
[[nodiscard]] bool matches_article_filter(const std::string& title,
                                          const std::string& author,
                                          const std::string& query,
                                          bool fuzzy_search);

/// Build or append an EPUB from staged chapters, downloading/optimizing images if enabled.
BuildResult build_book_epub(
    const std::string& book_title,
    const std::vector<Chapter>& chapters,
    const BuildOptions& opts,
    const std::function<void(const std::string&)>& progress_cb = {},
    const std::atomic<bool>* cancel_flag = nullptr);

/// Format a human-readable summary including chapter count, images, and file size.
[[nodiscard]] std::string format_build_summary(const BuildResult& res, bool keep_images);

} // namespace endpaper
