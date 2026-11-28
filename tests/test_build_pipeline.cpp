#include "build_pipeline.h"
#include "config.h"
#include "database.h"
#include "http_client.h"

#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QPainter>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace endpaper;

#define TEST(name)                                                 \
    do {                                                           \
        std::cout << "  TEST: " << #name << "... ";                \
        name();                                                    \
        std::cout << "PASS\n";                                     \
    } while (0)

void test_slugify_and_paths() {
    assert(slugify("Hello, World! 2026") == "hello-world-2026");
    assert(slugify("___") == "book");

    std::string p1 = resolve_epub_output_path("My Book", "/tmp/custom_out.epub", "/tmp/ignored");
    assert(p1 == "/tmp/custom_out.epub");

    std::string p2 = resolve_epub_output_path("My Book", "", "/tmp/out_dir");
    assert(p2 == "/tmp/out_dir/my-book.epub");

    std::string p3 = resolve_epub_output_path("My Book", "/tmp/explicit_dir/", "/tmp/ignored");
    assert(p3 == "/tmp/explicit_dir/my-book.epub");
}

void test_url_entity_decoding() {
    std::string raw = "https://i0.wp.com/example.com/a.png?w=800&#038;ssl=1&amp;q=80&#x26;x=2";
    std::string decoded = decode_html_entities_in_url(raw);
    assert(decoded == "https://i0.wp.com/example.com/a.png?w=800&ssl=1&q=80&x=2");
}

void test_optimize_image_for_ereader() {
    QImage src(800, 400, QImage::Format_ARGB32);
    src.fill(Qt::transparent);
    {
        QPainter p(&src);
        p.fillRect(300, 150, 200, 100, QColor(0, 0, 0));
    }

    QByteArray png_bytes;
    QBuffer buf(&png_bytes);
    assert(buf.open(QIODevice::WriteOnly));
    assert(src.save(&buf, "PNG"));

    auto optimized = optimize_image_for_ereader(
        std::string_view(png_bytes.constData(), static_cast<size_t>(png_bytes.size())),
        480, 75);
    assert(optimized.has_value());

    QImage out;
    assert(out.loadFromData(reinterpret_cast<const uchar*>(optimized->data()),
                            static_cast<int>(optimized->size()), "JPEG"));
    assert(out.width() == 480);
    assert(out.height() == 240);
    assert(out.allGray());

    // Transparent edge should be composited onto white, center square should be dark
    assert(qGray(out.pixel(5, 5)) >= 240);
    assert(qGray(out.pixel(240, 120)) <= 35);
}

void test_article_filter() {
    assert(matches_article_filter("Rust Async Patterns", "Jane Doe", "", true));
    assert(matches_article_filter("Rust Async Patterns", "Jane Doe", "jane rust", true));
    assert(!matches_article_filter("Rust Async Patterns", "Jane Doe", "python rust", true));

    assert(matches_article_filter("Issue #42: Systems", "Editor", R"(issue\s+#\d+)", false));
    assert(!matches_article_filter("Issue #42: Systems", "Editor", R"(^editor$)", true));
    // Malformed regex while typing falls back to substring
    assert(matches_article_filter("Vector[int] Guide", "Dev", "vector[", false));

    // Precompiled filter matches same semantics
    CompiledArticleFilter cf_fuzzy("jane rust", true);
    assert(cf_fuzzy.matches("Rust Async Patterns", "Jane Doe"));
    assert(!cf_fuzzy.matches("Python Async", "Jane Doe"));

    CompiledArticleFilter cf_regex(R"(issue\s+#\d+)", false);
    assert(cf_regex.matches("Issue #42: Systems", "Editor"));
    assert(!cf_regex.matches("No match here", "Editor"));

    CompiledArticleFilter cf_bad("vector[", false);
    assert(cf_bad.matches("Vector[int] Guide", "Dev"));

    // Immediate HTTP cancelation when cancel_flag is set
    std::atomic<bool> canceled{true};
    HttpOptions opts;
    opts.cancel_flag = &canceled;
    bool threw_cancel = false;
    try {
        (void)http_get("https://example.com", opts);
    } catch (const std::runtime_error&) {
        threw_cancel = true;
    }
    assert(threw_cancel);
}

void test_config_legacy_migration_int64_and_perms() {
    fs::path tmp_root = fs::temp_directory_path() / "endpaper_cfg_test";
    fs::remove_all(tmp_root);
    fs::path cfg_dir = tmp_root / "cfg";
    fs::path data_dir = tmp_root / "data";
    fs::path state_dir = tmp_root / "state";
    fs::create_directories(cfg_dir / "endpaper");

    {
        std::ofstream f(cfg_dir / "endpaper" / "endpaper.conf");
        f << "[behavior]\nauto_fetch_akonadi = true\nauto_fetch_miniflux = true\nremember_config = false\n"
          << "[state]\nactive_tab = 3\nlast_miniflux_filter = 2\nlast_kmail_collection_id = 98765432109\n";
    }

    const char* prev_cfg = std::getenv("XDG_CONFIG_HOME");
    const char* prev_data = std::getenv("XDG_DATA_HOME");
    const char* prev_state = std::getenv("XDG_STATE_HOME");
    std::string old_cfg = prev_cfg ? prev_cfg : "";
    std::string old_data = prev_data ? prev_data : "";
    std::string old_state = prev_state ? prev_state : "";

    setenv("XDG_CONFIG_HOME", cfg_dir.c_str(), 1);
    setenv("XDG_DATA_HOME", data_dir.c_str(), 1);
    setenv("XDG_STATE_HOME", state_dir.c_str(), 1);

    {
        Config cfg;
        assert(cfg.get_bool("general", "auto_fetch_kmail", false) == true);
        assert(cfg.get_bool("general", "auto_fetch_miniflux", false) == true);
        assert(cfg.get_bool("general", "remember_state", true) == false);
        assert(cfg.get_int("state", "last_page_index", -1) == 3);
        assert(cfg.get_int("state", "last_miniflux_status_filter", -1) == 2);
        assert(cfg.get_int64("state", "last_kmail_collection_id", -1) == 98765432109LL);

        cfg.set("general", "auto_fetch_kmail", "false");
        cfg.save();
        auto mode = fs::status(cfg.config_file()).permissions() & fs::perms::mask;
        assert(mode == (fs::perms::owner_read | fs::perms::owner_write));

        cfg.reload();
        assert(cfg.get_bool("general", "auto_fetch_kmail", true) == false);
    }

    if (prev_cfg) setenv("XDG_CONFIG_HOME", old_cfg.c_str(), 1); else unsetenv("XDG_CONFIG_HOME");
    if (prev_data) setenv("XDG_DATA_HOME", old_data.c_str(), 1); else unsetenv("XDG_DATA_HOME");
    if (prev_state) setenv("XDG_STATE_HOME", old_state.c_str(), 1); else unsetenv("XDG_STATE_HOME");
    fs::remove_all(tmp_root);
}

void test_stage_articles_stub_resolution() {
    fs::path tmp_db = fs::temp_directory_path() / "endpaper_stage_test.db";
    fs::remove(tmp_db);
    {
        Database db(tmp_db);
        db.init();
        int64_t bid = db.create_book("StageBook");

        std::string long_body(1200, 'x');
        std::vector<StageItem> items = {
            {"Stub OK", "Alice", "<p>Short</p>", 1, "https://example.com/1"},
            {"Stub Fail", "Bob", "<p>Tiny</p>", 2, "https://example.com/2"},
            {"Full Article", "Carol", "<p>" + long_body + "</p>", 3, "https://example.com/3"},
        };

        StageOptions opts;
        opts.resolve_stubs = true;
        opts.stub_char_threshold = 500;
        opts.request_delay_ms = 0;

        int resolver_calls = 0;
        StageResult res = stage_articles(db, bid, items, opts, [&](const StageItem& it) -> std::string {
            ++resolver_calls;
            if (it.source_id == 1) return "<p>Expanded full article body.</p>";
            throw std::runtime_error("network down");
        });

        assert(resolver_calls == 2); // Item 3 was already long, skipped resolver
        assert(res.staged == 3);
        assert(res.stub_failures == 1);

        auto chs = db.get_chapters(bid);
        assert(chs.size() == 3);
        assert(chs[0].content_html.find("Expanded full article") != std::string::npos);
        assert(chs[1].content_html == "<p>Tiny</p>");
    }
    fs::remove(tmp_db);
}

void test_build_book_epub_pipeline() {
    fs::path tmp_dir = fs::temp_directory_path() / "endpaper_pipeline_test";
    fs::remove_all(tmp_dir);

    std::vector<Chapter> chaps = {
        {1, 1, "Chapter One", "Alice",
         "<header><img src=\"http://invalid.invalid/logo.png\"/></header>"
         "<div><p>Hello&nbsp;X4</p><br><br><br><br><p>End</p></div>", 1},
        {2, 1, "Chapter Two", "", "<p>Second</p>", 2},
    };
    BuildOptions opts;
    opts.author = "Tester";
    opts.publisher = "Endpaper";
    opts.explicit_path = (tmp_dir / "nested").string();
    opts.simplify = true;
    opts.keep_images = true;

    // simplify/sanitize must remove <header><img> BEFORE process_images_in_dom tries downloading it
    BuildResult res = build_book_epub("Daily Digest", chaps, opts);
    assert(res.chapters_built == 2);
    assert(res.images_embedded == 0);
    assert(res.images_failed == 0);
    assert(res.file_size_bytes > 0);
    assert(res.output_path == (tmp_dir / "nested" / "daily-digest.epub").string());
    assert(fs::exists(res.output_path));
    std::string summary = format_build_summary(res, true);
    assert(summary.find("(2 ch, 0 img") != std::string::npos);
    assert(summary.find("KB)") != std::string::npos);

    fs::remove_all(tmp_dir);
}

int main() {
    std::cout << "╔════════════════════════════════════════════════════╗\n";
    std::cout << "║  Endpaper — Build Pipeline Tests                   ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";

    TEST(test_slugify_and_paths);
    TEST(test_url_entity_decoding);
    TEST(test_optimize_image_for_ereader);
    TEST(test_article_filter);
    TEST(test_config_legacy_migration_int64_and_perms);
    TEST(test_stage_articles_stub_resolution);
    TEST(test_build_book_epub_pipeline);

    std::cout << "\n✓ All build pipeline tests passed!\n";
    return 0;
}
