#pragma once

#include "akonadi_fetcher.h"
#include "build_pipeline.h"
#include "config.h"
#include "database.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"

#include <QAbstractTableModel>
#include <QMainWindow>
#include <QSortFilterProxyModel>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTimer;
class QToolButton;
class QWidget;

namespace endpaper {

class HtmlExtractor;

struct ArticleRow {
    int64_t source_id = 0;
    std::string title;
    std::string author;
    std::string date;
    std::string url;
    std::string html_body;
    bool selected = false;
};

/// Table model holding fetched articles for one source (KMail, Miniflux, or RSS).
class ArticleTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        TitleColumn = 0,
        AuthorColumn = 1,
        DateColumn = 2,
        ColumnCount = 3
    };

    explicit ArticleTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setArticles(std::vector<ArticleRow> rows);
    void clear();
    void toggleRows(const std::vector<int>& sourceRows);
    void setRowsSelected(const std::vector<int>& sourceRows, bool selected);

    [[nodiscard]] const ArticleRow* rowAt(int sourceRow) const;
    [[nodiscard]] int selectedCount() const { return m_selectedCount; }
    [[nodiscard]] std::vector<StageItem> selectedItems() const;
    [[nodiscard]] std::optional<StageItem> stageItemAt(int sourceRow) const;

signals:
    void selectionChanged();

private:
    void emitRowCheckRange(int firstRow, int lastRow);

    std::vector<ArticleRow> m_rows;
    int m_selectedCount = 0;
};

/// Proxy model filtering and sorting articles using build_pipeline::matches_article_filter.
class ArticleFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit ArticleFilterProxy(QObject* parent = nullptr);

    [[nodiscard]] QString filterString() const { return m_filterString; }
    void setFilterString(const QString& query);

    [[nodiscard]] bool fuzzySearch() const { return m_fuzzySearch; }
    void setFuzzySearch(bool fuzzy);

    [[nodiscard]] std::vector<int> visibleSourceRows() const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    template <typename Fn>
    void refreshFilter(Fn&& update);

    QString m_filterString;
    bool m_fuzzySearch = true;
};

/// Keyboard-first 3-pane desktop workspace matching the Endpaper TUI workflow.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(Config& config,
               Database& db,
               AkonadiFetcher& fetcher,
               RssFetcher& rssFetcher,
               MinifluxFetcher& minifluxFetcher,
               HtmlExtractor& htmlExtractor,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    // Accessors and helpers used by UI and automated widget tests
    [[nodiscard]] int activeSource() const { return m_activeSource; }
    [[nodiscard]] int64_t selectedBookId() const { return m_selectedBookId; }
    [[nodiscard]] int chapterCount() const;
    [[nodiscard]] ArticleTableModel* articleModel(int sourceIndex) const;
    [[nodiscard]] ArticleFilterProxy* articleProxy() const { return m_articleProxy; }
    [[nodiscard]] QTableView* articleView() const { return m_articleView; }
    [[nodiscard]] QListWidget* chapterList() const { return m_chapterList; }
    [[nodiscard]] QListWidget* rssFeedList() const { return m_rssFeedList; }
    [[nodiscard]] QLineEdit* filterEdit() const { return m_filterEdit; }

    void setActiveSource(int index);
    void refreshBooks();
    void refreshChapters();
    bool createBookByTitle(const QString& title);
    bool moveChapterByDelta(int delta);
    bool deleteSelectedChapter();
    void autoSortChapters();
    void reverseChapterOrder();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void createBookInteractive();
    void deleteActiveBookInteractive();
    void openSettings();
    void refreshCurrentSource();

    void onBookActivated(int comboIndex);
    void onMinifluxFilterChanged();
    void toggleRssAddBar(bool visible);
    void submitRssFeed();
    void removeSelectedRssFeed();

    void stageSelectedOrHighlighted();
    void onArticleDoubleClicked(const QModelIndex& proxyIndex);
    void updateStageButtonLabel();

    void clearAllChaptersInteractive();
    void syncDraggedChapterOrder();
    void updateOutputPlaceholder();
    void browseBuildOutputFolder();
    void buildEpubAction();

private:
    void setupUi();
    void setupShortcuts();
    void loadCachedMetadata();
    void loadFoldersIntoList(const std::vector<FolderInfo>& folders);
    void loadCategoriesIntoList(const std::vector<MinifluxCategory>& categories);
    void loadRssFeedsIntoList();
    void maybeAutoFetchActiveSource();

    void fetchFolders();
    void fetchEmails(int64_t collectionId);
    void fetchMinifluxCategories();
    void fetchMinifluxEntries();
    void fetchRssEntries(int64_t feedId);

    void stageItems(int sourceIndex, std::vector<StageItem> items);

    void setStatus(const QString& msg, bool isError = false);
    void setBusy(bool busy);
    void updateAkonadiFooter();
    void updateBookControlsState();
    void updateChapterButtonsState();
    void persistInt(const std::string& section, const std::string& key, int value);
    void persistInt64(const std::string& section, const std::string& key, int64_t value);

    [[nodiscard]] bool rememberState() const;
    [[nodiscard]] bool cacheMetadata() const;
    [[nodiscard]] bool autoFetchKMail() const;
    [[nodiscard]] bool autoFetchMiniflux() const;
    [[nodiscard]] bool fuzzySearch() const;
    [[nodiscard]] QString currentBookTitle() const;

    Config& m_config;
    Database& m_db;
    AkonadiFetcher& m_fetcher;
    RssFetcher& m_rssFetcher;
    MinifluxFetcher& m_minifluxFetcher;
    HtmlExtractor& m_htmlExtractor;

    WorkerPool m_workers;

    bool m_busy = false;
    bool m_akonadiAvailable = false;
    bool m_kmailAutoFetched = false;
    bool m_minifluxAutoFetched = false;
    bool m_rebuildingChapters = false;
    QTimer* m_dragSyncTimer = nullptr;

    int m_activeSource = 0; // 0 = KMail, 1 = Miniflux, 2 = RSS
    int64_t m_selectedBookId = -1;
    int64_t m_lastKMailCollectionId = -1;
    int64_t m_lastMfCategoryId = -1;
    int m_mfStatusFilter = 0; // 0 = All, 1 = Unread, 2 = Read
    int64_t m_lastRssFeedId = -1;

    std::vector<Book> m_books;
    std::vector<Chapter> m_chapters;
    std::vector<RssFeed> m_rssFeeds;

    // Toolbar
    QButtonGroup* m_sourceBtnGroup = nullptr;
    std::array<QToolButton*, 3> m_sourceBtns{nullptr, nullptr, nullptr};
    QComboBox* m_bookCombo = nullptr;
    QToolButton* m_newBookBtn = nullptr;
    QToolButton* m_delBookBtn = nullptr;
    QToolButton* m_queueToggleBtn = nullptr;
    QToolButton* m_settingsBtn = nullptr;

    // Central splitter & Left sources pane
    QSplitter* m_splitter = nullptr;
    QLabel* m_sourceHeaderLabel = nullptr;
    QToolButton* m_refreshSourceBtn = nullptr;
    QStackedWidget* m_sourceStack = nullptr;
    QListWidget* m_kmailFolderList = nullptr;
    QLabel* m_akonadiStatusLabel = nullptr;
    QListWidget* m_mfCategoryList = nullptr;
    QListWidget* m_rssFeedList = nullptr;
    QWidget* m_rssAddWidget = nullptr;
    QLineEdit* m_rssTitleEdit = nullptr;
    QLineEdit* m_rssUrlEdit = nullptr;
    QToolButton* m_rssToggleAddBtn = nullptr;
    QToolButton* m_rssRemoveBtn = nullptr;

    // Middle articles pane
    QWidget* m_mfFilterBar = nullptr;
    QButtonGroup* m_mfStatusGroup = nullptr;
    std::array<QPushButton*, 3> m_mfStatusBtns{nullptr, nullptr, nullptr};
    QCheckBox* m_mfStarredCheck = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QPushButton* m_selectAllBtn = nullptr;
    QPushButton* m_selectNoneBtn = nullptr;
    QPushButton* m_stageBtn = nullptr;
    QTableView* m_articleView = nullptr;

    std::array<ArticleTableModel*, 3> m_articleModels{nullptr, nullptr, nullptr};
    std::array<QString, 3> m_sourceFilters;
    ArticleFilterProxy* m_articleProxy = nullptr;

    // Right book queue & build pane
    QWidget* m_queuePane = nullptr;
    QLabel* m_chapterHeaderLabel = nullptr;
    QToolButton* m_chUpBtn = nullptr;
    QToolButton* m_chDownBtn = nullptr;
    QToolButton* m_chSortBtn = nullptr;
    QToolButton* m_chReverseBtn = nullptr;
    QToolButton* m_chRemoveBtn = nullptr;
    QToolButton* m_chClearBtn = nullptr;
    QListWidget* m_chapterList = nullptr;

    QLineEdit* m_authorEdit = nullptr;
    QLineEdit* m_publisherEdit = nullptr;
    QLineEdit* m_outputPathEdit = nullptr;
    QCheckBox* m_simplifyCheck = nullptr;
    QCheckBox* m_keepImagesCheck = nullptr;
    QCheckBox* m_appendCheck = nullptr;
    QPushButton* m_buildBtn = nullptr;

    // Bottom status bar
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_busyBar = nullptr;
};

} // namespace endpaper
