#include "tui_app.h"
#include "epub_builder.h"
#include "html_sanitizer.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <regex>
#include <thread>

namespace endpaper {

using namespace ftxui;
namespace fs = std::filesystem;

// ══════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════

static std::string slugify(const std::string& s) {
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

// ══════════════════════════════════════════════════════════════════════════
// TuiApp implementation
// ══════════════════════════════════════════════════════════════════════════

TuiApp::TuiApp(Config& config, Database& db, AkonadiFetcher& fetcher, RssFetcher& rss_fetcher, MinifluxFetcher& miniflux_fetcher, HtmlExtractor& html_extractor)
    : config_(config), db_(db), fetcher_(fetcher), rss_fetcher_(rss_fetcher), miniflux_fetcher_(miniflux_fetcher), html_extractor_(html_extractor) {}

void TuiApp::run() {
    auto screen = ScreenInteractive::Fullscreen();

    // ── Shared state ─────────────────────────────────────────────────────
    bool auto_fetch_akonadi = config_.get("behavior", "auto_fetch_akonadi", "false") == "true";
    bool auto_fetch_miniflux = config_.get("behavior", "auto_fetch_miniflux", "false") == "true";
    bool remember_config = config_.get("behavior", "remember_config", "false") == "true";

    int active_tab = 0;
    std::string last_kmail_folder;
    std::string last_miniflux_category;
    std::string last_build_book;
    if (remember_config) {
        try { active_tab = std::stoi(config_.get("state", "active_tab", "0")); } catch(...) {}
        last_kmail_folder = config_.get("state", "last_kmail_folder", "");
        last_miniflux_category = config_.get("state", "last_miniflux_category", "");
        last_build_book = config_.get("state", "last_build_book", "");
    }
    
    bool enable_stub_resolving = config_.get_bool("fetching", "enable_stub_resolving", true);
    std::string stub_threshold_text = std::to_string(config_.get_int("fetching", "stub_char_threshold", 1000));
    std::string request_delay_text = std::to_string(config_.get_int("fetching", "request_delay_ms", 1000));
    
    std::string status_message;
    std::mutex state_mutex;

    // ── KMail tab state ─────────────────────────────────────────────────
    std::vector<FolderInfo> folders;
    std::vector<std::string> folder_labels;
    std::vector<EmailItem> emails;
    std::vector<EmailItem> filtered_emails;
    std::vector<std::string> email_labels;
    std::vector<bool> email_selected;
    int folder_selected = 0;
    int email_focused = 0;
    std::string filter_text;
    std::atomic<bool> loading_folders{false};
    std::atomic<bool> loading_emails{false};

    // ── Miniflux tab state ───────────────────────────────────────────────
    std::vector<MinifluxCategory> miniflux_categories;
    std::vector<std::string> miniflux_category_labels;
    std::vector<MinifluxEntry> miniflux_entries;
    std::vector<MinifluxEntry> filtered_miniflux;
    std::vector<std::string> miniflux_entry_labels;
    std::vector<bool> miniflux_selected;
    int miniflux_category_selected = 0;
    int miniflux_entry_focused = 0;
    int miniflux_status_filter = 0; // 0=Unread, 1=Starred, 2=All
    if (remember_config) {
        try { miniflux_status_filter = std::stoi(config_.get("state", "last_miniflux_filter", "0")); } catch(...) {}
    }
    std::string miniflux_filter_text;
    std::string miniflux_server_url = config_.get("miniflux", "url", "");
    std::string miniflux_token = config_.get("miniflux", "token", "");
    std::atomic<bool> loading_miniflux_categories{false};
    std::atomic<bool> loading_miniflux_entries{false};
    
    // Set initial credentials
    miniflux_fetcher_.set_credentials(miniflux_server_url, miniflux_token);

    // ── RSS tab state ────────────────────────────────────────────────────
    std::vector<RssFeed> rss_feeds;
    std::vector<std::string> feed_labels;
    std::vector<RssItem> rss_articles;
    std::vector<RssItem> filtered_rss;
    std::vector<std::string> rss_article_labels;
    std::vector<bool> rss_selected;
    int feed_selected = 0;
    int rss_article_focused = 0;
    std::string rss_filter_text;
    std::string rss_max_articles = config_.get("rss", "max_articles", "50");
    std::string new_feed_url;
    std::string new_feed_title;
    std::atomic<bool> loading_rss{false};
    std::atomic<bool> loading_feeds{false};

    // Book selector for staging
    std::vector<Book> books;
    std::vector<std::string> book_labels;
    int book_selected = 0;
    std::vector<Chapter> chapters;
    std::vector<std::string> chapter_labels;
    int chapter_selected = 0;
    std::string new_book_title;

    // ── Build tab state ──────────────────────────────────────────────────
    int build_book_selected = 0;
    std::vector<std::string> build_book_labels;
    std::string epub_output_path;
    std::string epub_author;
    std::string epub_publisher;
    std::atomic<bool> building{false};
    std::string build_status;
    bool append_mode = false;
    bool simplify_mode = false;
    bool fuzzy_search = config_.get("ui", "fuzzy_search", "false") == "true";

    // ── Shared Helpers ───────────────────────────────────────────────────

    auto sync_book_labels = [&]() {
        book_labels.clear();
        for (const auto& b : books) {
            book_labels.push_back(b.title + " (" + b.created_at + ")");
        }
        if (book_selected >= static_cast<int>(book_labels.size()))
            book_selected = std::max(0, static_cast<int>(book_labels.size()) - 1);

        build_book_labels.clear();
        for (const auto& b : books) {
            auto ch_count = db_.get_chapters(b.id).size();
            build_book_labels.push_back(b.title + " [" + std::to_string(ch_count) + " ch]");
        }
        if (build_book_selected >= static_cast<int>(build_book_labels.size()))
            build_book_selected = 0;
    };

    auto sync_chapter_labels = [&]() {
        chapter_labels.clear();
        for (const auto& c : chapters) {
            chapter_labels.push_back(std::to_string(c.sort_order) + ". " + c.subject + " — " + c.sender);
        }
        if (chapter_selected >= static_cast<int>(chapter_labels.size()))
            chapter_selected = std::max(0, static_cast<int>(chapter_labels.size()) - 1);
    };

    auto refresh_books = [&]() {
        books = db_.list_books();
        sync_book_labels();
    };

    auto update_epub_path = [&]() {
        if (!books.empty() && build_book_selected >= 0 && build_book_selected < static_cast<int>(books.size())) {
            epub_output_path = "~/Documents/" + slugify(books[build_book_selected].title) + ".epub";
        }
    };

    auto refresh_chapters = [&]() {
        if (!books.empty() && book_selected >= 0 && book_selected < static_cast<int>(books.size())) {
            chapters = db_.get_chapters(books[book_selected].id);
        } else {
            chapters.clear();
        }
        sync_chapter_labels();
    };

    // ── KMail Helpers ───────────────────────────────────────────────────

    auto sync_folder_labels = [&]() {
        folder_labels.clear();
        for (const auto& f : folders) {
            folder_labels.push_back(f.full_path);
        }
        if (folder_selected >= static_cast<int>(folder_labels.size()))
            folder_selected = std::max(0, static_cast<int>(folder_labels.size()) - 1);
    };

    auto apply_email_filter = [&]() {
        filtered_emails.clear();
        email_labels.clear();

        std::regex re;
        bool use_filter = !filter_text.empty();
        std::vector<std::string> fuzzy_words;

        if (use_filter) {
            if (fuzzy_search) {
                std::istringstream iss(filter_text);
                std::string w;
                while (iss >> w) {
                    std::transform(w.begin(), w.end(), w.begin(), ::tolower);
                    fuzzy_words.push_back(w);
                }
            } else {
                try { re = std::regex(filter_text, std::regex::icase); }
                catch (...) { use_filter = false; }
            }
        }

        for (size_t i = 0; i < emails.size(); ++i) {
            if (use_filter) {
                std::string haystack_lower = emails[i].subject + " " + emails[i].sender;
                std::transform(haystack_lower.begin(), haystack_lower.end(), haystack_lower.begin(), ::tolower);
                if (fuzzy_search) {
                    bool match = true;
                    for (const auto& w : fuzzy_words) {
                        if (haystack_lower.find(w) == std::string::npos) { match = false; break; }
                    }
                    if (!match) continue;
                } else {
                    if (!std::regex_search(emails[i].subject, re) && !std::regex_search(emails[i].sender, re)) continue;
                }
            }
            bool selected = (i < email_selected.size() && email_selected[i]);
            email_labels.push_back((selected ? "◉ " : "○ ") + emails[i].subject + " | " + emails[i].sender);
            filtered_emails.push_back(emails[i]);
        }
        if (email_focused >= static_cast<int>(email_labels.size()))
            email_focused = std::max(0, static_cast<int>(email_labels.size()) - 1);
    };

    // ── Miniflux Helpers ────────────────────────────────────────────────

    auto sync_miniflux_category_labels = [&]() {
        miniflux_category_labels.clear();
        miniflux_category_labels.push_back("All Categories");
        for (const auto& cat : miniflux_categories) {
            miniflux_category_labels.push_back(cat.title + " (" + std::to_string(cat.unread_count) + ")");
        }
        if (miniflux_category_selected >= static_cast<int>(miniflux_category_labels.size()))
            miniflux_category_selected = 0;
    };

    auto apply_miniflux_filter = [&]() {
        filtered_miniflux.clear();
        miniflux_entry_labels.clear();

        std::regex re;
        bool use_filter = !miniflux_filter_text.empty();
        std::vector<std::string> fuzzy_words;

        if (use_filter) {
            if (fuzzy_search) {
                std::istringstream iss(miniflux_filter_text);
                std::string w;
                while (iss >> w) {
                    std::transform(w.begin(), w.end(), w.begin(), ::tolower);
                    fuzzy_words.push_back(w);
                }
            } else {
                try { re = std::regex(miniflux_filter_text, std::regex::icase); }
                catch (...) { use_filter = false; }
            }
        }

        for (size_t i = 0; i < miniflux_entries.size(); ++i) {
            if (use_filter) {
                std::string haystack_lower = miniflux_entries[i].title + " " + miniflux_entries[i].author;
                std::transform(haystack_lower.begin(), haystack_lower.end(), haystack_lower.begin(), ::tolower);
                if (fuzzy_search) {
                    bool match = true;
                    for (const auto& w : fuzzy_words) {
                        if (haystack_lower.find(w) == std::string::npos) { match = false; break; }
                    }
                    if (!match) continue;
                } else {
                    if (!std::regex_search(miniflux_entries[i].title, re) && !std::regex_search(miniflux_entries[i].author, re)) continue;
                }
            }
            bool selected = (i < miniflux_selected.size() && miniflux_selected[i]);
            miniflux_entry_labels.push_back((selected ? "◉ " : "○ ") + miniflux_entries[i].title + " | " + miniflux_entries[i].author);
            filtered_miniflux.push_back(miniflux_entries[i]);
        }
        if (miniflux_entry_focused >= static_cast<int>(miniflux_entry_labels.size()))
            miniflux_entry_focused = std::max(0, static_cast<int>(miniflux_entry_labels.size()) - 1);
    };

    // ── RSS Helpers ─────────────────────────────────────────────────────

    auto sync_rss_labels = [&]() {
        feed_labels.clear();
        for (const auto& f : rss_feeds) {
            feed_labels.push_back(f.title + " (" + f.url + ")");
        }
        if (feed_selected >= static_cast<int>(feed_labels.size()))
            feed_selected = std::max(0, static_cast<int>(feed_labels.size()) - 1);
    };

    auto apply_rss_filter = [&]() {
        filtered_rss.clear();
        rss_article_labels.clear();
        std::regex re;
        bool use_filter = !rss_filter_text.empty();
        std::vector<std::string> fuzzy_words;

        if (use_filter) {
            if (fuzzy_search) {
                std::istringstream iss(rss_filter_text);
                std::string w;
                while (iss >> w) {
                    std::transform(w.begin(), w.end(), w.begin(), ::tolower);
                    fuzzy_words.push_back(w);
                }
            } else {
                try { re = std::regex(rss_filter_text, std::regex::icase); }
                catch (...) { use_filter = false; }
            }
        }

        for (size_t i = 0; i < rss_articles.size(); ++i) {
            if (use_filter) {
                std::string haystack_lower = rss_articles[i].title + " " + rss_articles[i].author;
                std::transform(haystack_lower.begin(), haystack_lower.end(), haystack_lower.begin(), ::tolower);
                if (fuzzy_search) {
                    bool match = true;
                    for (const auto& w : fuzzy_words) {
                        if (haystack_lower.find(w) == std::string::npos) { match = false; break; }
                    }
                    if (!match) continue;
                } else {
                    if (!std::regex_search(rss_articles[i].title, re) && !std::regex_search(rss_articles[i].author, re)) continue;
                }
            }
            bool selected = (i < rss_selected.size() && rss_selected[i]);
            rss_article_labels.push_back((selected ? "◉ " : "○ ") + rss_articles[i].title + " | " + rss_articles[i].author);
            filtered_rss.push_back(rss_articles[i]);
        }
        if (rss_article_focused >= static_cast<int>(rss_article_labels.size()))
            rss_article_focused = std::max(0, static_cast<int>(rss_article_labels.size()) - 1);
    };

    // Initial load
    refresh_books();
    refresh_chapters();
    rss_feeds = db_.list_rss_feeds();
    sync_rss_labels();
    epub_author = config_.get("epub", "author", "Unknown");
    epub_publisher = config_.get("epub", "publisher", "Endpaper");
    if (!books.empty()) {
        if (remember_config && !last_build_book.empty()) {
            for (size_t i = 0; i < books.size(); ++i) {
                if (books[i].title == last_build_book) { build_book_selected = i; break; }
            }
        }
        epub_output_path = "~/Documents/" + slugify(books[build_book_selected].title) + ".epub";
    }

    // ══════════════════════════════════════════════════════════════════════
    //  TAB 0: KMAIL
    // ══════════════════════════════════════════════════════════════════════

    auto folder_menu = Menu(&folder_labels, &folder_selected);
    auto email_menu_option = MenuOption::Vertical();
    email_menu_option.entries_option.transform = [&](EntryState resp) {
        auto e = text(resp.label);
        if (resp.label.substr(0, 3) == "◉ ") e = e | color(Color::Green) | bold;
        if (resp.focused) e = e | inverted;
        return e;
    };
    auto email_menu = Menu(&email_labels, &email_focused, email_menu_option);

    auto do_fetch_folders = [&] {
        if (loading_folders) return;
        loading_folders = true;
        status_message = "Fetching folders from Akonadi...";
        auto fut = fetcher_.fetch_folders();
        std::thread([&, fut = std::move(fut)]() mutable {
            try {
                auto result = fut.get();
                std::lock_guard lock(state_mutex);
                folders = std::move(result);
                folder_selected = 0;
                if (remember_config && !last_kmail_folder.empty()) {
                    for (size_t i = 0; i < folders.size(); ++i) {
                        if (folders[i].full_path == last_kmail_folder) { folder_selected = i; break; }
                    }
                }
                sync_folder_labels();
                status_message = "Loaded " + std::to_string(folders.size()) + " folders";
            } catch (const std::exception& e) {
                status_message = std::string("Error: ") + e.what();
            }
            loading_folders = false;
            screen.Post(Event::Custom);
        }).detach();
    };
    auto btn_fetch_folders = Button("⟳ Fetch Folders", do_fetch_folders);

    auto btn_fetch_emails = Button("📧 Fetch Emails", [&] {
        if (loading_emails || folders.empty()) return;
        loading_emails = true;
        int64_t col_id = folders[folder_selected].collection_id;
        status_message = "Fetching emails...";
        auto fut = fetcher_.fetch_emails(col_id);
        std::thread([&, fut = std::move(fut)]() mutable {
            try {
                auto result = fut.get();
                std::lock_guard lock(state_mutex);
                emails = std::move(result);
                email_selected.assign(emails.size(), false);
                email_focused = 0;
                apply_email_filter();
                status_message = "Loaded " + std::to_string(emails.size()) + " emails";
            } catch (const std::exception& e) {
                status_message = std::string("Error: ") + e.what();
            }
            loading_emails = false;
            screen.Post(Event::Custom);
        }).detach();
    });

    auto btn_stage_kmail = Button("✓ Stage Selected", [&] {
        if (books.empty()) { status_message = "Create a book first in the Books tab!"; return; }
        int64_t book_id = books[build_book_selected].id;
        int base_order = db_.max_sort_order(book_id);
        int staged = 0;
        for (size_t i = 0; i < emails.size(); ++i) {
            if (email_selected[i]) {
                db_.add_chapter(book_id, emails[i].subject, emails[i].sender, sanitize_html(emails[i].html_body), base_order + staged + 1);
                staged++;
            }
        }
        status_message = "Staged " + std::to_string(staged) + " emails";
        email_selected.assign(emails.size(), false);
        apply_email_filter();
        db_.renumber_chapters(book_id);
        refresh_chapters();
    });

    auto btn_kmail_select_all = Button("☑ All", [&] {
        for (const auto& fe : filtered_emails) {
            for (size_t i = 0; i < emails.size(); ++i) {
                if (emails[i].akonadi_id == fe.akonadi_id) { email_selected[i] = true; break; }
            }
        }
        apply_email_filter();
    });

    auto btn_kmail_deselect_all = Button("☐ None", [&] {
        email_selected.assign(emails.size(), false);
        apply_email_filter();
    });

    auto input_kmail_filter = Input(&filter_text, "Filter...");
    auto kmail_filter_wrapper = CatchEvent(input_kmail_filter, [&](Event e) {
        if (input_kmail_filter->OnEvent(e)) { apply_email_filter(); return true; }
        return false;
    });

    auto email_toggle = CatchEvent(email_menu, [&](Event event) {
        if ((event == Event::Character(' ') || event == Event::Return) && !filtered_emails.empty() && email_focused >= 0 && email_focused < (int)filtered_emails.size()) {
            int64_t tid = filtered_emails[email_focused].akonadi_id;
            for (size_t i = 0; i < emails.size(); ++i) { if (emails[i].akonadi_id == tid) { email_selected[i] = !email_selected[i]; break; } }
            apply_email_filter();
            return true;
        }
        return false;
    });

    DropdownOption kmail_book_opt;
    kmail_book_opt.radiobox.entries = &build_book_labels;
    kmail_book_opt.radiobox.selected = &build_book_selected;
    kmail_book_opt.radiobox.on_change = [&] { update_epub_path(); };
    auto kmail_book_dropdown = Dropdown(kmail_book_opt);

    auto kmail_tab = Container::Vertical({
        Container::Horizontal({btn_fetch_folders, btn_fetch_emails, kmail_filter_wrapper}),
        folder_menu,
        Container::Horizontal({btn_kmail_select_all, btn_kmail_deselect_all, btn_stage_kmail, kmail_book_dropdown}),
        email_toggle
    });

    auto kmail_renderer = Renderer(kmail_tab, [&] {
        int kmail_sel_count = 0;
        for (bool s : email_selected) if (s) kmail_sel_count++;

        Elements cols;
        cols.push_back(btn_fetch_folders->Render());
        cols.push_back(text(" "));
        cols.push_back(btn_fetch_emails->Render());
        cols.push_back(text("  Filter: "));
        cols.push_back(kmail_filter_wrapper->Render() | flex);

        Elements foot;
        foot.push_back(btn_kmail_select_all->Render());
        foot.push_back(btn_kmail_deselect_all->Render());
        foot.push_back(btn_stage_kmail->Render());
        foot.push_back(text(" (" + std::to_string(kmail_sel_count) + ")") | bold);
        foot.push_back(text("  → Book: "));
        foot.push_back(kmail_book_dropdown->Render());

        Elements rows;
        rows.push_back(text("📧 KMAIL INGEST") | bold | center);
        rows.push_back(separator());
        rows.push_back(hbox(std::move(cols)));
        rows.push_back(separator());
        rows.push_back(text("Folders:") | bold);
        rows.push_back(folder_menu->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 6) | border);
        rows.push_back(separator());
        rows.push_back(hbox(std::move(foot)));
        rows.push_back(separator());
        rows.push_back(email_toggle->Render() | vscroll_indicator | frame | flex | border);
        if (loading_emails.load()) rows.push_back(text("⏳ Loading...") | blink | center);

        return vbox(std::move(rows));
    });

    // ══════════════════════════════════════════════════════════════════════
    //  TAB 1: MINIFLUX
    // ══════════════════════════════════════════════════════════════════════

    auto miniflux_category_menu = Menu(&miniflux_category_labels, &miniflux_category_selected);
    auto miniflux_entry_option = MenuOption::Vertical();
    miniflux_entry_option.entries_option.transform = [&](EntryState resp) {
        auto e = text(resp.label);
        if (resp.label.substr(0, 3) == "◉ ") e = e | color(Color::Cyan) | bold;
        if (resp.focused) e = e | inverted;
        return e;
    };
    auto miniflux_entry_menu = Menu(&miniflux_entry_labels, &miniflux_entry_focused, miniflux_entry_option);

    auto do_fetch_mini_cats = [&] {
        if (miniflux_server_url.empty()) { status_message = "Set Miniflux URL in Settings!"; return; }
        loading_miniflux_categories = true;
        auto fut = miniflux_fetcher_.fetch_categories();
        std::thread([&, fut = std::move(fut)]() mutable {
            try {
                auto res = fut.get();
                std::lock_guard lock(state_mutex);
                miniflux_categories = std::move(res);
                if (remember_config && !last_miniflux_category.empty()) {
                    for (size_t i = 0; i < miniflux_categories.size(); ++i) {
                        if (miniflux_categories[i].title == last_miniflux_category) {
                            miniflux_category_selected = i + 1; break;
                        }
                    }
                }
                sync_miniflux_category_labels();
                status_message = "Loaded " + std::to_string(miniflux_categories.size()) + " categories";
            } catch (const std::exception& e) { status_message = e.what(); }
            loading_miniflux_categories = false;
            screen.Post(Event::Custom);
        }).detach();
    };
    auto btn_fetch_mini_cats = Button("⟳ Fetch Categories", do_fetch_mini_cats);

    auto btn_fetch_mini_entries = Button("🌀 Fetch Entries", [&] {
        if (loading_miniflux_entries) return;
        loading_miniflux_entries = true;
        MinifluxFetcher::FetchParams p;
        if (miniflux_category_selected > 0) p.category_id = miniflux_categories[miniflux_category_selected - 1].id;
        if (miniflux_status_filter == 0) p.status = "unread";
        else if (miniflux_status_filter == 1) p.starred = true;

        status_message = "Fetching entries...";
        auto fut = miniflux_fetcher_.fetch_entries(p);
        std::thread([&, fut = std::move(fut)]() mutable {
            try {
                auto res = fut.get();
                std::lock_guard lock(state_mutex);
                miniflux_entries = std::move(res);
                miniflux_selected.assign(miniflux_entries.size(), false);
                miniflux_entry_focused = 0;
                apply_miniflux_filter();
                status_message = "Loaded " + std::to_string(miniflux_entries.size()) + " entries";
            } catch (const std::exception& e) { status_message = e.what(); }
            loading_miniflux_entries = false;
            screen.Post(Event::Custom);
        }).detach();
    });

    auto filter_labels_mini = std::vector<std::string>{"Unread", "Starred", "All"};
    auto filter_toggle_mini = Toggle(&filter_labels_mini, &miniflux_status_filter);

    auto btn_stage_mini = Button("✓ Stage Selected", [&] {
        if (books.empty()) { status_message = "Create a book first!"; return; }
        if (loading_miniflux_entries) return;
        
        std::vector<MinifluxEntry> selected_entries;
        for (size_t i = 0; i < miniflux_entries.size(); ++i) {
            if (miniflux_selected[i]) selected_entries.push_back(miniflux_entries[i]);
        }
        if (selected_entries.empty()) return;

        loading_miniflux_entries = true;
        int64_t book_id = books[build_book_selected].id;
        status_message = "Staging (fetching full content)...";

        std::thread([&, selected_entries = std::move(selected_entries), book_id]() {
            int base_order = db_.max_sort_order(book_id);
            int staged = 0;

            bool resolve_stubs = config_.get_bool("fetching", "enable_stub_resolving", true);
            size_t threshold = config_.get_int("fetching", "stub_char_threshold", 1000);
            int delay_ms = config_.get_int("fetching", "request_delay_ms", 1000);

            for (const auto& ent : selected_entries) {
                std::string content = ent.content;
                if (resolve_stubs && content.size() < threshold) {
                    {
                        std::lock_guard lock(state_mutex);
                        status_message = "Staging [" + std::to_string(staged+1) + "/" + std::to_string(selected_entries.size()) + "]: Resolving stub for " + ent.title;
                    }
                    screen.Post(Event::Custom);

                    if (staged > 0 && delay_ms > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                    }

                    try {
                        auto fut = miniflux_fetcher_.fetch_original_content(ent.id);
                        std::string full_content = fut.get();
                        if (!full_content.empty()) content = full_content;
                    } catch (...) {}
                } else {
                    std::lock_guard lock(state_mutex);
                    status_message = "Staging [" + std::to_string(staged+1) + "/" + std::to_string(selected_entries.size()) + "]: " + ent.title;
                    screen.Post(Event::Custom);
                }

                {
                    std::lock_guard lock(state_mutex);
                    db_.add_chapter(book_id, ent.title, ent.author, sanitize_html(content), base_order + staged + 1);
                    staged++;
                }
            }
            {
                std::lock_guard lock(state_mutex);
                status_message = "✓ Staged " + std::to_string(staged) + " entries";
                miniflux_selected.assign(miniflux_entries.size(), false);
                loading_miniflux_entries = false;
                apply_miniflux_filter();
                db_.renumber_chapters(book_id);
                refresh_chapters();
            }
            screen.Post(Event::Custom);
        }).detach();
    });

    auto btn_mini_select_all = Button("☑ All", [&] {
        for (const auto& fe : filtered_miniflux) {
            for (size_t i = 0; i < miniflux_entries.size(); ++i) {
                if (miniflux_entries[i].id == fe.id) { miniflux_selected[i] = true; break; }
            }
        }
        apply_miniflux_filter();
    });

    auto btn_mini_deselect_all = Button("☐ None", [&] {
        miniflux_selected.assign(miniflux_entries.size(), false);
        apply_miniflux_filter();
    });

    auto input_mini_filter = Input(&miniflux_filter_text, "Search...");
    auto mini_filter_wrapper = CatchEvent(input_mini_filter, [&](Event e) {
        if (input_mini_filter->OnEvent(e)) { apply_miniflux_filter(); return true; }
        return false;
    });

    auto mini_entry_toggle = CatchEvent(miniflux_entry_menu, [&](Event event) {
        if ((event == Event::Character(' ') || event == Event::Return) && !filtered_miniflux.empty() && miniflux_entry_focused >= 0 && miniflux_entry_focused < (int)filtered_miniflux.size()) {
            int64_t tid = filtered_miniflux[miniflux_entry_focused].id;
            for (size_t i = 0; i < miniflux_entries.size(); ++i) { if (miniflux_entries[i].id == tid) { miniflux_selected[i] = !miniflux_selected[i]; break; } }
            apply_miniflux_filter();
            return true;
        }
        return false;
    });

    DropdownOption miniflux_book_opt;
    miniflux_book_opt.radiobox.entries = &build_book_labels;
    miniflux_book_opt.radiobox.selected = &build_book_selected;
    miniflux_book_opt.radiobox.on_change = [&] { update_epub_path(); };
    auto miniflux_book_dropdown = Dropdown(miniflux_book_opt);

    auto miniflux_tab = Container::Vertical({
        Container::Horizontal({btn_fetch_mini_cats, btn_fetch_mini_entries, filter_toggle_mini, mini_filter_wrapper}),
        miniflux_category_menu,
        Container::Horizontal({btn_mini_select_all, btn_mini_deselect_all, btn_stage_mini, miniflux_book_dropdown}),
        mini_entry_toggle
    });

    auto miniflux_renderer = Renderer(miniflux_tab, [&] {
        int mini_sel_count = 0;
        for (bool s : miniflux_selected) if (s) mini_sel_count++;

        Elements head;
        head.push_back(btn_fetch_mini_cats->Render());
        head.push_back(text(" "));
        head.push_back(btn_fetch_mini_entries->Render());
        head.push_back(separator());
        head.push_back(filter_toggle_mini->Render());
        head.push_back(text("  Search: "));
        head.push_back(mini_filter_wrapper->Render() | flex);

        Elements foot;
        foot.push_back(btn_mini_select_all->Render());
        foot.push_back(btn_mini_deselect_all->Render());
        foot.push_back(btn_stage_mini->Render());
        foot.push_back(text(" (" + std::to_string(mini_sel_count) + ")") | bold);
        foot.push_back(text("  → Book: "));
        foot.push_back(miniflux_book_dropdown->Render());

        Elements rows;
        rows.push_back(text("🌀 MINIFLUX INGEST") | bold | center);
        rows.push_back(separator());
        rows.push_back(hbox(std::move(head)));
        rows.push_back(separator());
        rows.push_back(text("Categories:") | bold);
        rows.push_back(miniflux_category_menu->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 6) | border);
        rows.push_back(separator());
        rows.push_back(hbox(std::move(foot)));
        rows.push_back(separator());
        rows.push_back(mini_entry_toggle->Render() | vscroll_indicator | frame | flex | border);
        if (loading_miniflux_entries.load()) rows.push_back(text("⏳ Loading...") | blink | center);

        return vbox(std::move(rows));
    });

    // ══════════════════════════════════════════════════════════════════════
    //  TAB 2: RSS
    // ══════════════════════════════════════════════════════════════════════

    auto rss_feed_menu = Menu(&feed_labels, &feed_selected);
    auto rss_article_menu_option = MenuOption::Vertical();
    rss_article_menu_option.entries_option.transform = [&](EntryState resp) {
        auto e = text(resp.label);
        if (resp.label.substr(0, 3) == "◉ ") e = e | color(Color::Green) | bold;
        if (resp.focused) e = e | inverted;
        return e;
    };
    auto rss_article_menu = Menu(&rss_article_labels, &rss_article_focused, rss_article_menu_option);

    auto btn_add_rss_feed = Button("+ Add Feed", [&] {
        if (new_feed_url.empty() || new_feed_title.empty()) return;
        try { db_.add_rss_feed(new_feed_title, new_feed_url); new_feed_url.clear(); new_feed_title.clear(); rss_feeds = db_.list_rss_feeds(); sync_rss_labels(); status_message = "Feed added!"; }
        catch (const std::exception& e) { status_message = e.what(); }
    });

    auto btn_remove_rss_feed = Button("🗑 Remove", [&] {
        if (rss_feeds.empty()) return;
        db_.delete_rss_feed(rss_feeds[feed_selected].id);
        rss_feeds = db_.list_rss_feeds(); sync_rss_labels(); status_message = "Feed removed";
    });

    auto btn_fetch_rss = Button("🌐 Fetch Articles", [&] {
        if (loading_rss || rss_feeds.empty()) return;
        loading_rss = true;
        size_t max_items = 0; try { max_items = std::stoul(rss_max_articles); } catch (...) {}
        status_message = "Fetching RSS...";
        auto fut = rss_fetcher_.fetch_items(rss_feeds[feed_selected].url, max_items);
        std::thread([&, fut = std::move(fut)]() mutable {
            try {
                auto res = fut.get();
                std::lock_guard lock(state_mutex);
                rss_articles = std::move(res);
                rss_selected.assign(rss_articles.size(), false);
                rss_article_focused = 0;
                apply_rss_filter();
                status_message = "Loaded " + std::to_string(rss_articles.size()) + " articles";
            } catch (const std::exception& e) { status_message = e.what(); }
            loading_rss = false;
            screen.Post(Event::Custom);
        }).detach();
    });

    auto btn_stage_rss = Button("✓ Stage Selected", [&] {
        if (books.empty()) { status_message = "Create a book first!"; return; }
        if (loading_rss) return;

        std::vector<RssItem> selected_articles;
        for (size_t i = 0; i < rss_articles.size(); ++i) {
            if (rss_selected[i]) selected_articles.push_back(rss_articles[i]);
        }
        if (selected_articles.empty()) return;

        loading_rss = true;
        int64_t book_id = books[build_book_selected].id;
        status_message = "Staging (fetching full content)...";

        std::thread([&, selected_articles = std::move(selected_articles), book_id]() {
            int base_order = db_.max_sort_order(book_id);
            int staged = 0;

            bool resolve_stubs = config_.get_bool("fetching", "enable_stub_resolving", true);
            size_t threshold = config_.get_int("fetching", "stub_char_threshold", 1000);
            int delay_ms = config_.get_int("fetching", "request_delay_ms", 1000);

            for (const auto& art : selected_articles) {
                std::string content = art.content_html;
                if (resolve_stubs && content.size() < threshold && !art.link.empty()) {
                    {
                        std::lock_guard lock(state_mutex);
                        status_message = "Staging [" + std::to_string(staged+1) + "/" + std::to_string(selected_articles.size()) + "]: Resolving stub for " + art.title;
                    }
                    screen.Post(Event::Custom);

                    if (staged > 0 && delay_ms > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                    }

                    try {
                        auto fut = html_extractor_.fetch_and_extract(art.link);
                        std::string full_content = fut.get();
                        if (!full_content.empty()) content = full_content;
                    } catch (...) {}
                } else {
                    std::lock_guard lock(state_mutex);
                    status_message = "Staging [" + std::to_string(staged+1) + "/" + std::to_string(selected_articles.size()) + "]: " + art.title;
                    screen.Post(Event::Custom);
                }

                {
                    std::lock_guard lock(state_mutex);
                    db_.add_chapter(book_id, art.title, art.author, sanitize_html(content), base_order + staged + 1);
                    staged++;
                }
            }
            {
                std::lock_guard lock(state_mutex);
                status_message = "✓ Staged " + std::to_string(staged) + " articles";
                rss_selected.assign(rss_articles.size(), false);
                loading_rss = false;
                apply_rss_filter();
                db_.renumber_chapters(book_id);
                refresh_chapters();
            }
            screen.Post(Event::Custom);
        }).detach();
    });

    auto btn_rss_select_all = Button("☑ All", [&] {
        for (const auto& fr : filtered_rss) {
            for (size_t i = 0; i < rss_articles.size(); ++i) {
                if (rss_articles[i].link == fr.link) { rss_selected[i] = true; break; }
            }
        }
        apply_rss_filter();
    });

    auto btn_rss_deselect_all = Button("☐ None", [&] {
        rss_selected.assign(rss_articles.size(), false);
        apply_rss_filter();
    });

    auto input_rss_filter = Input(&rss_filter_text, "Filter...");
    auto rss_filter_wrapper = CatchEvent(input_rss_filter, [&](Event e) {
        if (input_rss_filter->OnEvent(e)) { apply_rss_filter(); return true; }
        return false;
    });

    auto rss_toggle = CatchEvent(rss_article_menu, [&](Event event) {
        if ((event == Event::Character(' ') || event == Event::Return) && !filtered_rss.empty() && rss_article_focused >= 0 && rss_article_focused < (int)filtered_rss.size()) {
            std::string tlink = filtered_rss[rss_article_focused].link;
            for (size_t i = 0; i < rss_articles.size(); ++i) { if (rss_articles[i].link == tlink) { rss_selected[i] = !rss_selected[i]; break; } }
            apply_rss_filter(); return true;
        }
        return false;
    });

    DropdownOption rss_book_opt;
    rss_book_opt.radiobox.entries = &build_book_labels;
    rss_book_opt.radiobox.selected = &build_book_selected;
    rss_book_opt.radiobox.on_change = [&] { update_epub_path(); };
    auto rss_book_dropdown = Dropdown(rss_book_opt);

    auto rss_tab = Container::Vertical({
        Container::Horizontal({Input(&new_feed_title, "Title"), Input(&new_feed_url, "URL"), btn_add_rss_feed}),
        rss_feed_menu,
        Container::Horizontal({btn_fetch_rss, btn_remove_rss_feed, rss_filter_wrapper}),
        Container::Horizontal({btn_rss_select_all, btn_rss_deselect_all, btn_stage_rss, rss_book_dropdown}),
        rss_toggle
    });

    auto rss_renderer = Renderer(rss_tab, [&] {
        int rss_sel_count = 0;
        for (bool s : rss_selected) if (s) rss_sel_count++;

        Elements row1;
        row1.push_back(text("Add: "));
        row1.push_back(rss_tab->ChildAt(0)->Render());

        Elements feed_row;
        feed_row.push_back(rss_feed_menu->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 6) | border | flex);
        feed_row.push_back(btn_remove_rss_feed->Render() | center);

        Elements search_row;
        search_row.push_back(btn_fetch_rss->Render());
        search_row.push_back(text("  Filter: "));
        search_row.push_back(rss_filter_wrapper->Render() | flex);

        Elements stage_row;
        stage_row.push_back(btn_rss_select_all->Render());
        stage_row.push_back(btn_rss_deselect_all->Render());
        stage_row.push_back(btn_stage_rss->Render());
        stage_row.push_back(text(" (" + std::to_string(rss_sel_count) + ")") | bold);
        stage_row.push_back(text("  → Book: "));
        stage_row.push_back(rss_book_dropdown->Render());

        Elements rows;
        rows.push_back(text("🌐 RSS INGEST") | bold | center);
        rows.push_back(separator());
        rows.push_back(hbox(std::move(row1)));
        rows.push_back(separator());
        rows.push_back(text("Feeds:") | bold);
        rows.push_back(hbox(std::move(feed_row)));
        rows.push_back(separator());
        rows.push_back(hbox(std::move(search_row)));
        rows.push_back(separator());
        rows.push_back(hbox(std::move(stage_row)));
        rows.push_back(separator());
        rows.push_back(rss_toggle->Render() | vscroll_indicator | frame | flex | border);
        if (loading_rss.load()) rows.push_back(text("⏳ Loading...") | blink | center);

        return vbox(std::move(rows));
    });

    // ══════════════════════════════════════════════════════════════════════
    //  TAB 3: BOOKS
    // ══════════════════════════════════════════════════════════════════════

    auto book_menu_option = MenuOption::Vertical();
    book_menu_option.on_change = [&] { refresh_chapters(); };
    auto book_menu = Menu(&book_labels, &book_selected, book_menu_option);
    book_menu->TakeFocus();

    auto chapter_menu_option = MenuOption::Vertical();
    chapter_menu_option.on_enter = [&] {
        if (!chapters.empty()) {
            db_.reverse_chapter_order(books[book_selected].id);
            refresh_chapters();
        }
    };
    auto chapter_menu = Menu(&chapter_labels, &chapter_selected, chapter_menu_option);

    auto chapter_menu_wrapper = CatchEvent(chapter_menu, [&](Event e) {
        if (e == Event::Character('r') || e == Event::Character('R')) {
            if (!books.empty() && book_selected >= 0) {
                db_.reverse_chapter_order(books[book_selected].id);
                refresh_chapters();
                return true;
            }
        }
        return chapter_menu->OnEvent(e);
    });

    auto btn_create_book = Button("+ Create Book", [&] {
        if (new_book_title.empty()) return;
        db_.create_book(new_book_title); new_book_title.clear(); refresh_books(); status_message = "Book created!";
    });

    auto btn_delete_book = Button("🗑 Delete", [&] {
        if (books.empty()) return;
        db_.delete_book(books[book_selected].id); refresh_books(); refresh_chapters(); status_message = "Book deleted";
    });

    auto btn_move_up = Button("▲ Up", [&] {
        if (chapters.size() < 2 || chapter_selected <= 0) return;
        int i = chapter_selected;
        db_.update_sort_order(chapters[i].id, chapters[i-1].sort_order);
        db_.update_sort_order(chapters[i-1].id, chapters[i].sort_order);
        db_.renumber_chapters(books[book_selected].id);
        chapter_selected--; refresh_chapters();
    });

    auto btn_move_down = Button("▼ Down", [&] {
        if (chapters.size() < 2 || chapter_selected >= (int)chapters.size() - 1) return;
        int i = chapter_selected;
        db_.update_sort_order(chapters[i].id, chapters[i+1].sort_order);
        db_.update_sort_order(chapters[i+1].id, chapters[i].sort_order);
        db_.renumber_chapters(books[book_selected].id);
        chapter_selected++; refresh_chapters();
    });

    auto btn_delete_chapter = Button("✕ Remove", [&] {
        if (chapters.empty()) return;
        db_.delete_chapter(chapters[chapter_selected].id);
        db_.renumber_chapters(books[book_selected].id);
        refresh_chapters(); status_message = "Chapter removed";
    });

    auto btn_reverse_chapters = Button("⇅ Reverse", [&] {
        if (books.empty()) return;
        db_.reverse_chapter_order(books[book_selected].id);
        refresh_chapters(); status_message = "Order reversed";
    });

    auto books_tab = Container::Horizontal({
        Container::Vertical({book_menu, Container::Horizontal({Input(&new_book_title, "New Book"), btn_create_book}), btn_delete_book}),
        Container::Vertical({chapter_menu_wrapper, Container::Horizontal({btn_move_up, btn_move_down, btn_reverse_chapters, btn_delete_chapter})})
    });

    auto books_renderer = Renderer(books_tab, [&] {
        Elements left_rows;
        left_rows.push_back(text("Books") | bold);
        left_rows.push_back(book_menu->Render() | vscroll_indicator | frame | flex);
        left_rows.push_back(separator());
        Elements create_row;
        create_row.push_back(books_tab->ChildAt(0)->ChildAt(1)->Render());
        create_row.push_back(btn_create_book->Render());
        left_rows.push_back(hbox(std::move(create_row)));
        left_rows.push_back(btn_delete_book->Render());

        Elements right_rows;
        right_rows.push_back(text("Chapters") | bold);
        right_rows.push_back(chapter_menu->Render() | vscroll_indicator | frame | flex);
        right_rows.push_back(separator());
        right_rows.push_back(books_tab->ChildAt(1)->ChildAt(1)->Render());

        Elements cols;
        cols.push_back(vbox(std::move(left_rows)) | flex | border);
        cols.push_back(vbox(std::move(right_rows)) | flex | border);

        return hbox(std::move(cols));
    });

    // ══════════════════════════════════════════════════════════════════════
    //  TAB 4: BUILD
    // ══════════════════════════════════════════════════════════════════════

    auto build_menu_option = MenuOption::Vertical();
    build_menu_option.on_change = [&] { update_epub_path(); };
    auto build_book_menu = Menu(&build_book_labels, &build_book_selected, build_menu_option);
    auto input_output = Input(&epub_output_path, "Path...");
    auto input_author = Input(&epub_author, "Author");
    auto input_publisher = Input(&epub_publisher, "Publisher");
    auto check_append = Checkbox("Append", &append_mode);
    auto check_simplify = Checkbox("Simplify", &simplify_mode);
    
    auto btn_build = Button("📖 BUILD EPUB", [&] {
        if (building || books.empty() || epub_output_path.empty()) return;
        building = true; build_status = "Building...";
        int64_t bid = books[build_book_selected].id;
        std::string title = books[build_book_selected].title;
        std::string opath = epub_output_path;
        if (!opath.empty() && opath[0] == '~') { 
            const char* h = std::getenv("HOME"); 
            if (h) opath = std::string(h) + opath.substr(1); 
        }
        auto chaps = db_.get_chapters(bid);
        std::thread([&, chaps = std::move(chaps), title, opath]() {
            try {
                std::vector<EpubChapter> ec;
                for (const auto& c : chaps) {
                    std::string content = simplify_mode ? simplify_html(c.content_html) : c.content_html;
                    ec.push_back(EpubChapter{c.subject, c.sender, content});
                }
                EpubMetadata m; m.title = title; m.author = epub_author; m.publisher = epub_publisher;
                if (append_mode && fs::exists(opath)) EpubBuilder::append(opath, ec);
                else EpubBuilder::build(opath, m, ec);
                build_status = "✓ Success: " + opath;
            } catch (const std::exception& e) { build_status = std::string("✗ Error: ") + e.what(); }
            building = false; screen.Post(Event::Custom);
        }).detach();
    });

    auto build_tab = Container::Vertical({build_book_menu, input_output, input_author, input_publisher, check_append, check_simplify, btn_build});
    auto build_renderer = Renderer(build_tab, [&] {
        Elements rows;
        rows.push_back(text("📖 BUILD") | bold | center);
        rows.push_back(separator());
        rows.push_back(text("Select Book:") | bold);
        rows.push_back(build_book_menu->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 6) | border);
        rows.push_back(separator());
        Elements out_row;
        out_row.push_back(text("Output:    "));
        out_row.push_back(input_output->Render() | flex);
        rows.push_back(hbox(std::move(out_row)));

        Elements auth_row;
        auth_row.push_back(text("Author:    "));
        auth_row.push_back(input_author->Render() | flex);
        rows.push_back(hbox(std::move(auth_row)));

        Elements pub_row;
        pub_row.push_back(text("Publisher: "));
        pub_row.push_back(input_publisher->Render() | flex);
        rows.push_back(hbox(std::move(pub_row)));

        Elements chk_row;
        chk_row.push_back(check_append->Render());
        chk_row.push_back(text("    "));
        chk_row.push_back(check_simplify->Render());
        rows.push_back(hbox(std::move(chk_row)));
        rows.push_back(separator());
        rows.push_back(btn_build->Render() | center);
        rows.push_back(separator());
        
        auto status_text = text(build_status) | center;
        if (building.load()) status_text = status_text | blink;
        rows.push_back(status_text);

        return vbox(std::move(rows));
    });

    // ══════════════════════════════════════════════════════════════════════
    //  TAB 5: SETTINGS
    // ══════════════════════════════════════════════════════════════════════

    auto input_miniflux_url = Input(&miniflux_server_url, "Miniflux URL");
    auto input_miniflux_token = Input(&miniflux_token, "API Token");
    auto btn_save_miniflux = Button("Save Miniflux Settings", [&] {
        config_.set("miniflux", "url", miniflux_server_url);
        config_.set("miniflux", "token", miniflux_token);
        config_.save();
        miniflux_fetcher_.set_credentials(miniflux_server_url, miniflux_token);
        status_message = "Miniflux settings saved!";
    });

    auto input_rss_max = Input(&rss_max_articles, "Max Articles");
    auto btn_save_rss = Button("Save RSS Settings", [&] {
        config_.set("rss", "max_articles", rss_max_articles);
        config_.save();
        status_message = "RSS settings saved!";
    });

    auto btn_save_fuzzy = Button("Toggle & Save Fuzzy Search", [&] {
        fuzzy_search = !fuzzy_search;
        config_.set("ui", "fuzzy_search", fuzzy_search ? "true" : "false");
        config_.save();
        status_message = "Fuzzy search " + std::string(fuzzy_search ? "enabled" : "disabled");
    });

    auto check_auto_akonadi = Checkbox("Auto-fetch Akonadi Folders on KMail tab", &auto_fetch_akonadi);
    auto check_auto_miniflux = Checkbox("Auto-fetch Miniflux Categories on Miniflux tab", &auto_fetch_miniflux);
    auto check_remember_config = Checkbox("Remember state (tab, folders, filters) across launches", &remember_config);
    auto btn_save_behavior = Button("Save Behavior Settings", [&] {
        config_.set("behavior", "auto_fetch_akonadi", auto_fetch_akonadi ? "true" : "false");
        config_.set("behavior", "auto_fetch_miniflux", auto_fetch_miniflux ? "true" : "false");
        config_.set("behavior", "remember_config", remember_config ? "true" : "false");
        config_.save();
        status_message = "Behavior settings saved!";
    });

    auto check_resolve_stubs = Checkbox("Automatically resolve article stubs during staging", &enable_stub_resolving);
    auto input_stub_threshold = Input(&stub_threshold_text, "Threshold (chars)");
    auto input_request_delay = Input(&request_delay_text, "Request Delay (ms)");
    auto btn_save_fetching = Button("Save Fetching Settings", [&] {
        config_.set("fetching", "enable_stub_resolving", enable_stub_resolving ? "true" : "false");
        config_.set("fetching", "stub_char_threshold", stub_threshold_text);
        config_.set("fetching", "request_delay_ms", request_delay_text);
        config_.save();
        status_message = "Fetching settings saved!";
    });

    auto settings_tab = Container::Vertical({
        input_miniflux_url, input_miniflux_token, btn_save_miniflux,
        input_rss_max, btn_save_rss,
        btn_save_fuzzy,
        check_auto_akonadi, check_auto_miniflux, check_remember_config, btn_save_behavior,
        check_resolve_stubs, input_stub_threshold, input_request_delay, btn_save_fetching
    });
    auto settings_renderer = Renderer(settings_tab, [&] {
        Elements rows;
        rows.push_back(text("⚙ SETTINGS") | bold | center);
        rows.push_back(separator());
        rows.push_back(text("Miniflux API") | bold);
        Elements url_row;
        url_row.push_back(text("  URL:   "));
        url_row.push_back(input_miniflux_url->Render() | flex);
        rows.push_back(hbox(std::move(url_row)));
        Elements token_row;
        token_row.push_back(text("  Token: "));
        token_row.push_back(input_miniflux_token->Render() | flex);
        rows.push_back(hbox(std::move(token_row)));
        rows.push_back(btn_save_miniflux->Render());
        rows.push_back(separator());
        rows.push_back(text("Behavior & Memory") | bold);
        rows.push_back(check_auto_akonadi->Render());
        rows.push_back(check_auto_miniflux->Render());
        rows.push_back(check_remember_config->Render());
        rows.push_back(btn_save_behavior->Render());
        rows.push_back(separator());
        rows.push_back(text("Fetching & Stubs") | bold);
        rows.push_back(check_resolve_stubs->Render());
        Elements thresh_row;
        thresh_row.push_back(text("  Threshold: "));
        thresh_row.push_back(input_stub_threshold->Render() | flex);
        rows.push_back(hbox(std::move(thresh_row)));
        Elements delay_row;
        delay_row.push_back(text("  Delay (ms): "));
        delay_row.push_back(input_request_delay->Render() | flex);
        rows.push_back(hbox(std::move(delay_row)));
        rows.push_back(btn_save_fetching->Render());
        rows.push_back(check_auto_miniflux->Render());
        rows.push_back(check_remember_config->Render());
        rows.push_back(btn_save_behavior->Render());
        rows.push_back(separator());
        rows.push_back(text("RSS Options") | bold);
        Elements max_row;
        max_row.push_back(text("  Max Articles: "));
        max_row.push_back(input_rss_max->Render());
        rows.push_back(hbox(std::move(max_row)));
        rows.push_back(btn_save_rss->Render());
        rows.push_back(separator());
        rows.push_back(text("Search Behavior") | bold);
        rows.push_back(text(std::string("  Current Mode: ") + (fuzzy_search ? "Fuzzy" : "Regex")));
        rows.push_back(btn_save_fuzzy->Render());
        rows.push_back(separator());
        Elements akonadi_row;
        akonadi_row.push_back(text("Akonadi Status: "));
        akonadi_row.push_back(text(fetcher_.is_available() ? "✓ Connected" : "✗ Disconnected") | color(fetcher_.is_available() ? Color::Green : Color::Red));
        rows.push_back(hbox(std::move(akonadi_row)));
        rows.push_back(separator());
        rows.push_back(text("Config File: " + config_.config_file().string()) | dim);

        return vbox(std::move(rows));
    });

    // ══════════════════════════════════════════════════════════════════════
    //  MAIN CONTAINER
    // ══════════════════════════════════════════════════════════════════════

    std::vector<std::string> tab_labels = {" 📧 KMail ", " 🌀 Miniflux ", " 🌐 RSS ", " 📚 Books ", " 📖 Build ", " ⚙ Settings "};
    auto tab_toggle = Toggle(&tab_labels, &active_tab);
    auto tab_content = Container::Tab({kmail_renderer, miniflux_renderer, rss_renderer, books_renderer, build_renderer, settings_renderer}, &active_tab);
    
    auto main_container = Container::Vertical({tab_toggle, tab_content});
    auto main_component = CatchEvent(main_container, [&](Event event) {
        int prev_tab = active_tab;
        bool handled = false;
        
        if (main_container->OnEvent(event)) handled = true;
        else if (event == Event::F1) { active_tab = 0; handled = true; }
        else if (event == Event::F2) { active_tab = 1; handled = true; }
        else if (event == Event::F3) { active_tab = 2; handled = true; }
        else if (event == Event::F4) { active_tab = 3; handled = true; }
        else if (event == Event::F5) { active_tab = 4; handled = true; }
        else if (event == Event::F6) { active_tab = 5; handled = true; }
        else if (event == Event::Character('1')) { active_tab = 0; handled = true; }
        else if (event == Event::Character('2')) { active_tab = 1; handled = true; }
        else if (event == Event::Character('3')) { active_tab = 2; handled = true; }
        else if (event == Event::Character('4')) { active_tab = 3; handled = true; }
        else if (event == Event::Character('5')) { active_tab = 4; handled = true; }
        else if (event == Event::Character('6')) { active_tab = 5; handled = true; }
        else if (event == Event::Character('q') || event == Event::Escape) { screen.Exit(); return true; }

        if (handled && prev_tab != active_tab) {
            if (active_tab == 0 && auto_fetch_akonadi && folders.empty() && !loading_folders) do_fetch_folders();
            if (active_tab == 1 && auto_fetch_miniflux && miniflux_categories.empty() && !loading_miniflux_categories) do_fetch_mini_cats();
        }
        return handled;
    });

    auto final_renderer = Renderer(main_component, [&] {
        Elements foot;
        foot.push_back(text(" Endpaper v0.1 ") | bold | inverted);
        foot.push_back(text(" "));
        foot.push_back(text(status_message) | flex);
        foot.push_back(text(" [q]uit [1-6]tabs ") | dim);

        Elements rows;
        rows.push_back(tab_toggle->Render() | center);
        rows.push_back(separator());
        rows.push_back(tab_content->Render() | flex);
        rows.push_back(separator());
        rows.push_back(hbox(std::move(foot)));

        return vbox(std::move(rows)) | border;
    });

    screen.Loop(final_renderer);

    // Save state on exit if enabled
    if (remember_config) {
        config_.set("state", "active_tab", std::to_string(active_tab));
        config_.set("state", "last_miniflux_filter", std::to_string(miniflux_status_filter));
        if (folder_selected >= 0 && folder_selected < static_cast<int>(folders.size())) {
            config_.set("state", "last_kmail_folder", folders[folder_selected].full_path);
        }
        if (miniflux_category_selected > 0 && miniflux_category_selected - 1 < static_cast<int>(miniflux_categories.size())) {
            config_.set("state", "last_miniflux_category", miniflux_categories[miniflux_category_selected - 1].title);
        }
        if (build_book_selected >= 0 && build_book_selected < static_cast<int>(books.size())) {
            config_.set("state", "last_build_book", books[build_book_selected].title);
        }
        config_.save();
    }
}

} // namespace endpaper
