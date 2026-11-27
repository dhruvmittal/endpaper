#include "tui_app.h"
#include "build_pipeline.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace ftxui;

namespace endpaper {

namespace {

std::string trim_copy(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool parse_non_negative_int(const std::string& raw, int& out) {
    std::string s = trim_copy(raw);
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    try {
        size_t pos = 0;
        long long v = std::stoll(s, &pos, 10);
        if (pos != s.size() || v < 0 || v > INT_MAX) return false;
        out = static_cast<int>(v);
        return true;
    } catch (...) {
        return false;
    }
}

struct SelectableArticle {
    std::string title;
    std::string author;
    std::string date;
    std::string url;
    std::string html_body;
    int64_t source_id = 0;
    bool selected = false;
};

struct FilteredView {
    std::vector<size_t> source_indices;
    std::vector<std::string> labels;
    int cursor = 0;
};

void rebuild_filtered_view(
    const std::vector<SelectableArticle>& items,
    const std::string& query,
    bool fuzzy,
    FilteredView& view)
{
    view.source_indices.clear();
    view.labels.clear();

    for (size_t i = 0; i < items.size(); ++i) {
        if (!matches_article_filter(items[i].title, items[i].author, query, fuzzy)) {
            continue;
        }
        view.source_indices.push_back(i);
        std::string line = items[i].selected ? "[x] " : "[ ] ";
        line += items[i].title.empty() ? "(Untitled)" : items[i].title;
        if (!items[i].author.empty()) line += " — " + items[i].author;
        if (!items[i].date.empty()) line += " (" + items[i].date + ")";
        view.labels.push_back(std::move(line));
    }

    if (view.labels.empty()) {
        view.cursor = 0;
    } else if (view.cursor >= static_cast<int>(view.labels.size())) {
        view.cursor = static_cast<int>(view.labels.size()) - 1;
    }
}

std::vector<MinifluxCategory> with_all_category(const std::vector<MinifluxCategory>& cats) {
    std::vector<MinifluxCategory> out;
    out.reserve(cats.size() + 1);
    MinifluxCategory all{-1, "All Categories", 0};
    for (const auto& c : cats) all.unread_count += c.unread_count;
    out.push_back(std::move(all));
    out.insert(out.end(), cats.begin(), cats.end());
    return out;
}

} // namespace

TuiApp::TuiApp(Config& config, Database& db, AkonadiFetcher& fetcher,
               RssFetcher& rss_fetcher, MinifluxFetcher& miniflux_fetcher,
               HtmlExtractor& html_extractor)
    : config_(config)
    , db_(db)
    , fetcher_(fetcher)
    , rss_fetcher_(rss_fetcher)
    , miniflux_fetcher_(miniflux_fetcher)
    , html_extractor_(html_extractor) {}

void TuiApp::run() {
    auto screen = ScreenInteractive::Fullscreen();

    WorkerPool worker_pool;
    std::atomic<bool> shutting_down{false};
    auto run_on_ui = [&](std::function<void()> fn) {
        if (shutting_down.load()) return;
        screen.Post(std::move(fn));
        screen.PostEvent(Event::Custom);
    };

    // ── Status & busy flags ──────────────────────────────────────────────
    std::string status_msg = "Ready";
    bool status_error = false;
    bool loading_folders = false;
    bool loading_emails = false;
    bool loading_mf_cats = false;
    bool loading_mf_entries = false;
    bool loading_rss = false;
    bool staging = false;
    bool building = false;
    std::string confirm_action;

    auto set_status = [&](const std::string& msg, bool err = false) {
        status_msg = msg;
        status_error = err;
    };
    auto is_busy = [&]() {
        return loading_folders || loading_emails || loading_mf_cats ||
               loading_mf_entries || loading_rss || staging || building;
    };

    // ── Config-backed settings state ─────────────────────────────────────
    bool auto_fetch_kmail = config_.get_bool("general", "auto_fetch_kmail", false);
    bool auto_fetch_miniflux = config_.get_bool("general", "auto_fetch_miniflux", false);
    bool cache_metadata = config_.get_bool("general", "cache_metadata", true);
    bool remember_state = config_.get_bool("general", "remember_state", true);
    bool fuzzy_search = config_.get_bool("ui", "fuzzy_search", true);

    bool enable_stub_resolving = config_.get_bool("fetching", "enable_stub_resolving", true);
    std::string stub_threshold_text = std::to_string(
        std::max(0, config_.get_int("fetching", "stub_char_threshold", 1000)));
    std::string request_delay_text = std::to_string(
        std::max(0, config_.get_int("fetching", "request_delay_ms", 1000)));

    std::string mf_url_str = config_.get("miniflux", "url", "");
    std::string mf_token_str = config_.get("miniflux", "token", "");
    miniflux_fetcher_.set_credentials(mf_url_str, mf_token_str);

    std::string epub_author = config_.get("epub", "author", "Unknown");
    std::string epub_publisher = config_.get("epub", "publisher", "Endpaper");
    std::string default_folder = config_.get("general", "default_folder", "");
    std::string rss_max_articles_text = std::to_string(
        std::max(0, config_.get_int("rss", "max_articles", 50)));

    bool simplify_html_opt = config_.get_bool("epub", "simplify", true);
    bool keep_images_opt = config_.get_bool("epub", "keep_images", false);
    bool append_mode = false; // Session-only so rebuilds don't silently duplicate chapters

    bool akonadi_available = fetcher_.is_available();

    // ── Persisted navigation/selection state ─────────────────────────────
    int tab_index = remember_state
        ? std::clamp(config_.get_int("state", "last_page_index", 0), 0, 5)
        : 0;
    int main_sel_main = 0; // 0 = top tab bar, 1 = tab body

    int64_t selected_book_id = remember_state
        ? config_.get_int64("general", "selected_book_id", -1)
        : -1;
    int64_t last_kmail_col_id = remember_state
        ? config_.get_int64("state", "last_kmail_collection_id", -1)
        : -1;
    int64_t last_mf_cat_id = remember_state
        ? config_.get_int64("state", "last_miniflux_category_id", -1)
        : -1;
    int mf_status_idx = remember_state
        ? std::clamp(config_.get_int("state", "last_miniflux_status_filter", 0), 0, 2)
        : 0;
    int64_t last_rss_feed_id = remember_state
        ? config_.get_int64("state", "last_rss_feed_id", -1)
        : -1;

    auto persist_state = [&](const std::string& sec, const std::string& key, const std::string& val) {
        if (!remember_state) return;
        config_.set(sec, key, val);
        config_.save();
    };

    // ── Track text inputs so single-letter shortcuts never steal typing ──
    std::vector<Component> text_inputs;
    auto make_input = [&](std::string* content, std::string placeholder = "",
                          bool password = false, std::function<void()> on_change = {}) {
        InputOption opt;
        opt.multiline = false;
        opt.password = password;
        if (on_change) opt.on_change = std::move(on_change);
        auto comp = Input(content, std::move(placeholder), std::move(opt));
        text_inputs.push_back(comp);
        return comp;
    };
    auto any_input_focused = [&]() {
        for (const auto& inp : text_inputs) {
            if (inp->Focused()) return true;
        }
        return false;
    };

    // ── Books & Chapters state ───────────────────────────────────────────
    std::vector<Book> books;
    std::vector<std::string> book_labels;
    int selected_book_idx = 0;

    std::vector<Chapter> chapters;
    std::vector<std::string> chapter_labels;
    int selected_chapter_idx = 0;

    std::string new_book_title;
    std::string epub_output_path;
    bool custom_output_path_edited = false;

    auto current_book_title = [&]() -> std::string {
        for (const auto& b : books) {
            if (b.id == selected_book_id) return b.title;
        }
        return "";
    };

    auto update_epub_path = [&](bool force) {
        if (!force && custom_output_path_edited) return;
        std::string title = current_book_title();
        if (title.empty()) {
            epub_output_path.clear();
        } else {
            epub_output_path = resolve_epub_output_path(title, "", trim_copy(default_folder));
        }
    };

    auto refresh_chapters = [&]() {
        chapters.clear();
        chapter_labels.clear();
        selected_chapter_idx = 0;
        if (selected_book_id == -1) return;

        chapters = db_.get_chapters(selected_book_id);
        for (size_t i = 0; i < chapters.size(); ++i) {
            std::string label = std::to_string(i + 1) + ". " + chapters[i].subject;
            if (!chapters[i].sender.empty()) label += " — " + chapters[i].sender;
            chapter_labels.push_back(std::move(label));
        }
    };

    auto refresh_books = [&]() {
        books = db_.list_books();
        book_labels.clear();
        if (books.empty()) {
            book_labels.push_back("(No books)");
            selected_book_id = -1;
            selected_book_idx = 0;
            refresh_chapters();
            update_epub_path(true);
            return;
        }

        int matched = -1;
        for (size_t i = 0; i < books.size(); ++i) {
            book_labels.push_back(books[i].title + " (" + std::to_string(books[i].chapter_count) + " ch)");
            if (books[i].id == selected_book_id) matched = static_cast<int>(i);
        }
        if (matched >= 0) {
            selected_book_idx = matched;
        } else {
            selected_book_idx = 0;
            selected_book_id = books[0].id;
            persist_state("general", "selected_book_id", std::to_string(selected_book_id));
        }
        refresh_chapters();
        update_epub_path(false);
    };

    auto on_book_selection_changed = [&]() {
        confirm_action.clear();
        if (selected_book_idx < 0 || selected_book_idx >= static_cast<int>(books.size())) {
            selected_book_id = -1;
        } else {
            selected_book_id = books[selected_book_idx].id;
        }
        custom_output_path_edited = false;
        persist_state("general", "selected_book_id", std::to_string(selected_book_id));
        refresh_chapters();
        update_epub_path(true);
    };

    auto make_book_dropdown = [&]() {
        DropdownOption opt;
        opt.radiobox.entries = &book_labels;
        opt.radiobox.selected = &selected_book_idx;
        opt.radiobox.on_change = [&]() { on_book_selection_changed(); };
        return Dropdown(std::move(opt));
    };

    // ── Shared staging helper ────────────────────────────────────────────
    auto run_stage = [&](std::vector<SelectableArticle>& items,
                         const std::string& label_noun,
                         StubResolver resolver) {
        if (is_busy()) {
            set_status("Another background operation is already running.", true);
            return;
        }
        if (selected_book_id == -1) {
            set_status("Select or create a Book before staging.", true);
            return;
        }

        std::vector<StageItem> chosen;
        for (const auto& a : items) {
            if (!a.selected) continue;
            chosen.push_back(StageItem{a.title, a.author, a.html_body, a.source_id, a.url});
        }
        if (chosen.empty()) {
            set_status("No articles selected.", true);
            return;
        }

        StageOptions opts;
        opts.resolve_stubs = enable_stub_resolving;
        int threshold = 1000;
        if (parse_non_negative_int(stub_threshold_text, threshold)) {
            opts.stub_char_threshold = static_cast<size_t>(threshold);
        }
        int delay = 1000;
        if (parse_non_negative_int(request_delay_text, delay)) {
            opts.request_delay_ms = delay;
        }

        staging = true;
        int64_t target_book_id = selected_book_id;
        std::string book_name = current_book_title();
        set_status("Staging " + std::to_string(chosen.size()) + " " + label_noun + "...");

        worker_pool.launch([&, target_book_id, book_name, chosen = std::move(chosen),
                            opts, resolver = std::move(resolver)]() mutable {
            try {
                auto res = stage_articles(
                    db_, target_book_id, chosen, opts, resolver,
                    [&](const std::string& msg) {
                        run_on_ui([&, msg]() { set_status(msg); });
                    },
                    worker_pool.stop_flag());

                run_on_ui([&, res, book_name]() {
                    staging = false;
                    refresh_books();
                    std::string msg = "Staged " + std::to_string(res.staged) + " articles into \"" +
                                      (book_name.empty() ? "Book" : book_name) + "\"";
                    if (res.stub_failures > 0) {
                        msg += " (" + std::to_string(res.stub_failures) + " stub resolution warning(s))";
                    }
                    set_status(msg, false);
                });
            } catch (const std::exception& e) {
                std::string err = e.what() ? e.what() : "Staging error";
                run_on_ui([&, err]() {
                    staging = false;
                    set_status("Stage Error: " + err, true);
                });
            } catch (...) {
                run_on_ui([&]() {
                    staging = false;
                    set_status("Stage Error: unexpected failure", true);
                });
            }
        });
    };

    // Helper for article Menu with Space/Return toggling without double-dispatch
    auto make_article_menu = [&](std::vector<SelectableArticle>& items,
                                 FilteredView& view,
                                 const std::string& query) {
        MenuOption mopt;
        mopt.entries = &view.labels;
        mopt.selected = &view.cursor;
        auto menu = Menu(std::move(mopt));
        return CatchEvent(menu, [&](Event ev) {
            if (ev != Event::Character(' ') && ev != Event::Return) {
                return false;
            }
            if (view.cursor < 0 || view.cursor >= static_cast<int>(view.source_indices.size())) {
                return true;
            }
            size_t idx = view.source_indices[view.cursor];
            if (idx < items.size()) {
                items[idx].selected = !items[idx].selected;
                rebuild_filtered_view(items, query, fuzzy_search, view);
            }
            return true;
        });
    };

    auto select_visible = [&](std::vector<SelectableArticle>& items,
                              FilteredView& view,
                              const std::string& query,
                              bool state) {
        for (size_t idx : view.source_indices) {
            if (idx < items.size()) items[idx].selected = state;
        }
        rebuild_filtered_view(items, query, fuzzy_search, view);
    };

    // ═════════════════════════════════════════════════════════════════════
    // TAB 0: KMAIL (Akonadi)
    // ═════════════════════════════════════════════════════════════════════
    struct TuiFolderEntry {
        int64_t collection_id = -1;
        bool has_children = false;
        bool is_root = false;
        bool is_pinned_row = false;
        std::string name;
        std::string label;
    };

    std::vector<FolderInfo> folders;
    FolderPrefs folder_prefs = db_.load_folder_prefs();
    std::unordered_set<int64_t> expanded_kmail_nodes;
    bool auto_expand_last_done = false;
    bool show_hidden_kmail = false;

    std::vector<TuiFolderEntry> folder_rows;
    std::vector<std::string> folder_labels;
    int selected_folder_idx = 0;

    std::vector<SelectableArticle> kmail_articles;
    std::string kmail_filter;
    FilteredView kmail_view;

    auto collect_kmail_subtree = [&](int64_t root_id) {
        std::unordered_map<int64_t, std::vector<int64_t>> by_parent;
        for (const auto& f : folders) {
            if (f.parent_id != -1 && f.parent_id != f.collection_id) {
                by_parent[f.parent_id].push_back(f.collection_id);
            }
        }
        std::vector<int64_t> out;
        std::unordered_set<int64_t> seen;
        std::vector<int64_t> stack{root_id};
        while (!stack.empty()) {
            int64_t cur = stack.back();
            stack.pop_back();
            if (!seen.insert(cur).second) continue;
            out.push_back(cur);
            if (auto it = by_parent.find(cur); it != by_parent.end()) {
                for (int64_t ch : it->second) stack.push_back(ch);
            }
        }
        return out;
    };

    auto rebuild_kmail_tree = [&](bool prefer_last = false) {
        populate_folder_paths(folders);
        folder_prefs = db_.load_folder_prefs();

        int64_t target_id = prefer_last
            ? last_kmail_col_id
            : (selected_folder_idx >= 0 && selected_folder_idx < static_cast<int>(folder_rows.size())
                   ? folder_rows[selected_folder_idx].collection_id
                   : last_kmail_col_id);

        std::unordered_map<int64_t, const FolderInfo*> by_id;
        by_id.reserve(folders.size());
        for (const auto& f : folders) by_id.emplace(f.collection_id, &f);

        auto is_hidden = [&](int64_t cid) {
            int64_t cur = cid;
            for (int depth = 0; depth < 32 && cur != -1; ++depth) {
                if (folder_prefs.hidden.count(cur)) return true;
                auto it = by_id.find(cur);
                if (it == by_id.end()) break;
                cur = it->second->parent_id;
            }
            return false;
        };

        if (!auto_expand_last_done && last_kmail_col_id != -1 && !folders.empty()) {
            auto_expand_last_done = true;
            if (!is_hidden(last_kmail_col_id) && !folder_prefs.pinned.count(last_kmail_col_id)) {
                auto it = by_id.find(last_kmail_col_id);
                if (it != by_id.end()) {
                    int64_t cur = it->second->parent_id;
                    for (int depth = 0; depth < 32 && cur != -1; ++depth) {
                        expanded_kmail_nodes.insert(cur);
                        auto pit = by_id.find(cur);
                        if (pit == by_id.end()) break;
                        cur = pit->second->parent_id;
                    }
                }
            }
        }

        auto root_name_for = [&](int64_t cid) -> std::string {
            int64_t cur = cid;
            const FolderInfo* root = nullptr;
            for (int depth = 0; depth < 32 && cur != -1; ++depth) {
                auto it = by_id.find(cur);
                if (it == by_id.end()) break;
                root = it->second;
                cur = it->second->parent_id;
            }
            return (root && root->collection_id != cid) ? root->name : "";
        };

        folder_rows.clear();
        folder_labels.clear();
        int matched_row = -1;

        // Pinned rows at top
        for (const auto& f : folders) {
            if (!folder_prefs.pinned.count(f.collection_id) || is_hidden(f.collection_id)) continue;
            std::string acct = root_name_for(f.collection_id);
            std::string lbl = "★ " + (f.name.empty() ? f.full_path : f.name);
            if (!acct.empty()) lbl += " (" + acct + ")";
            if (f.collection_id == target_id && matched_row == -1) {
                matched_row = static_cast<int>(folder_rows.size());
            }
            folder_rows.push_back(TuiFolderEntry{
                f.collection_id, false, false, true, f.name, lbl
            });
            folder_labels.push_back(lbl);
        }

        // Hierarchical accounts & folders
        std::unordered_map<int64_t, std::vector<const FolderInfo*>> children_by_parent;
        std::vector<const FolderInfo*> roots;
        for (const auto& f : folders) {
            if (is_hidden(f.collection_id) && !show_hidden_kmail) continue;
            if (f.parent_id == -1 || f.parent_id == f.collection_id || by_id.find(f.parent_id) == by_id.end()) {
                roots.push_back(&f);
            } else {
                children_by_parent[f.parent_id].push_back(&f);
            }
        }

        std::function<void(const FolderInfo*, int, bool)> append_node;
        append_node = [&](const FolderInfo* f, int depth, bool is_root) {
            std::vector<const FolderInfo*> vis_children;
            if (auto it = children_by_parent.find(f->collection_id); it != children_by_parent.end()) {
                for (const FolderInfo* ch : it->second) {
                    if (!is_hidden(ch->collection_id) || show_hidden_kmail) {
                        vis_children.push_back(ch);
                    }
                }
            }
            bool has_ch = !vis_children.empty();
            bool expanded = has_ch && expanded_kmail_nodes.count(f->collection_id) > 0;
            std::string lbl(static_cast<size_t>(depth * 2), ' ');
            if (has_ch) lbl += expanded ? "▾ " : "▸ ";
            else lbl += "  ";
            lbl += f->name.empty() ? f->full_path : f->name;
            if (folder_prefs.pinned.count(f->collection_id)) lbl += " ★";
            if (is_hidden(f->collection_id)) lbl += " [hidden]";

            if (f->collection_id == target_id && matched_row == -1) {
                matched_row = static_cast<int>(folder_rows.size());
            }
            folder_rows.push_back(TuiFolderEntry{
                f->collection_id, has_ch, is_root, false, f->name, lbl
            });
            folder_labels.push_back(lbl);

            if (expanded) {
                for (const FolderInfo* ch : vis_children) {
                    append_node(ch, depth + 1, false);
                }
            }
        };

        for (const FolderInfo* r : roots) {
            append_node(r, 0, true);
        }

        if (matched_row >= 0) {
            selected_folder_idx = matched_row;
        } else if (folder_labels.empty()) {
            selected_folder_idx = 0;
        } else if (selected_folder_idx >= static_cast<int>(folder_labels.size())) {
            selected_folder_idx = static_cast<int>(folder_labels.size()) - 1;
        }
    };

    if (cache_metadata) {
        folders = db_.load_folder_cache();
        rebuild_kmail_tree(true);
    }

    auto fetch_kmail_folders = [&]() {
        if (is_busy()) return;
        akonadi_available = fetcher_.is_available();
        if (!akonadi_available) {
            set_status("Akonadi service unavailable.", true);
            return;
        }
        loading_folders = true;
        set_status("Fetching KMail folders...");
        worker_pool.launch([&]() {
            try {
                auto result = fetcher_.fetch_folders().get();
                if (worker_pool.is_shutting_down()) return;
                if (cache_metadata) db_.save_folder_cache(result);

                run_on_ui([&, result = std::move(result)]() mutable {
                    loading_folders = false;
                    folders = std::move(result);
                    rebuild_kmail_tree(true);
                    set_status("Loaded " + std::to_string(folders.size()) + " KMail folders");
                });
            } catch (const std::exception& e) {
                std::string err = e.what() ? e.what() : "Unknown error";
                run_on_ui([&, err]() {
                    loading_folders = false;
                    set_status("KMail Error: " + err, true);
                });
            } catch (...) {
                run_on_ui([&]() {
                    loading_folders = false;
                    set_status("KMail Error: unexpected failure", true);
                });
            }
        });
    };

    auto fetch_kmail_emails = [&]() {
        if (is_busy()) return;
        if (selected_folder_idx < 0 || selected_folder_idx >= static_cast<int>(folder_rows.size())) {
            set_status("Select a KMail folder first.", true);
            return;
        }
        const auto& row = folder_rows[selected_folder_idx];
        if (row.is_root && row.has_children) {
            if (expanded_kmail_nodes.count(row.collection_id)) {
                expanded_kmail_nodes.erase(row.collection_id);
            } else {
                expanded_kmail_nodes.insert(row.collection_id);
            }
            rebuild_kmail_tree(false);
            return;
        }
        akonadi_available = fetcher_.is_available();
        if (!akonadi_available) {
            set_status("Akonadi service unavailable.", true);
            return;
        }

        int64_t col_id = row.collection_id;
        last_kmail_col_id = col_id;
        persist_state("state", "last_kmail_collection_id", std::to_string(col_id));

        loading_emails = true;
        set_status("Fetching emails from " + row.name + "...");
        worker_pool.launch([&, col_id]() {
            try {
                auto emails = fetcher_.fetch_emails(col_id).get();
                if (worker_pool.is_shutting_down()) return;

                std::vector<SelectableArticle> out;
                out.reserve(emails.size());
                for (auto& em : emails) {
                    out.push_back(SelectableArticle{
                        std::move(em.subject), std::move(em.sender), std::move(em.date),
                        "", std::move(em.html_body), em.akonadi_id, false
                    });
                }

                run_on_ui([&, out = std::move(out)]() mutable {
                    loading_emails = false;
                    kmail_articles = std::move(out);
                    rebuild_filtered_view(kmail_articles, kmail_filter, fuzzy_search, kmail_view);
                    set_status("Loaded " + std::to_string(kmail_articles.size()) + " emails");
                });
            } catch (const std::exception& e) {
                std::string err = e.what() ? e.what() : "Unknown error";
                run_on_ui([&, err]() {
                    loading_emails = false;
                    set_status("Email Fetch Error: " + err, true);
                });
            } catch (...) {
                run_on_ui([&]() {
                    loading_emails = false;
                    set_status("Email Fetch Error: unexpected failure", true);
                });
            }
        });
    };

    MenuOption kmail_folder_mopt;
    kmail_folder_mopt.entries = &folder_labels;
    kmail_folder_mopt.selected = &selected_folder_idx;
    kmail_folder_mopt.on_enter = fetch_kmail_emails;
    auto raw_kmail_folder_menu = Menu(std::move(kmail_folder_mopt));
    auto kmail_folder_menu = CatchEvent(raw_kmail_folder_menu, [&](Event ev) {
        if (selected_folder_idx < 0 || selected_folder_idx >= static_cast<int>(folder_rows.size())) {
            return false;
        }
        const auto row = folder_rows[selected_folder_idx];
        if (ev == Event::ArrowRight && row.has_children && !row.is_pinned_row) {
            expanded_kmail_nodes.insert(row.collection_id);
            rebuild_kmail_tree(false);
            return true;
        }
        if (ev == Event::ArrowLeft && row.has_children && !row.is_pinned_row) {
            expanded_kmail_nodes.erase(row.collection_id);
            rebuild_kmail_tree(false);
            return true;
        }
        if ((ev == Event::Character('p') || ev == Event::Character('P')) && !row.is_root) {
            bool next_pin = folder_prefs.pinned.count(row.collection_id) == 0;
            db_.set_folder_pinned(row.collection_id, next_pin);
            rebuild_kmail_tree(false);
            set_status((next_pin ? "Pinned folder: " : "Unpinned folder: ") + row.name);
            return true;
        }
        if (ev == Event::Character('h') || ev == Event::Character('H')) {
            auto ids = collect_kmail_subtree(row.collection_id);
            bool currently_hidden = folder_prefs.hidden.count(row.collection_id) > 0;
            db_.set_folders_hidden(ids, !currently_hidden);
            rebuild_kmail_tree(false);
            set_status((currently_hidden ? "Unhid " : "Hid ") + row.name);
            return true;
        }
        return false;
    });

    auto kmail_filter_input = make_input(&kmail_filter, "Filter emails...", false, [&]() {
        rebuild_filtered_view(kmail_articles, kmail_filter, fuzzy_search, kmail_view);
    });
    auto kmail_article_menu = make_article_menu(kmail_articles, kmail_view, kmail_filter);
    auto kmail_book_dd = make_book_dropdown();

    auto kmail_btn_folders = Button("Fetch Folders", fetch_kmail_folders);
    auto kmail_btn_emails = Button("Load Folder", fetch_kmail_emails);
    auto kmail_btn_hide_sys = Button("Hide System", [&]() {
        std::vector<int64_t> sys_ids;
        for (const auto& f : folders) {
            if (is_system_mail_folder(f.name)) {
                auto sub = collect_kmail_subtree(f.collection_id);
                sys_ids.insert(sys_ids.end(), sub.begin(), sub.end());
            }
        }
        db_.set_folders_hidden(sys_ids, true);
        rebuild_kmail_tree(false);
        set_status("Hidden system folders (" + std::to_string(sys_ids.size()) + ")");
    });
    auto kmail_show_hidden_cb = [&]() {
        CheckboxOption opt = CheckboxOption::Simple();
        opt.label = "Show hidden";
        opt.checked = &show_hidden_kmail;
        opt.on_change = [&]() { rebuild_kmail_tree(false); };
        return Checkbox(std::move(opt));
    }();
    auto kmail_btn_all = Button("Select All", [&]() {
        select_visible(kmail_articles, kmail_view, kmail_filter, true);
    });
    auto kmail_btn_none = Button("Select None", [&]() {
        select_visible(kmail_articles, kmail_view, kmail_filter, false);
    });
    auto kmail_btn_stage = Button("Stage Selected", [&]() {
        run_stage(kmail_articles, "emails", {});
    });

    auto kmail_container = Container::Vertical({
        Container::Horizontal({
            kmail_book_dd, kmail_btn_folders, kmail_btn_emails,
            kmail_btn_hide_sys, kmail_show_hidden_cb,
        }),
        kmail_filter_input,
        Container::Horizontal({kmail_folder_menu, kmail_article_menu}),
        Container::Horizontal({kmail_btn_all, kmail_btn_none, kmail_btn_stage}),
    });

    auto kmail_renderer = Renderer(kmail_container, [&]() {
        return vbox({
            hbox({
                text(" Book: ") | bold | vcenter,
                kmail_book_dd->Render(),
                text("  "),
                kmail_btn_folders->Render(),
                text(" "),
                kmail_btn_emails->Render(),
                text(" "),
                kmail_btn_hide_sys->Render(),
                text(" "),
                kmail_show_hidden_cb->Render() | vcenter,
                filler(),
                text(akonadi_available ? "Akonadi: Online " : "Akonadi: Offline ") |
                    color(akonadi_available ? Color::Green : Color::Red),
            }),
            separator(),
            hbox({text(" Search: ") | bold, kmail_filter_input->Render() | flex}),
            separator(),
            hbox({
                vbox({
                    text(" Folders (" + std::to_string(folder_labels.size()) + ")  [p:pin h:hide ←/→]") | bold,
                    kmail_folder_menu->Render() | vscroll_indicator | frame | flex,
                }) | size(WIDTH, GREATER_THAN, 30) | size(WIDTH, LESS_THAN, 44) | border,
                vbox({
                    text(" Emails (" + std::to_string(kmail_view.labels.size()) + ")  [Space/Enter: toggle]") | bold,
                    kmail_article_menu->Render() | vscroll_indicator | frame | flex,
                }) | flex | border,
            }) | flex,
            hbox({
                kmail_btn_all->Render(),
                text(" "),
                kmail_btn_none->Render(),
                filler(),
                kmail_btn_stage->Render(),
            }),
        }) | flex;
    });

    // ═════════════════════════════════════════════════════════════════════
    // TAB 1: MINIFLUX
    // ═════════════════════════════════════════════════════════════════════
    std::vector<MinifluxCategory> mf_categories;
    std::vector<std::string> mf_cat_labels;
    int selected_mf_cat_idx = 0;

    std::vector<std::string> mf_status_options = {"All", "Unread", "Read"};
    bool mf_starred_only = false;

    std::vector<SelectableArticle> mf_articles;
    std::string mf_filter;
    FilteredView mf_view;

    auto rebuild_mf_cat_labels = [&]() {
        mf_cat_labels.clear();
        selected_mf_cat_idx = 0;
        for (size_t i = 0; i < mf_categories.size(); ++i) {
            std::string lbl = mf_categories[i].title;
            if (mf_categories[i].unread_count > 0) {
                lbl += " (" + std::to_string(mf_categories[i].unread_count) + ")";
            }
            mf_cat_labels.push_back(std::move(lbl));
            if (mf_categories[i].id == last_mf_cat_id) {
                selected_mf_cat_idx = static_cast<int>(i);
            }
        }
    };

    if (cache_metadata) {
        auto cached_cats = db_.load_category_cache();
        if (!cached_cats.empty()) {
            mf_categories = with_all_category(cached_cats);
            rebuild_mf_cat_labels();
        }
    }

    auto fetch_mf_categories = [&]() {
        if (is_busy()) return;
        if (trim_copy(mf_url_str).empty()) {
            set_status("Configure Miniflux Server URL in Settings first.", true);
            return;
        }
        loading_mf_cats = true;
        set_status("Fetching Miniflux categories...");
        worker_pool.launch([&]() {
            try {
                auto raw = miniflux_fetcher_.fetch_categories().get();
                if (worker_pool.is_shutting_down()) return;
                if (cache_metadata) db_.save_category_cache(raw);

                auto wrapped = with_all_category(raw);
                run_on_ui([&, wrapped = std::move(wrapped)]() mutable {
                    loading_mf_cats = false;
                    mf_categories = std::move(wrapped);
                    rebuild_mf_cat_labels();
                    set_status("Loaded " + std::to_string(mf_categories.size() > 0 ? mf_categories.size() - 1 : 0) +
                               " Miniflux categories");
                });
            } catch (const std::exception& e) {
                std::string err = e.what() ? e.what() : "Unknown error";
                run_on_ui([&, err]() {
                    loading_mf_cats = false;
                    set_status("Miniflux Error: " + err, true);
                });
            } catch (...) {
                run_on_ui([&]() {
                    loading_mf_cats = false;
                    set_status("Miniflux Error: unexpected failure", true);
                });
            }
        });
    };

    auto fetch_mf_entries = [&]() {
        if (is_busy()) return;
        if (trim_copy(mf_url_str).empty()) {
            set_status("Configure Miniflux Server URL in Settings first.", true);
            return;
        }

        int64_t cat_id = -1;
        if (selected_mf_cat_idx >= 0 && selected_mf_cat_idx < static_cast<int>(mf_categories.size())) {
            cat_id = mf_categories[selected_mf_cat_idx].id;
        }
        last_mf_cat_id = cat_id;
        persist_state("state", "last_miniflux_category_id", std::to_string(cat_id));
        persist_state("state", "last_miniflux_status_filter", std::to_string(mf_status_idx));

        MinifluxFetcher::FetchParams params;
        params.category_id = cat_id;
        if (mf_status_idx == 1) params.status = "unread";
        else if (mf_status_idx == 2) params.status = "read";
        params.starred = mf_starred_only;
        params.search = trim_copy(mf_filter);

        loading_mf_entries = true;
        set_status("Fetching Miniflux entries...");
        worker_pool.launch([&, params]() {
            try {
                auto entries = miniflux_fetcher_.fetch_entries(params).get();
                if (worker_pool.is_shutting_down()) return;

                std::vector<SelectableArticle> out;
                out.reserve(entries.size());
                for (auto& en : entries) {
                    out.push_back(SelectableArticle{
                        std::move(en.title), std::move(en.author), std::move(en.published_at),
                        std::move(en.url), std::move(en.content), en.id, false
                    });
                }

                run_on_ui([&, out = std::move(out)]() mutable {
                    loading_mf_entries = false;
                    mf_articles = std::move(out);
                    rebuild_filtered_view(mf_articles, mf_filter, fuzzy_search, mf_view);
                    set_status("Loaded " + std::to_string(mf_articles.size()) + " Miniflux entries");
                });
            } catch (const std::exception& e) {
                std::string err = e.what() ? e.what() : "Unknown error";
                run_on_ui([&, err]() {
                    loading_mf_entries = false;
                    set_status("Miniflux Error: " + err, true);
                });
            } catch (...) {
                run_on_ui([&]() {
                    loading_mf_entries = false;
                    set_status("Miniflux Error: unexpected failure", true);
                });
            }
        });
    };

    MenuOption mf_cat_mopt;
    mf_cat_mopt.entries = &mf_cat_labels;
    mf_cat_mopt.selected = &selected_mf_cat_idx;
    mf_cat_mopt.on_enter = fetch_mf_entries;
    auto mf_cat_menu = Menu(std::move(mf_cat_mopt));

    RadioboxOption mf_status_ropt;
    mf_status_ropt.entries = &mf_status_options;
    mf_status_ropt.selected = &mf_status_idx;
    mf_status_ropt.on_change = [&]() {
        persist_state("state", "last_miniflux_status_filter", std::to_string(mf_status_idx));
    };
    auto mf_status_radio = Radiobox(std::move(mf_status_ropt));
    auto mf_starred_cb = Checkbox("Starred only", &mf_starred_only);

    auto mf_filter_input = make_input(&mf_filter, "Filter entries (or server search)...", false, [&]() {
        rebuild_filtered_view(mf_articles, mf_filter, fuzzy_search, mf_view);
    });
    auto mf_article_menu = make_article_menu(mf_articles, mf_view, mf_filter);
    auto mf_book_dd = make_book_dropdown();

    auto mf_btn_cats = Button("Fetch Categories", fetch_mf_categories);
    auto mf_btn_entries = Button("Fetch Entries", fetch_mf_entries);
    auto mf_btn_all = Button("Select All", [&]() {
        select_visible(mf_articles, mf_view, mf_filter, true);
    });
    auto mf_btn_none = Button("Select None", [&]() {
        select_visible(mf_articles, mf_view, mf_filter, false);
    });
    auto mf_btn_stage = Button("Stage Selected", [&]() {
        run_stage(mf_articles, "Miniflux entries", [&](const StageItem& it) {
            return miniflux_fetcher_.fetch_original_content(it.source_id).get();
        });
    });

    auto mf_container = Container::Vertical({
        Container::Horizontal({mf_book_dd, mf_btn_cats, mf_btn_entries, mf_starred_cb}),
        Container::Horizontal({mf_status_radio, mf_filter_input}),
        Container::Horizontal({mf_cat_menu, mf_article_menu}),
        Container::Horizontal({mf_btn_all, mf_btn_none, mf_btn_stage}),
    });

    auto mf_renderer = Renderer(mf_container, [&]() {
        return vbox({
            hbox({
                text(" Book: ") | bold | vcenter,
                mf_book_dd->Render(),
                text("  "),
                mf_btn_cats->Render(),
                text(" "),
                mf_btn_entries->Render(),
                text("  "),
                mf_starred_cb->Render() | vcenter,
            }),
            separator(),
            hbox({
                text(" Status: ") | bold | vcenter,
                mf_status_radio->Render(),
                text("  Search: ") | bold | vcenter,
                mf_filter_input->Render() | flex,
            }),
            separator(),
            hbox({
                vbox({
                    text(" Categories (" + std::to_string(mf_cat_labels.size()) + ")") | bold,
                    mf_cat_menu->Render() | vscroll_indicator | frame | flex,
                }) | size(WIDTH, GREATER_THAN, 26) | size(WIDTH, LESS_THAN, 40) | border,
                vbox({
                    text(" Entries (" + std::to_string(mf_view.labels.size()) + ")  [Space/Enter: toggle]") | bold,
                    mf_article_menu->Render() | vscroll_indicator | frame | flex,
                }) | flex | border,
            }) | flex,
            hbox({
                mf_btn_all->Render(),
                text(" "),
                mf_btn_none->Render(),
                filler(),
                mf_btn_stage->Render(),
            }),
        }) | flex;
    });

    // ═════════════════════════════════════════════════════════════════════
    // TAB 2: RSS FEEDS
    // ═════════════════════════════════════════════════════════════════════
    std::vector<RssFeed> rss_feeds;
    std::vector<std::string> rss_feed_labels;
    int selected_rss_feed_idx = 0;

    std::string new_rss_title;
    std::string new_rss_url;
    std::vector<SelectableArticle> rss_articles;
    std::string rss_filter;
    FilteredView rss_view;

    auto refresh_rss_feeds = [&]() {
        rss_feeds = db_.list_rss_feeds();
        rss_feed_labels.clear();
        selected_rss_feed_idx = 0;
        for (size_t i = 0; i < rss_feeds.size(); ++i) {
            rss_feed_labels.push_back(rss_feeds[i].title);
            if (rss_feeds[i].id == last_rss_feed_id) {
                selected_rss_feed_idx = static_cast<int>(i);
            }
        }
    };
    refresh_rss_feeds();

    auto fetch_rss_entries = [&]() {
        if (is_busy()) return;
        if (selected_rss_feed_idx < 0 || selected_rss_feed_idx >= static_cast<int>(rss_feeds.size())) {
            set_status("Select an RSS feed first.", true);
            return;
        }
        const auto& feed = rss_feeds[selected_rss_feed_idx];
        last_rss_feed_id = feed.id;
        persist_state("state", "last_rss_feed_id", std::to_string(feed.id));

        int max_items_i = 50;
        if (!parse_non_negative_int(rss_max_articles_text, max_items_i)) {
            max_items_i = 50;
        }
        size_t max_items = static_cast<size_t>(max_items_i);
        std::string feed_url = feed.url;
        std::string feed_title = feed.title;

        loading_rss = true;
        set_status("Fetching RSS feed: " + feed_title + "...");
        worker_pool.launch([&, feed_url, max_items]() {
            try {
                auto items = rss_fetcher_.fetch_items(feed_url, max_items).get();
                if (worker_pool.is_shutting_down()) return;

                std::vector<SelectableArticle> out;
                out.reserve(items.size());
                for (auto& it : items) {
                    out.push_back(SelectableArticle{
                        std::move(it.title), std::move(it.author), std::move(it.date),
                        std::move(it.link), std::move(it.content_html), 0, false
                    });
                }

                run_on_ui([&, out = std::move(out)]() mutable {
                    loading_rss = false;
                    rss_articles = std::move(out);
                    rebuild_filtered_view(rss_articles, rss_filter, fuzzy_search, rss_view);
                    set_status("Loaded " + std::to_string(rss_articles.size()) + " RSS articles");
                });
            } catch (const std::exception& e) {
                std::string err = e.what() ? e.what() : "Unknown error";
                run_on_ui([&, err]() {
                    loading_rss = false;
                    set_status("RSS Error: " + err, true);
                });
            } catch (...) {
                run_on_ui([&]() {
                    loading_rss = false;
                    set_status("RSS Error: unexpected failure", true);
                });
            }
        });
    };

    MenuOption rss_feed_mopt;
    rss_feed_mopt.entries = &rss_feed_labels;
    rss_feed_mopt.selected = &selected_rss_feed_idx;
    rss_feed_mopt.on_enter = fetch_rss_entries;
    auto rss_feed_menu = Menu(std::move(rss_feed_mopt));

    auto rss_title_input = make_input(&new_rss_title, "Feed title...");
    auto rss_url_input = make_input(&new_rss_url, "https://.../feed.xml");
    auto rss_filter_input = make_input(&rss_filter, "Filter RSS entries...", false, [&]() {
        rebuild_filtered_view(rss_articles, rss_filter, fuzzy_search, rss_view);
    });
    auto rss_article_menu = make_article_menu(rss_articles, rss_view, rss_filter);
    auto rss_book_dd = make_book_dropdown();

    auto rss_btn_add = Button("Add Feed", [&]() {
        if (is_busy()) {
            set_status("Wait for background work to finish.", true);
            return;
        }
        std::string t = trim_copy(new_rss_title);
        std::string u = trim_copy(new_rss_url);
        if (t.empty() || u.empty()) {
            set_status("Feed title and URL are required.", true);
            return;
        }
        if (!db_.add_rss_feed(t, u)) {
            set_status("RSS feed already exists: " + u, true);
            return;
        }
        new_rss_title.clear();
        new_rss_url.clear();
        confirm_action.clear();
        refresh_rss_feeds();
        set_status("Added RSS feed: " + t);
    });
    auto rss_btn_fetch = Button("Fetch Feed", fetch_rss_entries);
    auto rss_btn_remove = Button("Remove Feed", [&]() {
        if (is_busy()) {
            set_status("Wait for background work to finish.", true);
            return;
        }
        if (selected_rss_feed_idx < 0 || selected_rss_feed_idx >= static_cast<int>(rss_feeds.size())) {
            set_status("No RSS feed selected.", true);
            return;
        }
        int64_t fid = rss_feeds[selected_rss_feed_idx].id;
        std::string fname = rss_feeds[selected_rss_feed_idx].title;
        std::string token = "remove_feed:" + std::to_string(fid);
        if (confirm_action != token) {
            confirm_action = token;
            set_status("Press Remove Feed again to confirm removing \"" + fname + "\"", true);
            return;
        }
        confirm_action.clear();
        db_.delete_rss_feed(fid);
        if (last_rss_feed_id == fid) {
            last_rss_feed_id = -1;
            persist_state("state", "last_rss_feed_id", "-1");
            rss_articles.clear();
            rebuild_filtered_view(rss_articles, rss_filter, fuzzy_search, rss_view);
        }
        refresh_rss_feeds();
        set_status("Removed RSS feed: " + fname);
    });
    auto rss_btn_all = Button("Select All", [&]() {
        select_visible(rss_articles, rss_view, rss_filter, true);
    });
    auto rss_btn_none = Button("Select None", [&]() {
        select_visible(rss_articles, rss_view, rss_filter, false);
    });
    auto rss_btn_stage = Button("Stage Selected", [&]() {
        run_stage(rss_articles, "RSS entries", [&](const StageItem& it) {
            return it.url.empty() ? std::string{} : html_extractor_.fetch_and_extract(it.url).get();
        });
    });

    auto rss_container = Container::Vertical({
        Container::Horizontal({rss_book_dd, rss_title_input, rss_url_input, rss_btn_add}),
        rss_filter_input,
        Container::Horizontal({rss_feed_menu, rss_article_menu}),
        Container::Horizontal({rss_btn_fetch, rss_btn_remove, rss_btn_all, rss_btn_none, rss_btn_stage}),
    });

    auto rss_renderer = Renderer(rss_container, [&]() {
        return vbox({
            hbox({
                text(" Book: ") | bold | vcenter,
                rss_book_dd->Render(),
                text("  Title: ") | bold | vcenter,
                rss_title_input->Render() | size(WIDTH, GREATER_THAN, 16),
                text(" URL: ") | bold | vcenter,
                rss_url_input->Render() | flex,
                text(" "),
                rss_btn_add->Render(),
            }),
            separator(),
            hbox({text(" Search: ") | bold, rss_filter_input->Render() | flex}),
            separator(),
            hbox({
                vbox({
                    text(" Feeds (" + std::to_string(rss_feed_labels.size()) + ")") | bold,
                    rss_feed_menu->Render() | vscroll_indicator | frame | flex,
                }) | size(WIDTH, GREATER_THAN, 26) | size(WIDTH, LESS_THAN, 40) | border,
                vbox({
                    text(" Articles (" + std::to_string(rss_view.labels.size()) + ")  [Space/Enter: toggle]") | bold,
                    rss_article_menu->Render() | vscroll_indicator | frame | flex,
                }) | flex | border,
            }) | flex,
            hbox({
                rss_btn_fetch->Render(),
                text(" "),
                rss_btn_remove->Render(),
                text("   "),
                rss_btn_all->Render(),
                text(" "),
                rss_btn_none->Render(),
                filler(),
                rss_btn_stage->Render(),
            }),
        }) | flex;
    });

    // ═════════════════════════════════════════════════════════════════════
    // TAB 3: BOOKS & CHAPTERS
    // ═════════════════════════════════════════════════════════════════════
    auto new_book_input = make_input(&new_book_title, "New book title...");
    auto books_book_dd = make_book_dropdown();

    auto remove_selected_chapter = [&]() {
        if (staging || building) {
            set_status("Cannot modify chapters while staging or building.", true);
            return;
        }
        if (selected_book_id == -1 || selected_chapter_idx < 0 ||
            selected_chapter_idx >= static_cast<int>(chapters.size())) {
            set_status("No chapter selected.", true);
            return;
        }
        int saved_idx = selected_chapter_idx;
        std::string title = chapters[saved_idx].subject;
        db_.delete_chapter(chapters[saved_idx].id);
        db_.renumber_chapters(selected_book_id);
        confirm_action.clear();
        refresh_books();
        if (!chapters.empty()) {
            selected_chapter_idx = std::min(saved_idx, static_cast<int>(chapters.size()) - 1);
        }
        set_status("Removed chapter: " + title);
    };

    MenuOption ch_mopt;
    ch_mopt.entries = &chapter_labels;
    ch_mopt.selected = &selected_chapter_idx;
    auto raw_chapter_menu = Menu(std::move(ch_mopt));
    auto chapter_menu = CatchEvent(raw_chapter_menu, [&](Event ev) {
        if (ev == Event::Character('r') || ev == Event::Character('R') || ev == Event::Delete) {
            remove_selected_chapter();
            return true;
        }
        return false;
    });

    auto books_btn_create = Button("Create Book", [&]() {
        if (staging || building) {
            set_status("Cannot create a book while staging or building.", true);
            return;
        }
        std::string title = trim_copy(new_book_title);
        if (title.empty()) {
            set_status("Book title cannot be empty.", true);
            return;
        }
        int64_t new_id = db_.create_book(title);
        selected_book_id = new_id;
        custom_output_path_edited = false;
        persist_state("general", "selected_book_id", std::to_string(new_id));
        new_book_title.clear();
        confirm_action.clear();
        refresh_books();
        set_status("Created book: " + title);
    });

    auto books_btn_delete = Button("Delete Book", [&]() {
        if (staging || building) {
            set_status("Cannot delete a book while staging or building.", true);
            return;
        }
        if (selected_book_id == -1) {
            set_status("No book selected.", true);
            return;
        }
        std::string name = current_book_title();
        std::string token = "delete_book:" + std::to_string(selected_book_id);
        if (confirm_action != token) {
            confirm_action = token;
            set_status("Press Delete Book again to confirm deleting \"" + name + "\"", true);
            return;
        }
        confirm_action.clear();
        db_.delete_book(selected_book_id);
        selected_book_id = -1;
        custom_output_path_edited = false;
        refresh_books();
        set_status("Deleted book: " + name);
    });

    auto move_selected_chapter = [&](int delta) {
        if (staging || building) {
            set_status("Cannot reorder chapters while staging or building.", true);
            return;
        }
        if (selected_book_id == -1 || selected_chapter_idx < 0 ||
            selected_chapter_idx >= static_cast<int>(chapters.size())) {
            return;
        }
        int target = selected_chapter_idx + delta;
        if (target < 0 || target >= static_cast<int>(chapters.size())) return;
        int64_t cid = chapters[selected_chapter_idx].id;
        if (db_.move_chapter(selected_book_id, cid, delta)) {
            confirm_action.clear();
            refresh_chapters();
            selected_chapter_idx = target;
        }
    };

    auto books_btn_up = Button("↑ Move Up", [&]() { move_selected_chapter(-1); });
    auto books_btn_down = Button("↓ Move Down", [&]() { move_selected_chapter(+1); });
    auto books_btn_autosort = Button("Auto-Sort", [&]() {
        if (staging || building) {
            set_status("Cannot sort chapters while staging or building.", true);
            return;
        }
        if (selected_book_id == -1) return;
        confirm_action.clear();
        db_.auto_sort_chapters(selected_book_id);
        refresh_chapters();
        set_status("Chapters auto-sorted by numeric title.");
    });
    auto books_btn_reverse = Button("Reverse Order", [&]() {
        if (staging || building) {
            set_status("Cannot reverse chapters while staging or building.", true);
            return;
        }
        if (selected_book_id == -1) return;
        confirm_action.clear();
        db_.reverse_chapter_order(selected_book_id);
        refresh_chapters();
        set_status("Reversed chapter order.");
    });
    auto books_btn_remove = Button("Remove Chapter", remove_selected_chapter);
    auto books_btn_clear = Button("Clear All Chapters", [&]() {
        if (staging || building) {
            set_status("Cannot clear chapters while staging or building.", true);
            return;
        }
        if (selected_book_id == -1 || chapters.empty()) return;
        std::string token = "clear_chapters:" + std::to_string(selected_book_id);
        if (confirm_action != token) {
            confirm_action = token;
            set_status("Press Clear All Chapters again to confirm clearing all chapters", true);
            return;
        }
        confirm_action.clear();
        db_.delete_all_chapters(selected_book_id);
        refresh_books();
        set_status("Cleared all chapters from book.");
    });

    auto books_container = Container::Vertical({
        Container::Horizontal({books_book_dd, new_book_input, books_btn_create, books_btn_delete}),
        chapter_menu,
        Container::Horizontal({
            books_btn_up, books_btn_down, books_btn_autosort,
            books_btn_reverse, books_btn_remove, books_btn_clear,
        }),
    });

    auto books_renderer = Renderer(books_container, [&]() {
        return vbox({
            hbox({
                text(" Active Book: ") | bold | vcenter,
                books_book_dd->Render(),
                text("  New Book: ") | bold | vcenter,
                new_book_input->Render() | flex,
                text(" "),
                books_btn_create->Render(),
                text(" "),
                books_btn_delete->Render(),
            }),
            separator(),
            vbox({
                text(" Chapters (" + std::to_string(chapters.size()) + ")  [r/Del: remove]") | bold,
                chapter_menu->Render() | vscroll_indicator | frame | flex,
            }) | flex | border,
            hbox({
                books_btn_up->Render(),
                text(" "),
                books_btn_down->Render(),
                text(" "),
                books_btn_autosort->Render(),
                text(" "),
                books_btn_reverse->Render(),
                filler(),
                books_btn_remove->Render(),
                text(" "),
                books_btn_clear->Render(),
            }),
        }) | flex;
    });

    // ═════════════════════════════════════════════════════════════════════
    // TAB 4: BUILD EPUB
    // ═════════════════════════════════════════════════════════════════════
    auto build_book_dd = make_book_dropdown();
    auto build_author_input = make_input(&epub_author, "Unknown");
    auto build_publisher_input = make_input(&epub_publisher, "Endpaper");
    auto build_path_input = make_input(&epub_output_path, "Auto output path...", false, [&]() {
        custom_output_path_edited = true;
    });
    auto build_append_cb = Checkbox("Append to existing EPUB (this build only)", &append_mode);
    auto build_simplify_cb = [&]() {
        CheckboxOption opt = CheckboxOption::Simple();
        opt.label = "Simplify HTML for e-ink";
        opt.checked = &simplify_html_opt;
        opt.on_change = [&]() {
            config_.set("epub", "simplify", simplify_html_opt ? "true" : "false");
            config_.save();
        };
        return Checkbox(std::move(opt));
    }();
    auto build_images_cb = [&]() {
        CheckboxOption opt = CheckboxOption::Simple();
        opt.label = "Embed & optimize images (B&W JPEG <=480px)";
        opt.checked = &keep_images_opt;
        opt.on_change = [&]() {
            config_.set("epub", "keep_images", keep_images_opt ? "true" : "false");
            config_.save();
        };
        return Checkbox(std::move(opt));
    }();

    auto build_btn_reset_path = Button("Reset Path", [&]() {
        custom_output_path_edited = false;
        update_epub_path(true);
    });

    auto build_btn_start = Button("Build EPUB", [&]() {
        if (is_busy()) {
            set_status("Another background operation is running.", true);
            return;
        }
        if (selected_book_id == -1) {
            set_status("No book selected.", true);
            return;
        }
        auto chs = db_.get_chapters(selected_book_id);
        if (chs.empty()) {
            set_status("Book has no staged chapters.", true);
            return;
        }

        BuildOptions opts;
        std::string a = trim_copy(epub_author);
        std::string p = trim_copy(epub_publisher);
        opts.author = a.empty() ? "Unknown" : a;
        opts.publisher = p.empty() ? "Endpaper" : p;
        opts.explicit_path = trim_copy(epub_output_path);
        opts.default_folder = trim_copy(default_folder);
        opts.append = append_mode;
        opts.simplify = simplify_html_opt;
        opts.keep_images = keep_images_opt;

        std::string title = current_book_title();
        if (title.empty()) title = "book";

        building = true;
        set_status("Building EPUB: " + title + "...");

        worker_pool.launch([&, title, chs = std::move(chs), opts]() mutable {
            try {
                auto result = build_book_epub(
                    title, chs, opts,
                    [&](const std::string& step) {
                        run_on_ui([&, step]() { set_status(step); });
                    },
                    worker_pool.stop_flag());

                run_on_ui([&, result, opts]() {
                    building = false;
                    set_status(format_build_summary(result, opts.keep_images), false);
                });
            } catch (const std::exception& e) {
                std::string err = e.what() ? e.what() : "Build failure";
                run_on_ui([&, err]() {
                    building = false;
                    set_status("Build Error: " + err, true);
                });
            } catch (...) {
                run_on_ui([&]() {
                    building = false;
                    set_status("Build Error: unexpected failure", true);
                });
            }
        });
    });

    auto build_container = Container::Vertical({
        build_book_dd,
        build_author_input,
        build_publisher_input,
        Container::Horizontal({build_path_input, build_btn_reset_path}),
        build_append_cb,
        build_simplify_cb,
        build_images_cb,
        build_btn_start,
    });

    auto build_renderer = Renderer(build_container, [&]() {
        return vbox({
            hbox({text(" Target Book:     ") | bold | vcenter, build_book_dd->Render()}),
            hbox({text(" Chapters Ready:  ") | bold, text(std::to_string(chapters.size()))}),
            separator(),
            hbox({text(" Author:          ") | bold, build_author_input->Render() | flex}),
            hbox({text(" Publisher:       ") | bold, build_publisher_input->Render() | flex}),
            hbox({
                text(" Output File:     ") | bold | vcenter,
                build_path_input->Render() | flex,
                text(" "),
                build_btn_reset_path->Render(),
            }),
            separator(),
            build_append_cb->Render(),
            build_simplify_cb->Render(),
            build_images_cb->Render(),
            filler(),
            hbox({build_btn_start->Render(), filler()}),
        }) | border | flex;
    });

    // ═════════════════════════════════════════════════════════════════════
    // TAB 5: SETTINGS
    // ═════════════════════════════════════════════════════════════════════
    // Miniflux URL first so F6 places focus cleanly on miniflux_server_url
    auto set_mf_url_input = make_input(&mf_url_str, "https://miniflux.example.com");
    auto set_mf_token_input = make_input(&mf_token_str, "API token...", true);
    auto set_folder_input = make_input(&default_folder, "~/EPUBs");
    auto set_author_input = make_input(&epub_author, "Unknown");
    auto set_publisher_input = make_input(&epub_publisher, "Endpaper");
    auto set_rss_max_input = make_input(&rss_max_articles_text, "50");
    auto set_stub_thresh_input = make_input(&stub_threshold_text, "1000");
    auto set_delay_input = make_input(&request_delay_text, "1000");

    auto set_auto_kmail_cb = Checkbox("Auto-fetch KMail folders on start", &auto_fetch_kmail);
    auto set_auto_mf_cb = Checkbox("Auto-fetch Miniflux categories on start", &auto_fetch_miniflux);
    auto set_cache_cb = Checkbox("Cache folders & categories locally", &cache_metadata);
    auto set_remember_cb = Checkbox("Remember active tab, book & folder state", &remember_state);
    auto set_fuzzy_cb = [&]() {
        CheckboxOption opt = CheckboxOption::Simple();
        opt.label = "Fuzzy article search (off = regex)";
        opt.checked = &fuzzy_search;
        opt.on_change = [&]() {
            rebuild_filtered_view(kmail_articles, kmail_filter, fuzzy_search, kmail_view);
            rebuild_filtered_view(mf_articles, mf_filter, fuzzy_search, mf_view);
            rebuild_filtered_view(rss_articles, rss_filter, fuzzy_search, rss_view);
        };
        return Checkbox(std::move(opt));
    }();
    auto set_stub_cb = Checkbox("Resolve short stub articles from original URL", &enable_stub_resolving);

    auto set_btn_refresh_akonadi = Button("Refresh Akonadi Status", [&]() {
        akonadi_available = fetcher_.is_available();
        set_status(akonadi_available ? "Akonadi service online." : "Akonadi service unavailable.",
                   !akonadi_available);
    });

    auto set_btn_save = Button("Save Settings", [&]() {
        int rss_max = 0, stub_thresh = 0, delay_ms = 0;
        if (!parse_non_negative_int(rss_max_articles_text, rss_max)) {
            set_status("RSS max articles must be a non-negative integer.", true);
            return;
        }
        if (!parse_non_negative_int(stub_threshold_text, stub_thresh)) {
            set_status("Stub threshold must be a non-negative integer.", true);
            return;
        }
        if (!parse_non_negative_int(request_delay_text, delay_ms)) {
            set_status("Request delay (ms) must be a non-negative integer.", true);
            return;
        }

        mf_url_str = trim_copy(mf_url_str);
        mf_token_str = trim_copy(mf_token_str);
        default_folder = trim_copy(default_folder);
        rss_max_articles_text = std::to_string(rss_max);
        stub_threshold_text = std::to_string(stub_thresh);
        request_delay_text = std::to_string(delay_ms);

        config_.set("miniflux", "url", mf_url_str);
        config_.set("miniflux", "token", mf_token_str);
        config_.set("general", "default_folder", default_folder);
        config_.set("general", "auto_fetch_kmail", auto_fetch_kmail ? "true" : "false");
        config_.set("general", "auto_fetch_miniflux", auto_fetch_miniflux ? "true" : "false");
        config_.set("general", "cache_metadata", cache_metadata ? "true" : "false");
        config_.set("general", "remember_state", remember_state ? "true" : "false");
        config_.set("ui", "fuzzy_search", fuzzy_search ? "true" : "false");
        config_.set("epub", "author", trim_copy(epub_author).empty() ? "Unknown" : trim_copy(epub_author));
        config_.set("epub", "publisher", trim_copy(epub_publisher).empty() ? "Endpaper" : trim_copy(epub_publisher));
        config_.set("epub", "simplify", simplify_html_opt ? "true" : "false");
        config_.set("epub", "keep_images", keep_images_opt ? "true" : "false");
        config_.set("rss", "max_articles", rss_max_articles_text);
        config_.set("fetching", "enable_stub_resolving", enable_stub_resolving ? "true" : "false");
        config_.set("fetching", "stub_char_threshold", stub_threshold_text);
        config_.set("fetching", "request_delay_ms", request_delay_text);

        config_.save();
        miniflux_fetcher_.set_credentials(mf_url_str, mf_token_str);
        update_epub_path(true);
        set_status("Settings saved to " + config_.config_file().string());
    });

    auto settings_container = Container::Vertical({
        set_mf_url_input,
        set_mf_token_input,
        set_folder_input,
        set_author_input,
        set_publisher_input,
        set_rss_max_input,
        set_stub_thresh_input,
        set_delay_input,
        set_auto_kmail_cb,
        set_auto_mf_cb,
        set_cache_cb,
        set_remember_cb,
        set_fuzzy_cb,
        set_stub_cb,
        Container::Horizontal({set_btn_save, set_btn_refresh_akonadi}),
    });

    auto settings_renderer = Renderer(settings_container, [&]() {
        return vbox({
            hbox({text(" Miniflux URL:       ") | bold, set_mf_url_input->Render() | flex}),
            hbox({text(" Miniflux Token:     ") | bold, set_mf_token_input->Render() | flex}),
            hbox({text(" Default Output Dir: ") | bold, set_folder_input->Render() | flex}),
            hbox({text(" Default Author:     ") | bold, set_author_input->Render() | flex}),
            hbox({text(" Default Publisher:  ") | bold, set_publisher_input->Render() | flex}),
            hbox({text(" RSS Max Articles:   ") | bold, set_rss_max_input->Render() | size(WIDTH, EQUAL, 12),
                  text("  (0 = unlimited)") | dim}),
            hbox({text(" Stub Char Min:      ") | bold, set_stub_thresh_input->Render() | size(WIDTH, EQUAL, 12),
                  text("  Delay (ms): ") | bold, set_delay_input->Render() | size(WIDTH, EQUAL, 10)}),
            separator(),
            set_auto_kmail_cb->Render(),
            set_auto_mf_cb->Render(),
            set_cache_cb->Render(),
            set_remember_cb->Render(),
            set_fuzzy_cb->Render(),
            set_stub_cb->Render(),
            separator(),
            hbox({
                set_btn_save->Render(),
                text("  "),
                set_btn_refresh_akonadi->Render(),
                filler(),
                text(akonadi_available ? "Akonadi: Online " : "Akonadi: Offline ") |
                    color(akonadi_available ? Color::Green : Color::Red),
            }),
        }) | vscroll_indicator | frame | border | flex;
    });

    // ═════════════════════════════════════════════════════════════════════
    // MAIN TABS & KEYBOARD DISPATCH
    // ═════════════════════════════════════════════════════════════════════
    std::vector<std::string> tab_names = {
        " [F1] KMail ", " [F2] Miniflux ", " [F3] RSS ",
        " [F4] Books ", " [F5] Build ", " [F6] Settings "
    };

    MenuOption tab_mopt = MenuOption::HorizontalAnimated();
    tab_mopt.entries = &tab_names;
    tab_mopt.selected = &tab_index;
    tab_mopt.on_change = [&]() {
        confirm_action.clear();
        persist_state("state", "last_page_index", std::to_string(tab_index));
    };
    auto tab_menu = Menu(std::move(tab_mopt));

    auto tab_content = Container::Tab({
        kmail_renderer,
        mf_renderer,
        rss_renderer,
        books_renderer,
        build_renderer,
        settings_renderer,
    }, &tab_index);

    auto main_container = Container::Vertical({tab_menu, tab_content}, &main_sel_main);

    auto switch_tab = [&](int idx) {
        tab_index = std::clamp(idx, 0, 5);
        confirm_action.clear();
        persist_state("state", "last_page_index", std::to_string(tab_index));
        main_sel_main = 1;
        // Take focus into first interactive control of Settings tab immediately
        tab_content->SetActiveChild(tab_content->ChildAt(tab_index));
    };

    auto request_quit = [&]() {
        shutting_down.store(true);
        worker_pool.request_stop();
        fetcher_.stop();
        screen.ExitLoopClosure()();
    };

    auto main_component = CatchEvent(main_container, [&](Event ev) {
        if (ev == Event::CtrlQ) {
            request_quit();
            return true;
        }
        if (ev == Event::F1) { switch_tab(0); return true; }
        if (ev == Event::F2) { switch_tab(1); return true; }
        if (ev == Event::F3) { switch_tab(2); return true; }
        if (ev == Event::F4) { switch_tab(3); return true; }
        if (ev == Event::F5) { switch_tab(4); return true; }
        if (ev == Event::F6) { switch_tab(5); return true; }

        if (ev == Event::Escape) {
            if (main_sel_main == 1) {
                main_sel_main = 0;
                confirm_action.clear();
                return true;
            }
            request_quit();
            return true;
        }

        if (main_sel_main == 0 || !any_input_focused()) {
            if (ev == Event::Character('q') || ev == Event::Character('Q')) {
                request_quit();
                return true;
            }
            if (ev == Event::Character('1')) { switch_tab(0); return true; }
            if (ev == Event::Character('2')) { switch_tab(1); return true; }
            if (ev == Event::Character('3')) { switch_tab(2); return true; }
            if (ev == Event::Character('4')) { switch_tab(3); return true; }
            if (ev == Event::Character('5')) { switch_tab(4); return true; }
            if (ev == Event::Character('6')) { switch_tab(5); return true; }
        }

        return false;
    });

    auto root_renderer = Renderer(main_component, [&]() {
        std::string active_book = current_book_title();
        return vbox({
            hbox({
                text(" ENDPAPER ") | bold | inverted,
                text(" "),
                tab_menu->Render(),
                filler(),
                text(active_book.empty() ? "Book: (none) " : "Book: " + active_book + " ") | dim,
            }),
            separator(),
            tab_content->Render() | flex,
            separator(),
            hbox({
                text(" " + status_msg) | color(status_error ? Color::Red : Color::Green) | flex,
                text(is_busy() ? " [BUSY] " : " ") | color(Color::Yellow),
                text(" [Ctrl+Q]uit [F1-F6]tabs [Esc]bar ") | dim,
            }),
        });
    });

    // ── Initial load & optional auto-fetch ───────────────────────────────
    refresh_books();
    if (auto_fetch_kmail && akonadi_available) {
        fetch_kmail_folders();
    } else if (auto_fetch_miniflux && !trim_copy(mf_url_str).empty()) {
        fetch_mf_categories();
    }

    screen.Loop(root_renderer);

    // Ensure workers stop before local state goes out of scope
    shutting_down.store(true);
    worker_pool.request_stop();
    fetcher_.stop();
    worker_pool.stop_and_join();
}

} // namespace endpaper
