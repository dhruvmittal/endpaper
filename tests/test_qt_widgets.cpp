#include "akonadi_fetcher.h"
#include "config.h"
#include "database.h"
#include "html_extractor.h"
#include "main_window.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"
#include "settings_dialog.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QTableView>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace endpaper;

namespace {

struct XdgEnvGuard {
    fs::path root;
    bool hadCfg = false, hadData = false, hadState = false;
    std::string oldCfg, oldData, oldState;

    explicit XdgEnvGuard(const fs::path& r) : root(r) {
        if (const char* v = std::getenv("XDG_CONFIG_HOME")) { hadCfg = true; oldCfg = v; }
        if (const char* v = std::getenv("XDG_DATA_HOME")) { hadData = true; oldData = v; }
        if (const char* v = std::getenv("XDG_STATE_HOME")) { hadState = true; oldState = v; }

        fs::remove_all(root);
        fs::create_directories(root / "cfg");
        fs::create_directories(root / "data");
        fs::create_directories(root / "state");

        setenv("XDG_CONFIG_HOME", (root / "cfg").c_str(), 1);
        setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
        setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
    }

    ~XdgEnvGuard() {
        if (hadCfg) setenv("XDG_CONFIG_HOME", oldCfg.c_str(), 1); else unsetenv("XDG_CONFIG_HOME");
        if (hadData) setenv("XDG_DATA_HOME", oldData.c_str(), 1); else unsetenv("XDG_DATA_HOME");
        if (hadState) setenv("XDG_STATE_HOME", oldState.c_str(), 1); else unsetenv("XDG_STATE_HOME");
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    XdgEnvGuard(const XdgEnvGuard&) = delete;
    XdgEnvGuard& operator=(const XdgEnvGuard&) = delete;
};

void strictMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (type == QtWarningMsg && msg == "This plugin does not support propagateSizeHints()") {
        return;
    }
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        std::cerr << "Qt Fatal Warning/Error (" << (ctx.function ? ctx.function : "") << "): "
                  << msg.toStdString() << std::endl;
        std::abort();
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    qInstallMessageHandler(strictMessageHandler);

    XdgEnvGuard xdg(fs::temp_directory_path() / "endpaper_qt_widgets_test");

    QApplication app(argc, argv);

    Config config;
    Database db(config.db_path());
    db.init();

    AkonadiFetcher fetcher;
    RssFetcher rssFetcher;
    MinifluxFetcher mfFetcher;
    HtmlExtractor htmlExtractor;

    MainWindow window(config, db, fetcher, rssFetcher, mfFetcher, htmlExtractor);
    window.show();
    QApplication::processEvents();

    // ── Book & Chapter Operations ────────────────────────────────────────
    assert(window.createBookByTitle("Widget Test Book"));
    int64_t bid = window.selectedBookId();
    assert(bid > 0);
    assert(window.chapterCount() == 0);

    db.add_chapter(bid, "Part 10: Ten", "Alice", "<p>10</p>", 1);
    db.add_chapter(bid, "Part 2: Two", "Bob", "<p>2</p>", 2);
    db.add_chapter(bid, "Part 1: One", "Carol", "<p>1</p>", 3);
    window.refreshBooks();
    assert(window.chapterCount() == 3);

    window.chapterList()->setCurrentRow(0);
    assert(window.moveChapterByDelta(+1));
    auto chs = db.get_chapters(bid);
    assert(chs[0].subject == "Part 2: Two");
    assert(chs[1].subject == "Part 10: Ten");
    assert(window.moveChapterByDelta(-1));
    assert(db.get_chapters(bid)[0].subject == "Part 10: Ten");

    window.autoSortChapters();
    chs = db.get_chapters(bid);
    assert(chs[0].subject == "Part 1: One");
    assert(chs[1].subject == "Part 2: Two");
    assert(chs[2].subject == "Part 10: Ten");

    window.reverseChapterOrder();
    chs = db.get_chapters(bid);
    assert(chs[0].subject == "Part 10: Ten");
    assert(chs[2].subject == "Part 1: One");

    // Simulate InternalMove drag-drop sequence (insert cloned item, then remove original)
    {
        QListWidget* list = window.chapterList();
        QListWidgetItem* cloned = list->item(0)->clone();
        list->insertItem(3, cloned);
        delete list->takeItem(0);
        list->setCurrentRow(2);
        QApplication::processEvents();

        auto afterDrag = db.get_chapters(bid);
        assert(afterDrag.size() == 3);
        assert(afterDrag[0].subject == "Part 2: Two");
        assert(afterDrag[1].subject == "Part 1: One");
        assert(afterDrag[2].subject == "Part 10: Ten");
    }

    window.chapterList()->setCurrentRow(0);
    assert(window.deleteSelectedChapter());
    assert(window.chapterCount() == 2);

    // ── Source Switching & Per-Source Filtering / Selection ───────────────
    window.setActiveSource(0);
    assert(window.activeSource() == 0);
    window.filterEdit()->setText("rust");

    std::vector<ArticleRow> emailRows = {
        {1, "Rust Async", "Jane", "2026-01-01", "", "<p>a</p>", false},
        {2, "Python Typing", "Guido", "2026-01-02", "", "<p>b</p>", false},
        {3, "Rust Ownership", "Steve", "2026-01-03", "", "<p>c</p>", false},
    };
    window.articleModel(0)->setArticles(emailRows);
    assert(window.articleProxy()->rowCount() == 2);

    // Space key toggles highlighted row via MainWindow event filter
    window.articleView()->selectRow(0);
    QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
    QApplication::sendEvent(window.articleView(), &spacePress);
    assert(window.articleModel(0)->selectedCount() == 1);

    // Regex mode filter
    window.articleProxy()->setFuzzySearch(false);
    window.filterEdit()->setText("^rust.*async$");
    assert(window.articleProxy()->rowCount() == 1);
    window.articleProxy()->setFuzzySearch(true);

    // Switch sources and confirm per-source filter restoration
    window.setActiveSource(1);
    assert(window.activeSource() == 1);
    assert(window.filterEdit()->text().isEmpty());
    window.filterEdit()->setText("mf_query");

    window.setActiveSource(2);
    assert(window.activeSource() == 2);

    window.setActiveSource(0);
    assert(window.filterEdit()->text() == "^rust.*async$");

    window.setActiveSource(1);
    assert(window.filterEdit()->text() == "mf_query");

    // ── SettingsDialog Instantiation ─────────────────────────────────────
    {
        SettingsDialog dlg(config, fetcher, &window);
        QApplication::processEvents();
    }

    std::cout << "✓ All Qt Widgets workspace tests passed!\n";
    return 0;
}
