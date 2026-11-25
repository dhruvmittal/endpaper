#include "main_window.h"
#include "html_extractor.h"
#include "settings_dialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <future>
#include <memory>

namespace endpaper {

namespace {

std::vector<MinifluxCategory> with_all_category(const std::vector<MinifluxCategory>& cats) {
    std::vector<MinifluxCategory> out;
    out.reserve(cats.size() + 1);
    MinifluxCategory all{-1, "All Categories", 0};
    for (const auto& c : cats) {
        all.unread_count += c.unread_count;
    }
    out.push_back(std::move(all));
    out.insert(out.end(), cats.begin(), cats.end());
    return out;
}

template <typename ModelPtr>
StubResolver make_resolver(int sourceIndex, ModelPtr mfFetcher, HtmlExtractor* htmlExtractor) {
    if (sourceIndex == 1) {
        return [mfFetcher](const StageItem& it) {
            return mfFetcher->fetch_original_content(it.source_id).get();
        };
    }
    if (sourceIndex == 2) {
        return [htmlExtractor](const StageItem& it) {
            return it.url.empty() ? std::string{} : htmlExtractor->fetch_and_extract(it.url).get();
        };
    }
    return {};
}

QString semanticColorStyle(const QPalette& pal, bool isError, bool isPositive) {
    bool dark = pal.color(QPalette::Window).lightness() < 128;
    if (isError) {
        return dark ? QStringLiteral("color: #ff8a80; font-weight: bold;")
                    : QStringLiteral("color: #b3261e; font-weight: bold;");
    }
    if (isPositive) {
        return dark ? QStringLiteral("color: #7bd88f;")
                    : QStringLiteral("color: #1b6e3c;");
    }
    return {};
}

QLabel* createViewportPlaceholder(QWidget* viewport) {
    auto* layout = new QVBoxLayout(viewport);
    layout->setContentsMargins(16, 16, 16, 16);
    auto* label = new QLabel(viewport);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setEnabled(false);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addStretch(1);
    layout->addWidget(label);
    layout->addStretch(1);
    return label;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// ArticleTableModel
// ═════════════════════════════════════════════════════════════════════════════

ArticleTableModel::ArticleTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int ArticleTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int ArticleTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ArticleTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const ArticleRow& r = m_rows[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case TitleColumn:
                return r.title.empty() ? QString("(Untitled)") : QString::fromStdString(r.title);
            case AuthorColumn:
                return QString::fromStdString(r.author);
            case DateColumn:
                return QString::fromStdString(r.date);
            default:
                return {};
        }
    }
    if (role == Qt::CheckStateRole && index.column() == TitleColumn) {
        return r.selected ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::FontRole && index.column() == TitleColumn && r.selected) {
        QFont f;
        f.setBold(true);
        return f;
    }
    if (role == Qt::ToolTipRole) {
        QString tip = r.title.empty() ? QString("(Untitled)") : QString::fromStdString(r.title);
        if (!r.author.empty()) tip += "\nAuthor: " + QString::fromStdString(r.author);
        if (!r.date.empty()) tip += "\nDate: " + QString::fromStdString(r.date);
        if (!r.url.empty()) tip += "\n" + QString::fromStdString(r.url);
        return tip;
    }
    return {};
}

bool ArticleTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.column() != TitleColumn || role != Qt::CheckStateRole) {
        return false;
    }
    if (index.row() < 0 || index.row() >= static_cast<int>(m_rows.size())) {
        return false;
    }
    bool next = (value.toInt() == Qt::Checked);
    ArticleRow& row = m_rows[static_cast<size_t>(index.row())];
    if (row.selected == next) return false;

    row.selected = next;
    m_selectedCount += next ? 1 : -1;
    emitRowCheckRange(index.row(), index.row());
    emit selectionChanged();
    return true;
}

QVariant ArticleTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case TitleColumn: return QString("Title");
        case AuthorColumn: return QString("Author");
        case DateColumn: return QString("Date");
        default: return {};
    }
}

Qt::ItemFlags ArticleTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == TitleColumn) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

void ArticleTableModel::setArticles(std::vector<ArticleRow> rows) {
    beginResetModel();
    m_rows = std::move(rows);
    m_selectedCount = 0;
    for (const auto& r : m_rows) {
        if (r.selected) ++m_selectedCount;
    }
    endResetModel();
    emit selectionChanged();
}

void ArticleTableModel::clear() {
    setArticles({});
}

void ArticleTableModel::emitRowCheckRange(int firstRow, int lastRow) {
    if (firstRow > lastRow || firstRow < 0) return;
    emit dataChanged(
        index(firstRow, TitleColumn),
        index(lastRow, TitleColumn),
        {Qt::CheckStateRole, Qt::FontRole});
}

void ArticleTableModel::toggleRows(const std::vector<int>& sourceRows) {
    if (sourceRows.empty()) return;
    bool anyUnchecked = false;
    for (int r : sourceRows) {
        if (r >= 0 && r < static_cast<int>(m_rows.size()) && !m_rows[static_cast<size_t>(r)].selected) {
            anyUnchecked = true;
            break;
        }
    }
    setRowsSelected(sourceRows, anyUnchecked);
}

void ArticleTableModel::setRowsSelected(const std::vector<int>& sourceRows, bool selected) {
    int minChanged = static_cast<int>(m_rows.size());
    int maxChanged = -1;
    for (int r : sourceRows) {
        if (r < 0 || r >= static_cast<int>(m_rows.size())) continue;
        ArticleRow& row = m_rows[static_cast<size_t>(r)];
        if (row.selected != selected) {
            row.selected = selected;
            m_selectedCount += selected ? 1 : -1;
            minChanged = std::min(minChanged, r);
            maxChanged = std::max(maxChanged, r);
        }
    }
    if (maxChanged >= minChanged) {
        emitRowCheckRange(minChanged, maxChanged);
        emit selectionChanged();
    }
}

const ArticleRow* ArticleTableModel::rowAt(int sourceRow) const {
    if (sourceRow < 0 || sourceRow >= static_cast<int>(m_rows.size())) return nullptr;
    return &m_rows[static_cast<size_t>(sourceRow)];
}

std::vector<StageItem> ArticleTableModel::selectedItems() const {
    std::vector<StageItem> out;
    out.reserve(static_cast<size_t>(std::max(0, m_selectedCount)));
    for (const auto& r : m_rows) {
        if (!r.selected) continue;
        out.push_back(StageItem{r.title, r.author, r.html_body, r.source_id, r.url});
    }
    return out;
}

std::optional<StageItem> ArticleTableModel::stageItemAt(int sourceRow) const {
    const ArticleRow* r = rowAt(sourceRow);
    if (!r) return std::nullopt;
    return StageItem{r->title, r->author, r->html_body, r->source_id, r->url};
}

// ═════════════════════════════════════════════════════════════════════════════
// ArticleFilterProxy
// ═════════════════════════════════════════════════════════════════════════════

ArticleFilterProxy::ArticleFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent) {}

template <typename Fn>
void ArticleFilterProxy::refreshFilter(Fn&& update) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    update();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    update();
    invalidateFilter();
#endif
}

void ArticleFilterProxy::setFilterString(const QString& query) {
    if (m_filterString == query) return;
    refreshFilter([&]() { m_filterString = query; });
}

void ArticleFilterProxy::setFuzzySearch(bool fuzzy) {
    if (m_fuzzySearch == fuzzy) return;
    refreshFilter([&]() { m_fuzzySearch = fuzzy; });
}

std::vector<int> ArticleFilterProxy::visibleSourceRows() const {
    std::vector<int> rows;
    int count = rowCount();
    rows.reserve(static_cast<size_t>(count));
    for (int r = 0; r < count; ++r) {
        rows.push_back(mapToSource(index(r, 0)).row());
    }
    return rows;
}

bool ArticleFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex&) const {
    const auto* model = qobject_cast<const ArticleTableModel*>(sourceModel());
    if (!model) return false;
    const ArticleRow* row = model->rowAt(sourceRow);
    if (!row) return false;
    return matches_article_filter(row->title, row->author, m_filterString.toStdString(), m_fuzzySearch);
}

bool ArticleFilterProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const auto* model = qobject_cast<const ArticleTableModel*>(sourceModel());
    if (!model) return QSortFilterProxyModel::lessThan(left, right);
    const ArticleRow* a = model->rowAt(left.row());
    const ArticleRow* b = model->rowAt(right.row());
    if (!a || !b) return QSortFilterProxyModel::lessThan(left, right);

    switch (left.column()) {
        case ArticleTableModel::TitleColumn:
            return QString::fromStdString(a->title).compare(QString::fromStdString(b->title), Qt::CaseInsensitive) < 0;
        case ArticleTableModel::AuthorColumn:
            return QString::fromStdString(a->author).compare(QString::fromStdString(b->author), Qt::CaseInsensitive) < 0;
        case ArticleTableModel::DateColumn:
            return a->date < b->date;
        default:
            return left.row() < right.row();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// MainWindow
// ═════════════════════════════════════════════════════════════════════════════

MainWindow::MainWindow(Config& config,
                       Database& db,
                       AkonadiFetcher& fetcher,
                       RssFetcher& rssFetcher,
                       MinifluxFetcher& minifluxFetcher,
                       HtmlExtractor& htmlExtractor,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_config(config)
    , m_db(db)
    , m_fetcher(fetcher)
    , m_rssFetcher(rssFetcher)
    , m_minifluxFetcher(minifluxFetcher)
    , m_htmlExtractor(htmlExtractor)
    , m_akonadiAvailable(fetcher.is_available())
{
    setWindowTitle("Endpaper");
    resize(1160, 740);

    if (rememberState()) {
        m_activeSource = std::clamp(m_config.get_int("state", "last_page_index", 0), 0, 2);
        m_selectedBookId = m_config.get_int64("general", "selected_book_id", -1);
        m_lastKMailCollectionId = m_config.get_int64("state", "last_kmail_collection_id", -1);
        m_lastMfCategoryId = m_config.get_int64("state", "last_miniflux_category_id", -1);
        m_mfStatusFilter = std::clamp(m_config.get_int("state", "last_miniflux_status_filter", 0), 0, 2);
        m_lastRssFeedId = m_config.get_int64("state", "last_rss_feed_id", -1);
    }

    setupUi();
    setupShortcuts();

    loadCachedMetadata();
    loadRssFeedsIntoList();
    refreshBooks();
    setActiveSource(m_activeSource);
    updateAkonadiFooter();
    setStatus("Ready");

    QListWidget* focusTarget = (m_activeSource == 0) ? m_kmailFolderList
                             : (m_activeSource == 1) ? m_mfCategoryList
                                                     : m_rssFeedList;
    focusTarget->setFocus();
}

MainWindow::~MainWindow() {
    m_workers.request_stop();
    m_fetcher.stop();
    m_workers.stop_and_join();
}

void MainWindow::setupUi() {
    // ── Models ───────────────────────────────────────────────────────────
    for (int i = 0; i < 3; ++i) {
        m_articleModels[i] = new ArticleTableModel(this);
        connect(m_articleModels[i], &ArticleTableModel::selectionChanged,
                this, &MainWindow::updateStageButtonLabel);
    }
    m_articleProxy = new ArticleFilterProxy(this);
    m_articleProxy->setFuzzySearch(fuzzySearch());
    m_articleProxy->setSourceModel(m_articleModels[0]);

    // ── Top Toolbar ──────────────────────────────────────────────────────
    m_toolBar = addToolBar("Workspace");
    m_toolBar->setObjectName("mainToolbar");
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_sourceBtnGroup = new QButtonGroup(this);
    m_sourceBtnGroup->setExclusive(true);

    struct SourceMeta {
        const char* label;
        const char* icon;
        const char* tip;
    };
    static const SourceMeta kSources[3] = {
        {"KMail", "mail-folder-inbox", "KMail folders (Ctrl+1, F1)"},
        {"Miniflux", "view-web-browser-dom-tree", "Miniflux categories (Ctrl+2, F2)"},
        {"RSS", "application-rss+xml", "RSS feeds (Ctrl+3, F3)"},
    };
    for (int i = 0; i < 3; ++i) {
        auto* btn = new QToolButton(this);
        btn->setText(kSources[i].label);
        btn->setIcon(QIcon::fromTheme(kSources[i].icon));
        btn->setToolTip(kSources[i].tip);
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setCheckable(true);
        btn->setAutoRaise(true);
        m_sourceBtnGroup->addButton(btn, i);
        m_toolBar->addWidget(btn);
        m_sourceBtns[i] = btn;
        connect(btn, &QToolButton::clicked, this, [this, i]() { setActiveSource(i); });
    }

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacer);

    m_settingsBtn = new QToolButton(this);
    m_settingsBtn->setText("&Settings…");
    m_settingsBtn->setIcon(QIcon::fromTheme("configure"));
    m_settingsBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_settingsBtn->setToolTip("Configure Endpaper (Ctrl+,)");
    connect(m_settingsBtn, &QToolButton::clicked, this, &MainWindow::openSettings);
    m_toolBar->addWidget(m_settingsBtn);

    m_toolBar->addSeparator();
    m_toolBar->addWidget(new QLabel(" Book: ", this));

    m_bookCombo = new QComboBox(this);
    m_bookCombo->setAccessibleName("Active book");
    m_bookCombo->setToolTip("Select active book for staging and EPUB export");
    m_bookCombo->setMinimumContentsLength(22);
    m_bookCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_bookCombo, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onBookActivated);
    m_toolBar->addWidget(m_bookCombo);

    m_newBookBtn = new QToolButton(this);
    m_newBookBtn->setText("Create Book");
    m_newBookBtn->setAccessibleName("Create new book");
    m_newBookBtn->setIcon(QIcon::fromTheme("document-new"));
    m_newBookBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_newBookBtn->setToolTip("Create a new book (Ctrl+N)");
    connect(m_newBookBtn, &QToolButton::clicked, this, &MainWindow::createBookInteractive);
    m_toolBar->addWidget(m_newBookBtn);

    m_delBookBtn = new QToolButton(this);
    m_delBookBtn->setText("Delete Book");
    m_delBookBtn->setAccessibleName("Delete active book");
    m_delBookBtn->setIcon(QIcon::fromTheme("edit-delete"));
    m_delBookBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_delBookBtn->setToolTip("Delete active book");
    connect(m_delBookBtn, &QToolButton::clicked, this, &MainWindow::deleteActiveBookInteractive);
    m_toolBar->addWidget(m_delBookBtn);

    m_toolBar->addSeparator();

    m_queueToggleBtn = new QToolButton(this);
    m_queueToggleBtn->setText("&Queue");
    m_queueToggleBtn->setAccessibleName("Toggle book queue sidebar");
    m_queueToggleBtn->setIcon(QIcon::fromTheme("sidebar-collapse-right"));
    m_queueToggleBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_queueToggleBtn->setCheckable(true);
    m_queueToggleBtn->setChecked(true);
    m_queueToggleBtn->setToolTip("Show or hide book queue sidebar (Ctrl+\\, F4)");
    m_toolBar->addWidget(m_queueToggleBtn);

    // ── Central 3-Pane Splitter ──────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);

    // ── Pane 1: Sources ──────────────────────────────────────────────────
    auto* sourcesPane = new QWidget(m_splitter);
    sourcesPane->setMinimumWidth(190);
    auto* sourcesLayout = new QVBoxLayout(sourcesPane);
    sourcesLayout->setContentsMargins(6, 6, 4, 6);
    sourcesLayout->setSpacing(4);

    auto* srcHdrRow = new QHBoxLayout();
    m_sourceHeaderLabel = new QLabel("KMail Folders", sourcesPane);
    QFont hdrFont = m_sourceHeaderLabel->font();
    hdrFont.setBold(true);
    m_sourceHeaderLabel->setFont(hdrFont);

    m_refreshSourceBtn = new QToolButton(sourcesPane);
    m_refreshSourceBtn->setIcon(QIcon::fromTheme("view-refresh"));
    m_refreshSourceBtn->setText("Refresh");
    m_refreshSourceBtn->setAccessibleName("Refresh current source");
    m_refreshSourceBtn->setAutoRaise(true);
    m_refreshSourceBtn->setToolTip("Refresh current source (Ctrl+R, F5)");
    connect(m_refreshSourceBtn, &QToolButton::clicked, this, &MainWindow::refreshCurrentSource);

    srcHdrRow->addWidget(m_sourceHeaderLabel, 1);
    srcHdrRow->addWidget(m_refreshSourceBtn);
    sourcesLayout->addLayout(srcHdrRow);

    m_sourceStack = new QStackedWidget(sourcesPane);

    // Source 0: KMail
    auto* kmailContainer = new QWidget(m_sourceStack);
    auto* kmailLayout = new QVBoxLayout(kmailContainer);
    kmailLayout->setContentsMargins(0, 0, 0, 0);
    kmailLayout->setSpacing(4);
    m_kmailFolderList = new QListWidget(kmailContainer);
    m_kmailFolderList->installEventFilter(this);
    connect(m_kmailFolderList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        fetchEmails(item->data(Qt::UserRole).toLongLong());
    });
    m_akonadiStatusLabel = new QLabel(kmailContainer);
    kmailLayout->addWidget(m_kmailFolderList, 1);
    kmailLayout->addWidget(m_akonadiStatusLabel);
    m_sourceStack->addWidget(kmailContainer);

    // Source 1: Miniflux
    m_mfCategoryList = new QListWidget(m_sourceStack);
    m_mfCategoryList->installEventFilter(this);
    connect(m_mfCategoryList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        m_lastMfCategoryId = item->data(Qt::UserRole).toLongLong();
        persistInt64("state", "last_miniflux_category_id", m_lastMfCategoryId);
        fetchMinifluxEntries();
    });
    m_sourceStack->addWidget(m_mfCategoryList);

    // Source 2: RSS
    auto* rssContainer = new QWidget(m_sourceStack);
    auto* rssLayout = new QVBoxLayout(rssContainer);
    rssLayout->setContentsMargins(0, 0, 0, 0);
    rssLayout->setSpacing(4);

    m_rssFeedList = new QListWidget(rssContainer);
    m_rssFeedList->installEventFilter(this);
    connect(m_rssFeedList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        fetchRssEntries(item->data(Qt::UserRole).toLongLong());
    });
    rssLayout->addWidget(m_rssFeedList, 1);

    m_rssAddWidget = new QWidget(rssContainer);
    auto* rssAddForm = new QVBoxLayout(m_rssAddWidget);
    rssAddForm->setContentsMargins(0, 2, 0, 2);
    rssAddForm->setSpacing(3);
    m_rssTitleEdit = new QLineEdit(m_rssAddWidget);
    m_rssTitleEdit->setPlaceholderText("Feed title");
    m_rssUrlEdit = new QLineEdit(m_rssAddWidget);
    m_rssUrlEdit->setPlaceholderText("https://example.com/feed.xml");
    connect(m_rssUrlEdit, &QLineEdit::returnPressed, this, &MainWindow::submitRssFeed);

    auto* rssAddBtns = new QHBoxLayout();
    auto* rssConfirmAddBtn = new QPushButton("&Add", m_rssAddWidget);
    auto* rssCancelAddBtn = new QPushButton("&Cancel", m_rssAddWidget);
    connect(rssConfirmAddBtn, &QPushButton::clicked, this, &MainWindow::submitRssFeed);
    connect(rssCancelAddBtn, &QPushButton::clicked, this, [this]() { toggleRssAddBar(false); });
    rssAddBtns->addWidget(rssConfirmAddBtn);
    rssAddBtns->addWidget(rssCancelAddBtn);

    rssAddForm->addWidget(m_rssTitleEdit);
    rssAddForm->addWidget(m_rssUrlEdit);
    rssAddForm->addLayout(rssAddBtns);
    m_rssAddWidget->setVisible(false);
    rssLayout->addWidget(m_rssAddWidget);

    auto* rssActionRow = new QHBoxLayout();
    m_rssToggleAddBtn = new QToolButton(rssContainer);
    m_rssToggleAddBtn->setText("Add Feed");
    m_rssToggleAddBtn->setAccessibleName("Add RSS feed");
    m_rssToggleAddBtn->setIcon(QIcon::fromTheme("list-add"));
    m_rssToggleAddBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_rssToggleAddBtn->setCheckable(true);
    connect(m_rssToggleAddBtn, &QToolButton::toggled, this, &MainWindow::toggleRssAddBar);

    m_rssRemoveBtn = new QToolButton(rssContainer);
    m_rssRemoveBtn->setText("Remove");
    m_rssRemoveBtn->setAccessibleName("Remove selected RSS feed");
    m_rssRemoveBtn->setIcon(QIcon::fromTheme("list-remove"));
    m_rssRemoveBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_rssRemoveBtn->setToolTip("Remove selected RSS feed (Del)");
    connect(m_rssRemoveBtn, &QToolButton::clicked, this, &MainWindow::removeSelectedRssFeed);
    connect(m_rssFeedList, &QListWidget::currentRowChanged, this, [this](int) {
        m_rssRemoveBtn->setEnabled(!m_busy && m_rssFeedList->currentItem() != nullptr);
    });

    rssActionRow->addWidget(m_rssToggleAddBtn);
    rssActionRow->addStretch();
    rssActionRow->addWidget(m_rssRemoveBtn);
    rssLayout->addLayout(rssActionRow);

    m_sourceStack->addWidget(rssContainer);
    sourcesLayout->addWidget(m_sourceStack, 1);

    // ── Pane 2: Articles ─────────────────────────────────────────────────
    auto* articlesPane = new QWidget(m_splitter);
    articlesPane->setMinimumWidth(340);
    auto* articlesLayout = new QVBoxLayout(articlesPane);
    articlesLayout->setContentsMargins(4, 6, 4, 6);
    articlesLayout->setSpacing(4);

    // Miniflux-specific segmented bar
    m_mfFilterBar = new QWidget(articlesPane);
    auto* mfBarLayout = new QHBoxLayout(m_mfFilterBar);
    mfBarLayout->setContentsMargins(0, 0, 0, 0);
    mfBarLayout->setSpacing(4);

    mfBarLayout->addWidget(new QLabel("Status:", m_mfFilterBar));
    m_mfStatusGroup = new QButtonGroup(this);
    m_mfStatusGroup->setExclusive(true);
    static const char* kMfStatusLabels[3] = {"All", "Unread", "Read"};
    for (int i = 0; i < 3; ++i) {
        auto* btn = new QPushButton(kMfStatusLabels[i], m_mfFilterBar);
        btn->setCheckable(true);
        btn->setChecked(i == m_mfStatusFilter);
        m_mfStatusGroup->addButton(btn, i);
        mfBarLayout->addWidget(btn);
        m_mfStatusBtns[i] = btn;
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            m_mfStatusFilter = i;
            persistInt("state", "last_miniflux_status_filter", i);
            onMinifluxFilterChanged();
        });
    }
    m_mfStarredCheck = new QCheckBox("Sta&rred", m_mfFilterBar);
    connect(m_mfStarredCheck, &QCheckBox::toggled, this, &MainWindow::onMinifluxFilterChanged);
    mfBarLayout->addSpacing(8);
    mfBarLayout->addWidget(m_mfStarredCheck);
    mfBarLayout->addStretch();
    m_mfFilterBar->setVisible(false);
    articlesLayout->addWidget(m_mfFilterBar);

    // Filter + bulk actions row
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(4);
    m_filterEdit = new QLineEdit(articlesPane);
    m_filterEdit->setPlaceholderText("Filter articles (Ctrl+F; press Enter to search Miniflux server)");
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_sourceFilters[m_activeSource] = text;
        m_articleProxy->setFilterString(text);
    });
    connect(m_filterEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_activeSource == 1) {
            fetchMinifluxEntries();
        }
    });

    m_selectAllBtn = new QPushButton("Select &All", articlesPane);
    m_selectAllBtn->setToolTip("Select all visible articles (Ctrl+A)");
    connect(m_selectAllBtn, &QPushButton::clicked, this, [this]() {
        m_articleModels[m_activeSource]->setRowsSelected(m_articleProxy->visibleSourceRows(), true);
    });

    m_selectNoneBtn = new QPushButton("Select &None", articlesPane);
    m_selectNoneBtn->setToolTip("Clear article selection");
    connect(m_selectNoneBtn, &QPushButton::clicked, this, [this]() {
        m_articleModels[m_activeSource]->setRowsSelected(m_articleProxy->visibleSourceRows(), false);
    });

    m_stageBtn = new QPushButton("Stage → (0)", articlesPane);
    m_stageBtn->setIcon(QIcon::fromTheme("mail-forward"));
    m_stageBtn->setToolTip("Stage checked articles into active book (Ctrl+Enter)");
    connect(m_stageBtn, &QPushButton::clicked, this, &MainWindow::stageSelectedOrHighlighted);

    filterRow->addWidget(m_filterEdit, 1);
    filterRow->addWidget(m_selectAllBtn);
    filterRow->addWidget(m_selectNoneBtn);
    filterRow->addWidget(m_stageBtn);
    articlesLayout->addLayout(filterRow);

    // Article Table
    m_articleView = new QTableView(articlesPane);
    m_articleView->setModel(m_articleProxy);
    m_articleView->verticalHeader()->setVisible(false);
    m_articleView->verticalHeader()->setDefaultSectionSize(24);
    m_articleView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_articleView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_articleView->setAlternatingRowColors(true);
    m_articleView->setShowGrid(false);
    m_articleView->setSortingEnabled(true);
    m_articleView->setWordWrap(false);
    m_articleView->horizontalHeader()->setSectionResizeMode(ArticleTableModel::TitleColumn, QHeaderView::Stretch);
    m_articleView->horizontalHeader()->setSectionResizeMode(ArticleTableModel::AuthorColumn, QHeaderView::Interactive);
    m_articleView->horizontalHeader()->setSectionResizeMode(ArticleTableModel::DateColumn, QHeaderView::ResizeToContents);
    m_articleView->setColumnWidth(ArticleTableModel::AuthorColumn, 150);
    m_articleView->installEventFilter(this);
    m_articleEmptyLabel = createViewportPlaceholder(m_articleView->viewport());
    connect(m_articleView, &QTableView::doubleClicked, this, &MainWindow::onArticleDoubleClicked);
    connect(m_articleView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::updateStageButtonLabel);
    connect(m_articleProxy, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateEmptyPlaceholders);
    connect(m_articleProxy, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateEmptyPlaceholders);
    connect(m_articleProxy, &QAbstractItemModel::modelReset, this, &MainWindow::updateEmptyPlaceholders);
    connect(m_articleProxy, &QAbstractItemModel::layoutChanged, this, &MainWindow::updateEmptyPlaceholders);
    articlesLayout->addWidget(m_articleView, 1);

    // ── Pane 3: Book Queue & Build ───────────────────────────────────────
    m_queuePane = new QWidget(m_splitter);
    m_queuePane->setMinimumWidth(260);
    auto* queueLayout = new QVBoxLayout(m_queuePane);
    queueLayout->setContentsMargins(4, 6, 6, 6);
    queueLayout->setSpacing(6);

    connect(m_queueToggleBtn, &QToolButton::toggled, this, [this](bool visible) {
        m_queuePane->setVisible(visible);
        m_queueToggleBtn->setIcon(QIcon::fromTheme(
            visible ? QStringLiteral("sidebar-collapse-right")
                    : QStringLiteral("sidebar-expand-right")));
    });

    auto* chHdrRow = new QHBoxLayout();
    chHdrRow->setSpacing(2);
    m_chapterHeaderLabel = new QLabel("Chapters (0)", m_queuePane);
    m_chapterHeaderLabel->setFont(hdrFont);

    auto makeChBtn = [this](const char* iconName, const char* accessibleName, const char* tip) {
        auto* btn = new QToolButton(m_queuePane);
        btn->setText(accessibleName);
        btn->setAccessibleName(accessibleName);
        btn->setIcon(QIcon::fromTheme(iconName));
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setToolTip(tip);
        btn->setAutoRaise(true);
        return btn;
    };
    m_chUpBtn = makeChBtn("go-up", "Move chapter up", "Move selected chapter up (Alt+Up)");
    m_chDownBtn = makeChBtn("go-down", "Move chapter down", "Move selected chapter down (Alt+Down)");
    m_chSortBtn = makeChBtn("view-sort-ascending", "Auto-sort chapters", "Auto-sort chapters by number or title");
    m_chReverseBtn = makeChBtn("object-flip-vertical", "Reverse chapter order", "Reverse chapter order");
    m_chRemoveBtn = makeChBtn("edit-delete", "Remove chapter", "Remove selected chapter (Del)");
    m_chClearBtn = makeChBtn("edit-clear-all", "Clear all chapters", "Clear all chapters");

    connect(m_chUpBtn, &QToolButton::clicked, this, [this]() { moveChapterByDelta(-1); });
    connect(m_chDownBtn, &QToolButton::clicked, this, [this]() { moveChapterByDelta(+1); });
    connect(m_chSortBtn, &QToolButton::clicked, this, &MainWindow::autoSortChapters);
    connect(m_chReverseBtn, &QToolButton::clicked, this, &MainWindow::reverseChapterOrder);
    connect(m_chRemoveBtn, &QToolButton::clicked, this, [this]() { deleteSelectedChapter(); });
    connect(m_chClearBtn, &QToolButton::clicked, this, &MainWindow::clearAllChaptersInteractive);

    chHdrRow->addWidget(m_chapterHeaderLabel, 1);
    chHdrRow->addWidget(m_chUpBtn);
    chHdrRow->addWidget(m_chDownBtn);
    chHdrRow->addWidget(m_chSortBtn);
    chHdrRow->addWidget(m_chReverseBtn);
    chHdrRow->addWidget(m_chRemoveBtn);
    chHdrRow->addWidget(m_chClearBtn);
    queueLayout->addLayout(chHdrRow);

    m_chapterList = new QListWidget(m_queuePane);
    m_chapterList->setDragDropMode(QAbstractItemView::InternalMove);
    m_chapterList->setDefaultDropAction(Qt::MoveAction);
    m_chapterList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_chapterList->setAlternatingRowColors(true);
    m_chapterEmptyLabel = createViewportPlaceholder(m_chapterList->viewport());

    m_dragSyncTimer = new QTimer(this);
    m_dragSyncTimer->setSingleShot(true);
    connect(m_dragSyncTimer, &QTimer::timeout, this, &MainWindow::syncDraggedChapterOrder);

    auto scheduleDragSync = [this]() {
        if (!m_rebuildingChapters && m_chapterList->count() == static_cast<int>(m_chapters.size())) {
            m_dragSyncTimer->start(0);
        }
    };
    connect(m_chapterList->model(), &QAbstractItemModel::rowsMoved, this, scheduleDragSync);
    connect(m_chapterList->model(), &QAbstractItemModel::rowsRemoved, this, scheduleDragSync);
    connect(m_chapterList, &QListWidget::currentRowChanged, this, [this](int) {
        updateChapterButtonsState();
    });
    queueLayout->addWidget(m_chapterList, 1);

    // Compact Build group
    auto* buildBox = new QGroupBox("Build EPUB", m_queuePane);
    auto* buildLayout = new QVBoxLayout(buildBox);
    buildLayout->setSpacing(6);

    auto* buildForm = new QFormLayout();
    buildForm->setContentsMargins(0, 0, 0, 0);
    buildForm->setSpacing(4);

    m_authorEdit = new QLineEdit(QString::fromStdString(m_config.get("epub", "author", "Unknown")), buildBox);
    m_publisherEdit = new QLineEdit(QString::fromStdString(m_config.get("epub", "publisher", "Endpaper")), buildBox);

    auto* outRow = new QHBoxLayout();
    outRow->setContentsMargins(0, 0, 0, 0);
    outRow->setSpacing(2);
    m_outputPathEdit = new QLineEdit(buildBox);
    m_outputPathEdit->setClearButtonEnabled(true);
    auto* browseOutBtn = new QToolButton(buildBox);
    browseOutBtn->setText("Browse");
    browseOutBtn->setAccessibleName("Browse output folder");
    browseOutBtn->setIcon(QIcon::fromTheme("folder-open"));
    browseOutBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    browseOutBtn->setToolTip("Choose output folder…");
    connect(browseOutBtn, &QToolButton::clicked, this, &MainWindow::browseBuildOutputFolder);
    outRow->addWidget(m_outputPathEdit, 1);
    outRow->addWidget(browseOutBtn);

    buildForm->addRow("Author:", m_authorEdit);
    buildForm->addRow("Publisher:", m_publisherEdit);
    buildForm->addRow("Output:", outRow);
    buildLayout->addLayout(buildForm);

    auto* optsRow = new QHBoxLayout();
    m_simplifyCheck = new QCheckBox("&Simplify HTML", buildBox);
    m_simplifyCheck->setToolTip("Simplify HTML for e-ink reading");
    m_simplifyCheck->setChecked(m_config.get_bool("epub", "simplify", true));
    connect(m_simplifyCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_config.set("epub", "simplify", on ? "true" : "false");
        m_config.save();
    });

    m_keepImagesCheck = new QCheckBox("Grayscale &images", buildBox);
    m_keepImagesCheck->setToolTip("Embed and optimize images as grayscale JPEGs");
    m_keepImagesCheck->setChecked(m_config.get_bool("epub", "keep_images", false));
    connect(m_keepImagesCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_config.set("epub", "keep_images", on ? "true" : "false");
        m_config.save();
    });

    m_appendCheck = new QCheckBox("&Append", buildBox);
    m_appendCheck->setChecked(false);
    m_appendCheck->setToolTip("Append chapters to existing EPUB (this session only)");

    optsRow->addWidget(m_simplifyCheck);
    optsRow->addWidget(m_keepImagesCheck);
    optsRow->addWidget(m_appendCheck);
    optsRow->addStretch();
    buildLayout->addLayout(optsRow);

    m_buildBtn = new QPushButton("Build &EPUB (Ctrl+B)", buildBox);
    m_buildBtn->setIcon(QIcon::fromTheme("run-build"));
    m_buildBtn->setToolTip("Build EPUB from staged chapters (Ctrl+B)");
    connect(m_buildBtn, &QPushButton::clicked, this, &MainWindow::buildEpubAction);
    buildLayout->addWidget(m_buildBtn);

    queueLayout->addWidget(buildBox);

    m_splitter->addWidget(sourcesPane);
    m_splitter->addWidget(articlesPane);
    m_splitter->addWidget(m_queuePane);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes({220, 580, 320});
    setCentralWidget(m_splitter);

    // ── Bottom Status Bar ────────────────────────────────────────────────
    m_statusLabel = new QLabel("Ready", this);
    statusBar()->addWidget(m_statusLabel, 1);

    m_busyBar = new QProgressBar(this);
    m_busyBar->setRange(0, 0);
    m_busyBar->setFixedWidth(110);
    m_busyBar->setTextVisible(false);
    m_busyBar->setVisible(false);
    statusBar()->addPermanentWidget(m_busyBar);

    auto* hintLabel = new QLabel(
        " Ctrl+1–3 Source | Ctrl+F Filter | Space Check | Ctrl+Enter Stage | Ctrl+B Build | Ctrl+, Settings ",
        this);
    hintLabel->setEnabled(false);
    statusBar()->addPermanentWidget(hintLabel);
}

void MainWindow::setupShortcuts() {
    auto bind = [this](const QKeySequence& seq, auto fn) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };

    bind(QKeySequence(Qt::Key_F1), [this]() { setActiveSource(0); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_1), [this]() { setActiveSource(0); });
    bind(QKeySequence(Qt::Key_F2), [this]() { setActiveSource(1); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_2), [this]() { setActiveSource(1); });
    bind(QKeySequence(Qt::Key_F3), [this]() { setActiveSource(2); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_3), [this]() { setActiveSource(2); });

    bind(QKeySequence(Qt::Key_F4), [this]() { m_queueToggleBtn->toggle(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_Backslash), [this]() { m_queueToggleBtn->toggle(); });
    bind(QKeySequence(Qt::Key_F5), [this]() { refreshCurrentSource(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_R), [this]() { refreshCurrentSource(); });

    bind(QKeySequence::Find, [this]() {
        m_filterEdit->setFocus();
        m_filterEdit->selectAll();
    });
    bind(QKeySequence(Qt::CTRL | Qt::Key_N), [this]() { createBookInteractive(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_Return), [this]() { stageSelectedOrHighlighted(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_Enter), [this]() { stageSelectedOrHighlighted(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_B), [this]() { buildEpubAction(); });
    bind(QKeySequence::Preferences, [this]() { openSettings(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this]() { openSettings(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_Q), [this]() { close(); });

    // Widget-scoped shortcuts so Ctrl+A / Del don't break QLineEdits
    auto* selectAllSc = new QShortcut(QKeySequence::SelectAll, m_articleView);
    selectAllSc->setContext(Qt::WidgetShortcut);
    connect(selectAllSc, &QShortcut::activated, this, [this]() {
        m_articleModels[m_activeSource]->setRowsSelected(m_articleProxy->visibleSourceRows(), true);
    });

    auto* chUpSc = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), m_chapterList);
    chUpSc->setContext(Qt::WidgetShortcut);
    connect(chUpSc, &QShortcut::activated, this, [this]() { moveChapterByDelta(-1); });

    auto* chDownSc = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Down), m_chapterList);
    chDownSc->setContext(Qt::WidgetShortcut);
    connect(chDownSc, &QShortcut::activated, this, [this]() { moveChapterByDelta(+1); });

    auto* chDelSc = new QShortcut(QKeySequence::Delete, m_chapterList);
    chDelSc->setContext(Qt::WidgetShortcut);
    connect(chDelSc, &QShortcut::activated, this, [this]() { deleteSelectedChapter(); });

    auto* chBkSc = new QShortcut(QKeySequence(Qt::Key_Backspace), m_chapterList);
    chBkSc->setContext(Qt::WidgetShortcut);
    connect(chBkSc, &QShortcut::activated, this, [this]() { deleteSelectedChapter(); });

    auto* rssDelSc = new QShortcut(QKeySequence::Delete, m_rssFeedList);
    rssDelSc->setContext(Qt::WidgetShortcut);
    connect(rssDelSc, &QShortcut::activated, this, &MainWindow::removeSelectedRssFeed);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (watched == m_articleView && ke->key() == Qt::Key_Space) {
            std::vector<int> sourceRows;
            const auto selected = m_articleView->selectionModel()->selectedRows(0);
            for (const QModelIndex& proxyIdx : selected) {
                sourceRows.push_back(m_articleProxy->mapToSource(proxyIdx).row());
            }
            if (sourceRows.empty() && m_articleView->currentIndex().isValid()) {
                sourceRows.push_back(m_articleProxy->mapToSource(m_articleView->currentIndex()).row());
            }
            m_articleModels[m_activeSource]->toggleRows(sourceRows);
            return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            (watched == m_kmailFolderList || watched == m_mfCategoryList || watched == m_rssFeedList)) {
            auto* list = static_cast<QListWidget*>(watched);
            if (QListWidgetItem* item = list->currentItem()) {
                if (watched == m_kmailFolderList) {
                    fetchEmails(item->data(Qt::UserRole).toLongLong());
                } else if (watched == m_mfCategoryList) {
                    m_lastMfCategoryId = item->data(Qt::UserRole).toLongLong();
                    persistInt64("state", "last_miniflux_category_id", m_lastMfCategoryId);
                    fetchMinifluxEntries();
                } else {
                    fetchRssEntries(item->data(Qt::UserRole).toLongLong());
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ── State & Config Helpers ───────────────────────────────────────────────────

bool MainWindow::rememberState() const {
    return m_config.get_bool("general", "remember_state", true);
}

bool MainWindow::cacheMetadata() const {
    return m_config.get_bool("general", "cache_metadata", true);
}

bool MainWindow::autoFetchKMail() const {
    return m_config.get_bool("general", "auto_fetch_kmail", false);
}

bool MainWindow::autoFetchMiniflux() const {
    return m_config.get_bool("general", "auto_fetch_miniflux", false);
}

bool MainWindow::fuzzySearch() const {
    return m_config.get_bool("ui", "fuzzy_search", true);
}

void MainWindow::persistInt(const std::string& section, const std::string& key, int value) {
    if (!rememberState()) return;
    m_config.set(section, key, std::to_string(value));
    m_config.save();
}

void MainWindow::persistInt64(const std::string& section, const std::string& key, int64_t value) {
    if (!rememberState()) return;
    m_config.set(section, key, std::to_string(value));
    m_config.save();
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        applyStatusStyle();
        updateAkonadiFooter();
    }
}

void MainWindow::applyStatusStyle() {
    m_statusLabel->setStyleSheet(semanticColorStyle(palette(), m_statusIsError, false));
}

void MainWindow::setStatus(const QString& msg, bool isError) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, msg, isError]() {
            if (!m_workers.is_shutting_down()) setStatus(msg, isError);
        }, Qt::QueuedConnection);
        return;
    }
    m_statusIsError = isError;
    m_statusLabel->setText(msg);
    applyStatusStyle();
}

void MainWindow::setBusy(bool busy) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, busy]() {
            if (!m_workers.is_shutting_down()) setBusy(busy);
        }, Qt::QueuedConnection);
        return;
    }
    m_busy = busy;
    m_busyBar->setVisible(busy);
    m_refreshSourceBtn->setEnabled(!busy);
    m_rssToggleAddBtn->setEnabled(!busy);
    m_rssRemoveBtn->setEnabled(!busy && m_rssFeedList->currentItem() != nullptr);
    updateBookControlsState();
    updateChapterButtonsState();
    updateStageButtonLabel();
}

void MainWindow::updateAkonadiFooter() {
    m_akonadiStatusLabel->setText(m_akonadiAvailable ? "Akonadi: Online" : "Akonadi: Offline");
    m_akonadiStatusLabel->setStyleSheet(
        semanticColorStyle(palette(), !m_akonadiAvailable, m_akonadiAvailable));
}

void MainWindow::updateEmptyPlaceholders() {
    int proxyCount = m_articleProxy->rowCount();
    int sourceCount = m_articleModels[m_activeSource]->rowCount();
    m_articleEmptyLabel->setVisible(proxyCount == 0);
    if (proxyCount == 0) {
        if (sourceCount > 0) {
            m_articleEmptyLabel->setText("No articles match the current filter");
        } else if (m_activeSource == 0) {
            m_articleEmptyLabel->setText("Select a KMail folder on the left to load emails");
        } else if (m_activeSource == 1) {
            m_articleEmptyLabel->setText("Select a Miniflux category on the left to load entries");
        } else {
            m_articleEmptyLabel->setText("Select an RSS feed on the left to load articles");
        }
    }

    bool noChapters = m_chapters.empty();
    m_chapterEmptyLabel->setVisible(noChapters);
    if (noChapters) {
        m_chapterEmptyLabel->setText(
            m_selectedBookId == -1
                ? "Create or select a book in the toolbar to stage chapters"
                : "No chapters staged\nDouble-click an article or press Ctrl+Enter");
    }
}

bool MainWindow::confirmDestructive(const QString& title, const QString& text, const QString& actionLabel) {
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, this);
    QPushButton* actionBtn = box.addButton(actionLabel, QMessageBox::DestructiveRole);
    QPushButton* cancelBtn = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancelBtn);
    box.exec();
    return box.clickedButton() == actionBtn;
}

QString MainWindow::currentBookTitle() const {
    for (const auto& b : m_books) {
        if (b.id == m_selectedBookId) {
            return QString::fromStdString(b.title);
        }
    }
    return {};
}

// ── Source Switching & Cached Lists ──────────────────────────────────────────

void MainWindow::setActiveSource(int index) {
    m_activeSource = std::clamp(index, 0, 2);
    if (m_sourceBtns[m_activeSource]) {
        m_sourceBtns[m_activeSource]->setChecked(true);
    }
    m_sourceStack->setCurrentIndex(m_activeSource);
    m_mfFilterBar->setVisible(m_activeSource == 1);

    static const char* kHeaders[3] = {"KMail Folders", "Miniflux Categories", "RSS Feeds"};
    m_sourceHeaderLabel->setText(kHeaders[m_activeSource]);

    m_articleProxy->setSourceModel(m_articleModels[m_activeSource]);
    m_articleView->horizontalHeader()->setSectionResizeMode(ArticleTableModel::TitleColumn, QHeaderView::Stretch);
    m_articleView->horizontalHeader()->setSectionResizeMode(ArticleTableModel::AuthorColumn, QHeaderView::Interactive);
    m_articleView->horizontalHeader()->setSectionResizeMode(ArticleTableModel::DateColumn, QHeaderView::ResizeToContents);

    {
        QSignalBlocker block(m_filterEdit);
        m_filterEdit->setText(m_sourceFilters[m_activeSource]);
    }
    m_articleProxy->setFilterString(m_sourceFilters[m_activeSource]);

    updateStageButtonLabel();
    updateEmptyPlaceholders();
    persistInt("state", "last_page_index", m_activeSource);
    maybeAutoFetchActiveSource();
}

void MainWindow::maybeAutoFetchActiveSource() {
    if (m_busy) return;
    if (m_activeSource == 0 && !m_kmailAutoFetched && autoFetchKMail() && m_akonadiAvailable) {
        m_kmailAutoFetched = true;
        fetchFolders();
    } else if (m_activeSource == 1 && !m_minifluxAutoFetched && autoFetchMiniflux()) {
        if (!m_config.get("miniflux", "url", "").empty()) {
            m_minifluxAutoFetched = true;
            fetchMinifluxCategories();
        }
    }
}

void MainWindow::loadCachedMetadata() {
    if (!cacheMetadata()) return;
    loadFoldersIntoList(m_db.load_folder_cache());
    auto cats = m_db.load_category_cache();
    if (!cats.empty()) {
        loadCategoriesIntoList(with_all_category(cats));
    }
}

void MainWindow::loadFoldersIntoList(const std::vector<FolderInfo>& folders) {
    m_kmailFolderList->clear();
    int matchRow = -1;
    for (int i = 0; i < static_cast<int>(folders.size()); ++i) {
        const auto& f = folders[static_cast<size_t>(i)];
        auto* item = new QListWidgetItem(QString::fromStdString(f.full_path), m_kmailFolderList);
        item->setData(Qt::UserRole, static_cast<qint64>(f.collection_id));
        if (f.collection_id == m_lastKMailCollectionId) matchRow = i;
    }
    if (matchRow >= 0) m_kmailFolderList->setCurrentRow(matchRow);
}

void MainWindow::loadCategoriesIntoList(const std::vector<MinifluxCategory>& categories) {
    m_mfCategoryList->clear();
    int matchRow = 0;
    for (int i = 0; i < static_cast<int>(categories.size()); ++i) {
        const auto& c = categories[static_cast<size_t>(i)];
        QString label = QString::fromStdString(c.title);
        if (c.unread_count > 0) label += QString(" (%1)").arg(c.unread_count);
        auto* item = new QListWidgetItem(label, m_mfCategoryList);
        item->setData(Qt::UserRole, static_cast<qint64>(c.id));
        if (c.id == m_lastMfCategoryId) matchRow = i;
    }
    if (m_mfCategoryList->count() > 0) m_mfCategoryList->setCurrentRow(matchRow);
}

void MainWindow::loadRssFeedsIntoList() {
    m_rssFeeds = m_db.list_rss_feeds();
    m_rssFeedList->clear();
    int matchRow = -1;
    for (int i = 0; i < static_cast<int>(m_rssFeeds.size()); ++i) {
        const auto& f = m_rssFeeds[static_cast<size_t>(i)];
        auto* item = new QListWidgetItem(QString::fromStdString(f.title), m_rssFeedList);
        item->setData(Qt::UserRole, static_cast<qint64>(f.id));
        item->setToolTip(QString::fromStdString(f.url));
        if (f.id == m_lastRssFeedId) matchRow = i;
    }
    if (matchRow >= 0) m_rssFeedList->setCurrentRow(matchRow);
    else if (m_rssFeedList->count() > 0) m_rssFeedList->setCurrentRow(0);
    m_rssRemoveBtn->setEnabled(!m_busy && m_rssFeedList->currentItem() != nullptr);
}

void MainWindow::refreshCurrentSource() {
    if (m_busy) return;
    if (m_activeSource == 0) {
        fetchFolders();
    } else if (m_activeSource == 1) {
        fetchMinifluxCategories();
    } else {
        if (QListWidgetItem* item = m_rssFeedList->currentItem()) {
            fetchRssEntries(item->data(Qt::UserRole).toLongLong());
        } else {
            loadRssFeedsIntoList();
        }
    }
}

// ── Fetch Workers ────────────────────────────────────────────────────────────

void MainWindow::fetchFolders() {
    if (m_busy) return;
    m_akonadiAvailable = m_fetcher.is_available();
    updateAkonadiFooter();
    if (!m_akonadiAvailable) {
        setStatus("Akonadi server not running.", true);
        return;
    }

    setBusy(true);
    setStatus("Fetching KMail folders…");
    auto fut = std::make_shared<std::future<std::vector<FolderInfo>>>(m_fetcher.fetch_folders());
    bool shouldCache = cacheMetadata();

    m_workers.launch([this, fut, shouldCache]() {
        try {
            auto folders = fut->get();
            if (m_workers.is_shutting_down()) return;
            if (shouldCache) m_db.save_folder_cache(folders);

            QMetaObject::invokeMethod(this, [this, folders = std::move(folders)]() {
                if (m_workers.is_shutting_down()) return;
                loadFoldersIntoList(folders);
                setStatus(QString("Loaded %1 KMail folders").arg(folders.size()));
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            setStatus(QString::fromStdString(e.what()), true);
            setBusy(false);
        } catch (...) {
            setStatus("Error fetching KMail folders.", true);
            setBusy(false);
        }
    });
}

void MainWindow::fetchEmails(int64_t collectionId) {
    if (m_busy || collectionId < 0) return;
    m_akonadiAvailable = m_fetcher.is_available();
    updateAkonadiFooter();
    if (!m_akonadiAvailable) {
        setStatus("Akonadi server not running.", true);
        return;
    }

    m_lastKMailCollectionId = collectionId;
    persistInt64("state", "last_kmail_collection_id", collectionId);

    setBusy(true);
    setStatus("Fetching emails…");
    auto fut = std::make_shared<std::future<std::vector<EmailItem>>>(m_fetcher.fetch_emails(collectionId));

    m_workers.launch([this, fut]() {
        try {
            auto emails = fut->get();
            if (m_workers.is_shutting_down()) return;

            std::vector<ArticleRow> rows;
            rows.reserve(emails.size());
            for (auto& em : emails) {
                rows.push_back(ArticleRow{
                    em.akonadi_id, std::move(em.subject), std::move(em.sender),
                    std::move(em.date), "", std::move(em.html_body), false
                });
            }

            QMetaObject::invokeMethod(this, [this, rows = std::move(rows)]() mutable {
                if (m_workers.is_shutting_down()) return;
                size_t n = rows.size();
                m_articleModels[0]->setArticles(std::move(rows));
                setStatus(QString("Loaded %1 emails").arg(n));
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            setStatus(QString::fromStdString(e.what()), true);
            setBusy(false);
        } catch (...) {
            setStatus("Error fetching emails.", true);
            setBusy(false);
        }
    });
}

void MainWindow::fetchMinifluxCategories() {
    if (m_busy) return;
    std::string url = m_config.get("miniflux", "url", "");
    std::string token = m_config.get("miniflux", "token", "");
    if (url.empty() || token.empty()) {
        setStatus("Configure Miniflux URL and token in Settings (Ctrl+,).", true);
        return;
    }
    m_minifluxFetcher.set_credentials(url, token);

    setBusy(true);
    setStatus("Fetching Miniflux categories…");
    bool shouldCache = cacheMetadata();

    m_workers.launch([this, shouldCache]() {
        try {
            auto cats = m_minifluxFetcher.fetch_categories().get();
            if (m_workers.is_shutting_down()) return;
            if (shouldCache) m_db.save_category_cache(cats);

            auto wrapped = with_all_category(cats);
            QMetaObject::invokeMethod(this, [this, wrapped = std::move(wrapped), rawCount = cats.size()]() {
                if (m_workers.is_shutting_down()) return;
                loadCategoriesIntoList(wrapped);
                setStatus(QString("Loaded %1 Miniflux categories").arg(rawCount));
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            setStatus(QString::fromStdString(e.what()), true);
            setBusy(false);
        } catch (...) {
            setStatus("Error fetching Miniflux categories.", true);
            setBusy(false);
        }
    });
}

void MainWindow::onMinifluxFilterChanged() {
    if (m_config.get("miniflux", "url", "").empty()) return;
    fetchMinifluxEntries();
}

void MainWindow::fetchMinifluxEntries() {
    if (m_busy) return;
    std::string url = m_config.get("miniflux", "url", "");
    std::string token = m_config.get("miniflux", "token", "");
    if (url.empty() || token.empty()) {
        setStatus("Configure Miniflux URL and token in Settings (Ctrl+,).", true);
        return;
    }
    m_minifluxFetcher.set_credentials(url, token);

    MinifluxFetcher::FetchParams params;
    params.category_id = m_lastMfCategoryId;
    if (m_mfStatusFilter == 1) params.status = "unread";
    else if (m_mfStatusFilter == 2) params.status = "read";
    params.starred = m_mfStarredCheck->isChecked();
    params.search = m_sourceFilters[1].trimmed().toStdString();

    setBusy(true);
    setStatus("Fetching Miniflux entries…");

    m_workers.launch([this, params]() {
        try {
            auto entries = m_minifluxFetcher.fetch_entries(params).get();
            if (m_workers.is_shutting_down()) return;

            std::vector<ArticleRow> rows;
            rows.reserve(entries.size());
            for (auto& en : entries) {
                rows.push_back(ArticleRow{
                    en.id, std::move(en.title), std::move(en.author),
                    std::move(en.published_at), std::move(en.url), std::move(en.content), false
                });
            }

            QMetaObject::invokeMethod(this, [this, rows = std::move(rows)]() mutable {
                if (m_workers.is_shutting_down()) return;
                size_t n = rows.size();
                m_articleModels[1]->setArticles(std::move(rows));
                setStatus(QString("Loaded %1 Miniflux entries").arg(n));
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            setStatus(QString::fromStdString(e.what()), true);
            setBusy(false);
        } catch (...) {
            setStatus("Error fetching Miniflux entries.", true);
            setBusy(false);
        }
    });
}

void MainWindow::fetchRssEntries(int64_t feedId) {
    if (m_busy || feedId <= 0) return;

    std::string url;
    std::string title;
    for (const auto& f : m_rssFeeds) {
        if (f.id == feedId) {
            url = f.url;
            title = f.title;
            break;
        }
    }
    if (url.empty()) return;

    m_lastRssFeedId = feedId;
    persistInt64("state", "last_rss_feed_id", feedId);

    size_t maxItems = static_cast<size_t>(std::max(0, m_config.get_int("rss", "max_articles", 50)));
    setBusy(true);
    setStatus(QString("Fetching RSS: %1…").arg(QString::fromStdString(title)));

    m_workers.launch([this, url, maxItems]() {
        try {
            auto entries = m_rssFetcher.fetch_items(url, maxItems).get();
            if (m_workers.is_shutting_down()) return;

            std::vector<ArticleRow> rows;
            rows.reserve(entries.size());
            for (auto& e : entries) {
                rows.push_back(ArticleRow{
                    0, std::move(e.title), std::move(e.author),
                    std::move(e.date), std::move(e.link), std::move(e.content_html), false
                });
            }

            QMetaObject::invokeMethod(this, [this, rows = std::move(rows)]() mutable {
                if (m_workers.is_shutting_down()) return;
                size_t n = rows.size();
                m_articleModels[2]->setArticles(std::move(rows));
                setStatus(QString("Loaded %1 RSS articles").arg(n));
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            setStatus(QString::fromStdString(e.what()), true);
            setBusy(false);
        } catch (...) {
            setStatus("Error fetching RSS entries.", true);
            setBusy(false);
        }
    });
}

// ── RSS Feed Management ──────────────────────────────────────────────────────

void MainWindow::toggleRssAddBar(bool visible) {
    m_rssToggleAddBtn->setChecked(visible);
    m_rssAddWidget->setVisible(visible);
    if (visible) {
        m_rssTitleEdit->clear();
        m_rssUrlEdit->clear();
        m_rssTitleEdit->setFocus();
    }
}

void MainWindow::submitRssFeed() {
    if (m_busy) return;
    QString title = m_rssTitleEdit->text().trimmed();
    QString url = m_rssUrlEdit->text().trimmed();
    if (title.isEmpty() || url.isEmpty()) {
        setStatus("Feed title and URL are required.", true);
        return;
    }
    if (!m_db.add_rss_feed(title.toStdString(), url.toStdString())) {
        setStatus("RSS feed already exists: " + url, true);
        return;
    }
    toggleRssAddBar(false);
    loadRssFeedsIntoList();
    setStatus("Added RSS feed: " + title);
}

void MainWindow::removeSelectedRssFeed() {
    if (m_busy) return;
    QListWidgetItem* item = m_rssFeedList->currentItem();
    if (!item) return;

    int64_t feedId = item->data(Qt::UserRole).toLongLong();
    if (!confirmDestructive(
            "Remove RSS Feed",
            QString("Remove \"%1\" from your saved feeds?").arg(item->text()),
            "Remove Feed")) {
        return;
    }

    m_db.delete_rss_feed(feedId);
    if (m_lastRssFeedId == feedId) {
        m_lastRssFeedId = -1;
        persistInt64("state", "last_rss_feed_id", -1);
        m_articleModels[2]->clear();
    }
    loadRssFeedsIntoList();
    setStatus("Removed RSS feed.");
}

// ── Staging ──────────────────────────────────────────────────────────────────

void MainWindow::updateStageButtonLabel() {
    int count = m_articleModels[m_activeSource]->selectedCount();
    m_stageBtn->setText(QString("Stage → (%1)").arg(count));
    bool hasSelectedOrFocused = count > 0 ||
        (m_articleView->selectionModel() && !m_articleView->selectionModel()->selectedRows(0).isEmpty());
    m_stageBtn->setEnabled(!m_busy && m_selectedBookId != -1 && hasSelectedOrFocused);
}

void MainWindow::stageSelectedOrHighlighted() {
    if (m_busy) return;
    auto items = m_articleModels[m_activeSource]->selectedItems();
    if (items.empty() && m_articleView->selectionModel()) {
        const auto rows = m_articleView->selectionModel()->selectedRows(0);
        for (const QModelIndex& proxyIdx : rows) {
            if (auto item = m_articleModels[m_activeSource]->stageItemAt(
                    m_articleProxy->mapToSource(proxyIdx).row())) {
                items.push_back(std::move(*item));
            }
        }
    }
    stageItems(m_activeSource, std::move(items));
}

void MainWindow::onArticleDoubleClicked(const QModelIndex& proxyIndex) {
    if (m_busy || !proxyIndex.isValid()) return;
    int sourceRow = m_articleProxy->mapToSource(proxyIndex).row();
    if (auto item = m_articleModels[m_activeSource]->stageItemAt(sourceRow)) {
        stageItems(m_activeSource, {*item});
    }
}

void MainWindow::stageItems(int sourceIndex, std::vector<StageItem> items) {
    if (m_busy) return;
    if (m_selectedBookId == -1) {
        setStatus("Create or select a Book before staging.", true);
        return;
    }
    if (items.empty()) {
        setStatus("No articles selected.", true);
        return;
    }

    StageOptions opts;
    opts.resolve_stubs = m_config.get_bool("fetching", "enable_stub_resolving", true);
    opts.stub_char_threshold = static_cast<size_t>(std::max(0, m_config.get_int("fetching", "stub_char_threshold", 1000)));
    opts.request_delay_ms = std::max(0, m_config.get_int("fetching", "request_delay_ms", 1000));

    StubResolver resolver = make_resolver(sourceIndex, &m_minifluxFetcher, &m_htmlExtractor);
    int64_t bookId = m_selectedBookId;
    QString bookTitle = currentBookTitle();

    setBusy(true);
    setStatus(QString("Staging %1 article(s) into \"%2\"…").arg(items.size()).arg(bookTitle));

    m_workers.launch([this, bookId, bookTitle, items = std::move(items),
                      opts, resolver = std::move(resolver)]() mutable {
        try {
            StageResult res = stage_articles(
                m_db, bookId, items, opts, resolver,
                [this](const std::string& msg) { setStatus(QString::fromStdString(msg)); },
                m_workers.stop_flag());

            QMetaObject::invokeMethod(this, [this, res, bookTitle]() {
                if (m_workers.is_shutting_down()) return;
                refreshBooks();
                QString summary = QString("Staged %1 article(s) into \"%2\"").arg(res.staged).arg(bookTitle);
                if (res.stub_failures > 0) {
                    summary += QString(" (%1 stub fallback(s))").arg(res.stub_failures);
                }
                setStatus(summary);
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            setStatus("Staging error: " + QString::fromStdString(e.what()), true);
            setBusy(false);
        } catch (...) {
            setStatus("Unexpected staging error.", true);
            setBusy(false);
        }
    });
}

// ── Books & Chapters ─────────────────────────────────────────────────────────

ArticleTableModel* MainWindow::articleModel(int sourceIndex) const {
    if (sourceIndex < 0 || sourceIndex >= 3) return nullptr;
    return m_articleModels[sourceIndex];
}

int MainWindow::chapterCount() const {
    return static_cast<int>(m_chapters.size());
}

void MainWindow::refreshBooks() {
    m_books = m_db.list_books();
    QSignalBlocker block(m_bookCombo);
    m_bookCombo->clear();

    if (m_books.empty()) {
        m_selectedBookId = -1;
        m_bookCombo->addItem("(No books)", static_cast<qint64>(-1));
        refreshChapters();
        updateOutputPlaceholder();
        updateBookControlsState();
        return;
    }

    int matchedIdx = -1;
    for (int i = 0; i < static_cast<int>(m_books.size()); ++i) {
        const auto& b = m_books[static_cast<size_t>(i)];
        m_bookCombo->addItem(
            QString("%1 (%2 ch)").arg(QString::fromStdString(b.title)).arg(b.chapter_count),
            static_cast<qint64>(b.id));
        if (b.id == m_selectedBookId) matchedIdx = i;
    }

    if (matchedIdx < 0) {
        matchedIdx = 0;
        m_selectedBookId = m_books[0].id;
        persistInt64("general", "selected_book_id", m_selectedBookId);
    }
    m_bookCombo->setCurrentIndex(matchedIdx);
    refreshChapters();
    updateOutputPlaceholder();
    updateBookControlsState();
}

void MainWindow::refreshChapters() {
    m_rebuildingChapters = true;
    int prevRow = m_chapterList->currentRow();
    m_chapters.clear();
    m_chapterList->clear();

    if (m_selectedBookId != -1) {
        m_chapters = m_db.get_chapters(m_selectedBookId);
        for (int i = 0; i < static_cast<int>(m_chapters.size()); ++i) {
            const auto& c = m_chapters[static_cast<size_t>(i)];
            QString label = QString("%1. %2").arg(i + 1).arg(QString::fromStdString(c.subject));
            if (!c.sender.empty()) label += " — " + QString::fromStdString(c.sender);
            auto* item = new QListWidgetItem(label, m_chapterList);
            item->setData(Qt::UserRole, static_cast<qint64>(c.id));
        }
        if (prevRow >= 0 && prevRow < m_chapterList->count()) {
            m_chapterList->setCurrentRow(prevRow);
        }
    }

    m_rebuildingChapters = false;
    m_chapterHeaderLabel->setText(QString("Chapters (%1)").arg(m_chapters.size()));
    updateEmptyPlaceholders();
    updateBookControlsState();
    updateChapterButtonsState();
}

void MainWindow::onBookActivated(int comboIndex) {
    if (m_busy || comboIndex < 0) return;
    int64_t nextId = m_bookCombo->itemData(comboIndex).toLongLong();
    if (nextId == m_selectedBookId) return;

    m_selectedBookId = nextId;
    m_outputPathEdit->clear();
    persistInt64("general", "selected_book_id", m_selectedBookId);
    refreshChapters();
    updateOutputPlaceholder();
}

bool MainWindow::createBookByTitle(const QString& title) {
    QString clean = title.trimmed();
    if (m_busy || clean.isEmpty()) return false;
    int64_t id = m_db.create_book(clean.toStdString());
    m_selectedBookId = id;
    m_outputPathEdit->clear();
    persistInt64("general", "selected_book_id", id);
    refreshBooks();
    setStatus("Created book: " + clean);
    return true;
}

void MainWindow::createBookInteractive() {
    if (m_busy) return;
    bool ok = false;
    QString title = QInputDialog::getText(this, "New Book", "Book title:", QLineEdit::Normal, "", &ok);
    if (ok && !title.trimmed().isEmpty()) {
        createBookByTitle(title);
    }
}

void MainWindow::deleteActiveBookInteractive() {
    if (m_busy || m_selectedBookId == -1) return;
    QString title = currentBookTitle();
    if (!confirmDestructive(
            "Delete Book",
            QString("Delete \"%1\" and all %2 chapter(s)?").arg(title).arg(m_chapters.size()),
            "Delete Book")) {
        return;
    }

    m_db.delete_book(m_selectedBookId);
    m_selectedBookId = -1;
    m_outputPathEdit->clear();
    refreshBooks();
    setStatus("Deleted book: " + title);
}

bool MainWindow::moveChapterByDelta(int delta) {
    if (m_busy || m_selectedBookId == -1) return false;
    int row = m_chapterList->currentRow();
    int target = row + delta;
    if (row < 0 || target < 0 || target >= static_cast<int>(m_chapters.size())) return false;

    int64_t cid = m_chapters[static_cast<size_t>(row)].id;
    if (!m_db.move_chapter(m_selectedBookId, cid, delta)) return false;

    refreshChapters();
    m_chapterList->setCurrentRow(target);
    return true;
}

bool MainWindow::deleteSelectedChapter() {
    if (m_busy || m_selectedBookId == -1) return false;
    int row = m_chapterList->currentRow();
    if (row < 0 || row >= static_cast<int>(m_chapters.size())) return false;

    int64_t cid = m_chapters[static_cast<size_t>(row)].id;
    m_db.delete_chapter(cid);
    m_db.renumber_chapters(m_selectedBookId);
    refreshBooks();
    if (m_chapterList->count() > 0) {
        m_chapterList->setCurrentRow(std::min(row, m_chapterList->count() - 1));
    }
    return true;
}

void MainWindow::autoSortChapters() {
    if (m_busy || m_selectedBookId == -1) return;
    m_db.auto_sort_chapters(m_selectedBookId);
    refreshChapters();
    setStatus("Chapters auto-sorted by numeric title.");
}

void MainWindow::reverseChapterOrder() {
    if (m_busy || m_selectedBookId == -1) return;
    m_db.reverse_chapter_order(m_selectedBookId);
    refreshChapters();
    setStatus("Reversed chapter order.");
}

void MainWindow::clearAllChaptersInteractive() {
    if (m_busy || m_selectedBookId == -1 || m_chapters.empty()) return;
    if (!confirmDestructive(
            "Clear All Chapters",
            QString("Remove all staged chapters from \"%1\"?").arg(currentBookTitle()),
            "Clear All Chapters")) {
        return;
    }

    m_db.delete_all_chapters(m_selectedBookId);
    refreshBooks();
    setStatus("Cleared all chapters.");
}

void MainWindow::syncDraggedChapterOrder() {
    if (m_busy || m_selectedBookId == -1) return;
    int count = m_chapterList->count();
    int selected = m_chapterList->currentRow();
    for (int i = 0; i < count; ++i) {
        QListWidgetItem* item = m_chapterList->item(i);
        if (!item) continue;
        int64_t cid = item->data(Qt::UserRole).toLongLong();
        if (cid > 0) {
            m_db.update_sort_order(cid, i + 1);
        }
    }
    refreshChapters();
    if (selected >= 0 && selected < m_chapterList->count()) {
        m_chapterList->setCurrentRow(selected);
    }
}

void MainWindow::updateBookControlsState() {
    bool hasBook = (m_selectedBookId != -1);
    m_bookCombo->setEnabled(!m_busy && !m_books.empty());
    m_newBookBtn->setEnabled(!m_busy);
    m_delBookBtn->setEnabled(!m_busy && hasBook);
    m_buildBtn->setEnabled(!m_busy && hasBook && !m_chapters.empty());
    updateStageButtonLabel();
}

void MainWindow::updateChapterButtonsState() {
    int count = m_chapterList->count();
    int row = m_chapterList->currentRow();
    bool hasSelection = (row >= 0 && row < count);
    m_chUpBtn->setEnabled(!m_busy && hasSelection && row > 0);
    m_chDownBtn->setEnabled(!m_busy && hasSelection && row + 1 < count);
    m_chSortBtn->setEnabled(!m_busy && count > 1);
    m_chReverseBtn->setEnabled(!m_busy && count > 1);
    m_chRemoveBtn->setEnabled(!m_busy && hasSelection);
    m_chClearBtn->setEnabled(!m_busy && count > 0);
}

// ── Build EPUB & Settings ────────────────────────────────────────────────────

void MainWindow::updateOutputPlaceholder() {
    QString title = currentBookTitle();
    if (title.isEmpty()) {
        m_outputPathEdit->setPlaceholderText("Select or create a book");
        return;
    }
    std::string defaultFolder = m_config.get("general", "default_folder", "");
    m_outputPathEdit->setPlaceholderText(QString::fromStdString(
        resolve_epub_output_path(title.toStdString(), "", defaultFolder)));
}

void MainWindow::browseBuildOutputFolder() {
    QString chosen = QFileDialog::getExistingDirectory(this, "Select EPUB Output Folder");
    if (!chosen.isEmpty()) {
        m_outputPathEdit->setText(chosen);
    }
}

void MainWindow::buildEpubAction() {
    if (m_busy || m_selectedBookId == -1) return;
    auto chapters = m_db.get_chapters(m_selectedBookId);
    if (chapters.empty()) {
        setStatus("No chapters staged in active book.", true);
        return;
    }

    QString qTitle = currentBookTitle();
    std::string bookTitle = qTitle.isEmpty() ? "book" : qTitle.toStdString();

    BuildOptions opts;
    QString author = m_authorEdit->text().trimmed();
    QString publisher = m_publisherEdit->text().trimmed();
    opts.author = author.isEmpty() ? "Unknown" : author.toStdString();
    opts.publisher = publisher.isEmpty() ? "Endpaper" : publisher.toStdString();
    opts.explicit_path = m_outputPathEdit->text().trimmed().toStdString();
    opts.default_folder = m_config.get("general", "default_folder", "");
    opts.append = m_appendCheck->isChecked();
    opts.simplify = m_simplifyCheck->isChecked();
    opts.keep_images = m_keepImagesCheck->isChecked();

    setBusy(true);
    setStatus(QString("Building EPUB: %1…").arg(QString::fromStdString(bookTitle)));

    m_workers.launch([this, bookTitle, chapters = std::move(chapters), opts]() {
        try {
            BuildResult res = build_book_epub(
                bookTitle, chapters, opts,
                [this](const std::string& step) { setStatus(QString::fromStdString(step)); },
                m_workers.stop_flag());

            setStatus(QString::fromStdString(format_build_summary(res, opts.keep_images)), false);
            setBusy(false);
        } catch (const std::exception& e) {
            setStatus("Build error: " + QString::fromStdString(e.what()), true);
            setBusy(false);
        } catch (...) {
            setStatus("Unexpected build error.", true);
            setBusy(false);
        }
    });
}

void MainWindow::openSettings() {
    std::string prevAuthor = m_config.get("epub", "author", "Unknown");
    std::string prevPublisher = m_config.get("epub", "publisher", "Endpaper");

    SettingsDialog dlg(m_config, m_fetcher, this);
    bool accepted = (dlg.exec() == QDialog::Accepted);

    m_akonadiAvailable = dlg.akonadiAvailable();
    updateAkonadiFooter();

    if (!accepted) return;

    m_minifluxFetcher.set_credentials(
        m_config.get("miniflux", "url", ""),
        m_config.get("miniflux", "token", ""));
    m_articleProxy->setFuzzySearch(fuzzySearch());

    if (m_authorEdit->text().trimmed().toStdString() == prevAuthor) {
        m_authorEdit->setText(QString::fromStdString(m_config.get("epub", "author", "Unknown")));
    }
    if (m_publisherEdit->text().trimmed().toStdString() == prevPublisher) {
        m_publisherEdit->setText(QString::fromStdString(m_config.get("epub", "publisher", "Endpaper")));
    }

    updateOutputPlaceholder();
    setStatus("Settings saved.");
}

} // namespace endpaper
