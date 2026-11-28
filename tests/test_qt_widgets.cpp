#include "akonadi_fetcher.h"
#include "config.h"
#include "database.h"
#include "html_extractor.h"
#include "main_window.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"
#include "settings_dialog.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTableView>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

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

    // ── Toolbar Layout, Global Menu vs Hamburger Fallback & Queue Toggle ──
    QToolBar* tb = window.mainToolBar();
    QToolButton* queueBtn = window.queueToggleButton();
    QToolButton* menuBtn = window.menuButton();
    QComboBox* bookCombo = window.bookCombo();
    assert(tb && queueBtn && menuBtn && bookCombo);
    if (window.hasGlobalMenu()) {
        // Qt hides the in-window QMenuBar widget itself once a native QPlatformMenuBar takes over
        assert(window.menuBar()->isNativeMenuBar());
        assert(window.menuBar()->actions().size() == 6);
        assert(!menuBtn->isVisibleTo(tb));
    } else {
        assert(window.menuBar()->isHidden());
        assert(menuBtn->isVisibleTo(tb));
        assert(menuBtn->menu() != nullptr);
    }
    assert(bookCombo->x() + bookCombo->width() < queueBtn->x());
    for (QObject* child : tb->children()) {
        if (auto* w = qobject_cast<QWidget*>(child); w && w != queueBtn && w->isVisibleTo(tb)) {
            assert(w->x() <= queueBtn->x());
        }
    }
    assert(window.queuePane()->isVisible());
    queueBtn->click();
    QApplication::processEvents();
    assert(!window.queuePane()->isVisible());
    queueBtn->click();
    QApplication::processEvents();
    assert(window.queuePane()->isVisible());

    // ── Book & Chapter Operations ────────────────────────────────────────
    assert(window.createBookByTitle("Widget Test Book"));
    int64_t bid = window.selectedBookId();
    assert(bid > 0);
    assert(window.chapterCount() == 0);
    QApplication::processEvents();
    assert(window.chapterEmptyLabel()->isVisible());

    db.add_chapter(bid, "Part 10: Ten", "Alice", "<p>10</p>", 1);
    db.add_chapter(bid, "Part 2: Two", "Bob", "<p>2</p>", 2);
    db.add_chapter(bid, "Part 1: One", "Carol", "<p>1</p>", 3);
    window.refreshBooks();
    assert(window.chapterCount() == 3);
    QApplication::processEvents();
    assert(!window.chapterEmptyLabel()->isVisible());

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
    QApplication::processEvents();
    assert(window.articleEmptyLabel()->isVisible());
    window.filterEdit()->setText("rust");

    std::vector<ArticleRow> emailRows = {
        {1, "Rust Async", "Jane", "2026-01-01", "", "<p>a</p>", false},
        {2, "Python Typing", "Guido", "2026-01-02", "", "<p>b</p>", false},
        {3, "Rust Ownership", "Steve", "2026-01-03", "", "<p>c</p>", false},
    };
    window.articleModel(0)->setArticles(emailRows);
    assert(window.articleProxy()->rowCount() == 2);
    assert(!window.articleEmptyLabel()->isVisible());

    window.filterEdit()->setText("zzzz_no_match");
    assert(window.articleProxy()->rowCount() == 0);
    assert(window.articleEmptyLabel()->isVisible());
    assert(window.articleEmptyLabel()->text().contains("filter", Qt::CaseInsensitive));
    window.filterEdit()->setText("rust");
    assert(!window.articleEmptyLabel()->isVisible());

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

    // ── Chronological RFC 2822 Date Sort & Checkmark Clear After Staging ──
    window.setActiveSource(0);
    window.filterEdit()->clear();
    {
        std::vector<ArticleRow> dateRows = {
            {101, "Mon Post", "A", "Mon, 23 Nov 2026 10:00:00 +0000", "", "<p>m</p>", false},
            {102, "Wed Post", "B", "Wed, 18 Nov 2026 10:00:00 +0000", "", "<p>w</p>", false},
            {103, "Fri Post", "C", "Fri, 20 Nov 2026 10:00:00 +0000", "", "<p>f</p>", false},
        };
        window.articleModel(0)->setArticles(dateRows);
        window.articleProxy()->sort(ArticleTableModel::DateColumn, Qt::AscendingOrder);
        assert(window.articleProxy()->rowCount() == 3);
        assert(window.articleProxy()->index(0, ArticleTableModel::TitleColumn).data().toString() == "Wed Post");
        assert(window.articleProxy()->index(1, ArticleTableModel::TitleColumn).data().toString() == "Fri Post");
        assert(window.articleProxy()->index(2, ArticleTableModel::TitleColumn).data().toString() == "Mon Post");

        int prevChapters = window.chapterCount();
        window.articleModel(0)->setRowsSelected({0, 1}, true);
        assert(window.articleModel(0)->selectedCount() == 2);
        QPushButton* stageBtn = nullptr;
        for (auto* b : window.findChildren<QPushButton*>()) {
            if (b->text().startsWith("Stage")) {
                stageBtn = b;
                break;
            }
        }
        assert(stageBtn && stageBtn->isEnabled() && stageBtn->text() == "Stage → (2)");
        stageBtn->click();

        QElapsedTimer timer;
        timer.start();
        while ((window.articleModel(0)->selectedCount() > 0 || window.chapterCount() < prevChapters + 2) &&
               timer.elapsed() < 3000) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(10);
        }
        assert(window.articleModel(0)->selectedCount() == 0);
        assert(window.chapterCount() == prevChapters + 2);
        assert(stageBtn->text() == "Stage → (0)");
    }

    // ── KMail Folder Tree, Pinned Group & Visibility Dialog ───────────────
    std::vector<FolderInfo> testFolders = {
        {1, -1, "Local Folders", "Local Folders"},
        {2, 1, "inbox", "Local Folders/inbox"},
        {3, 1, "trash", "Local Folders/trash"},
        {10, -1, "IMAP (me)", "IMAP (me)"},
        {11, 10, "Inbox", "IMAP (me)/Inbox"},
        {12, 10, "Newsletters", "IMAP (me)/Newsletters"},
        {13, 12, "Tech", "IMAP (me)/Newsletters/Tech"},
        {14, 10, "Drafts", "IMAP (me)/Drafts"},
    };
    db.save_folder_cache(testFolders);
    window.reloadKMailFolders();
    QApplication::processEvents();

    QTreeWidget* kmailTree = window.kmailFolderTree();
    assert(kmailTree);
    assert(kmailTree->topLevelItemCount() == 2); // 2 accounts, collapsed by default
    assert(!kmailTree->topLevelItem(0)->isExpanded());
    assert(!kmailTree->topLevelItem(1)->isExpanded());

    // Pin Newsletters (12) -> "Pinned" section appears first with Newsletters + account name
    window.setFolderPinned(12, true);
    assert(kmailTree->topLevelItemCount() == 3);
    assert(kmailTree->topLevelItem(0)->text(0) == "Pinned");
    assert(kmailTree->topLevelItem(0)->childCount() == 1);
    assert(kmailTree->topLevelItem(0)->child(0)->data(0, Qt::UserRole).toLongLong() == 12);
    assert(kmailTree->topLevelItem(0)->child(0)->text(0) == "Newsletters (IMAP (me))");
    assert(db.load_folder_prefs().pinned.count(12) == 1);

    // Hide Local Folders (1) -> account and children disappear and persist as hidden
    window.hideFolderSubtree(1);
    assert(kmailTree->topLevelItemCount() == 2); // Pinned + IMAP (me)
    {
        auto prefs = db.load_folder_prefs();
        assert(prefs.hidden.count(1) == 1 && prefs.hidden.count(2) == 1 && prefs.hidden.count(3) == 1);
    }

    // FolderVisibilityDialog: hideSystemFolders marks Drafts (14) hidden while keeping Inbox (11)/Newsletters (12)
    {
        FolderVisibilityDialog visDlg(db, testFolders, &window);
        visDlg.hideSystemFolders();
        visDlg.saveAndAccept();
        auto prefs = db.load_folder_prefs();
        assert(prefs.hidden.count(14) == 1);
        assert(prefs.hidden.count(11) == 0);
        assert(prefs.hidden.count(12) == 0);
        assert(prefs.pinned.count(12) == 1);
    }
    // FolderVisibilityDialog: unchecking sole subfolder Tech (13) must NOT hide parent Newsletters (12),
    // and checking inbox (2) under hidden Local Folders (1) must unhide ancestor Local Folders (1)
    {
        auto findById = [](QTreeWidget* tree, int64_t id) -> QTreeWidgetItem* {
            for (QTreeWidgetItemIterator it(tree); *it; ++it) {
                if ((*it)->data(0, Qt::UserRole).toLongLong() == id) return *it;
            }
            return nullptr;
        };

        FolderVisibilityDialog visDlg(db, testFolders, &window);
        QTreeWidget* visTree = visDlg.findChild<QTreeWidget*>();
        assert(visTree);
        QTreeWidgetItem* techItem = findById(visTree, 13);
        assert(techItem);
        techItem->setCheckState(0, Qt::Unchecked);
        visDlg.saveAndAccept();

        auto prefs = db.load_folder_prefs();
        assert(prefs.hidden.count(13) == 1);
        assert(prefs.hidden.count(12) == 0); // parent stays visible

        // Reopen and re-save: parent Newsletters (12) must remain visible
        FolderVisibilityDialog visDlg2(db, testFolders, &window);
        visDlg2.saveAndAccept();
        prefs = db.load_folder_prefs();
        assert(prefs.hidden.count(13) == 1);
        assert(prefs.hidden.count(12) == 0);

        // Checking child (2) under hidden account (1) reveals ancestor (1)
        FolderVisibilityDialog visDlg3(db, testFolders, &window);
        QTreeWidget* visTree3 = visDlg3.findChild<QTreeWidget*>();
        assert(visTree3);
        QTreeWidgetItem* inboxItem = findById(visTree3, 2);
        assert(inboxItem);
        inboxItem->setCheckState(0, Qt::Checked);
        visDlg3.saveAndAccept();
        prefs = db.load_folder_prefs();
        assert(prefs.hidden.count(1) == 0 && prefs.hidden.count(2) == 0);
    }
    window.reloadKMailFolders();
    assert(kmailTree->topLevelItemCount() == 3); // Pinned + Local Folders + IMAP (me)

    // ── SettingsDialog Instantiation ─────────────────────────────────────
    {
        SettingsDialog dlg(config, db, fetcher, testFolders, &window);
        QApplication::processEvents();
    }

    std::cout << "✓ All Qt Widgets workspace tests passed!\n";
    return 0;
}
