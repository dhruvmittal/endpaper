#pragma once

#include "akonadi_fetcher.h"
#include "build_pipeline.h"
#include "database.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <qqml.h>

#include <algorithm>
#include <string>
#include <vector>

namespace endpaper {

struct ArticleEntry {
    int64_t id = 0;
    std::string title;
    std::string author;
    std::string date;
    std::string url;
    std::string content_html;
    bool starred = false;
};

// ──────────────────────────────────────────────────────────────────────────
// BookModel
// ──────────────────────────────────────────────────────────────────────────
class BookModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        CreatedAtRole,
        ChapterCountRole,
        DisplayTitleRole
    };
    explicit BookModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setBooks(const std::vector<Book>& books) {
        beginResetModel();
        m_books = books;
        endResetModel();
        emit countChanged();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_books.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_books.size())) return QVariant();
        const auto& b = m_books[index.row()];
        if (role == IdRole) return QVariant::fromValue(static_cast<qint64>(b.id));
        if (role == TitleRole) return QString::fromStdString(b.title);
        if (role == CreatedAtRole) return QString::fromStdString(b.created_at);
        if (role == ChapterCountRole) return b.chapter_count;
        if (role == DisplayTitleRole) {
            return QString("%1 [%2 ch]").arg(QString::fromStdString(b.title)).arg(b.chapter_count);
        }
        return QVariant();
    }

    Q_INVOKABLE int indexOf(qint64 bookId) const {
        for (int i = 0; i < static_cast<int>(m_books.size()); ++i) {
            if (m_books[i].id == bookId) return i;
        }
        return -1;
    }

    QString titleForId(qint64 bookId) const {
        for (const auto& b : m_books) {
            if (b.id == bookId) return QString::fromStdString(b.title);
        }
        return "";
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {IdRole, "id"},
            {TitleRole, "title"},
            {CreatedAtRole, "createdAt"},
            {ChapterCountRole, "chapterCount"},
            {DisplayTitleRole, "displayTitle"}
        };
    }
signals:
    void countChanged();
private:
    std::vector<Book> m_books;
};

// ──────────────────────────────────────────────────────────────────────────
// FolderModel (KMail Folders)
// ──────────────────────────────────────────────────────────────────────────
class FolderModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { CollectionIdRole = Qt::UserRole + 1, NameRole, FullPathRole };
    explicit FolderModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setFolders(const std::vector<FolderInfo>& folders) {
        beginResetModel();
        m_folders = folders;
        endResetModel();
        emit countChanged();
    }

    const std::vector<FolderInfo>& getFolders() const { return m_folders; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_folders.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_folders.size())) return QVariant();
        const auto& f = m_folders[index.row()];
        if (role == CollectionIdRole) return QVariant::fromValue(static_cast<qint64>(f.collection_id));
        if (role == NameRole) return QString::fromStdString(f.name);
        if (role == FullPathRole) return QString::fromStdString(f.full_path);
        return QVariant();
    }

    Q_INVOKABLE int indexOf(qint64 collectionId) const {
        for (int i = 0; i < static_cast<int>(m_folders.size()); ++i) {
            if (m_folders[i].collection_id == collectionId) return i;
        }
        return -1;
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {CollectionIdRole, "collectionId"}, {NameRole, "name"}, {FullPathRole, "fullPath"} };
    }
signals:
    void countChanged();
private:
    std::vector<FolderInfo> m_folders;
};

// ──────────────────────────────────────────────────────────────────────────
// ChapterModel
// ──────────────────────────────────────────────────────────────────────────
class ChapterModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { IdRole = Qt::UserRole + 1, SubjectRole, SenderRole, SortOrderRole };
    explicit ChapterModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setChapters(const std::vector<Chapter>& chapters) {
        beginResetModel();
        m_chapters = chapters;
        endResetModel();
        emit countChanged();
    }

    int indexOfId(qint64 chapterId) const {
        for (int i = 0; i < static_cast<int>(m_chapters.size()); ++i) {
            if (m_chapters[i].id == chapterId) return i;
        }
        return -1;
    }

    bool moveChapterRow(int from, int to) {
        if (from < 0 || to < 0 || from == to ||
            from >= static_cast<int>(m_chapters.size()) ||
            to >= static_cast<int>(m_chapters.size())) {
            return false;
        }
        int dest = (to > from) ? (to + 1) : to;
        if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), dest)) return false;
        Chapter item = std::move(m_chapters[from]);
        m_chapters.erase(m_chapters.begin() + from);
        m_chapters.insert(m_chapters.begin() + to, std::move(item));
        endMoveRows();

        int lo = std::min(from, to);
        int hi = std::max(from, to);
        for (int i = lo; i <= hi; ++i) {
            m_chapters[i].sort_order = i + 1;
        }
        emit dataChanged(index(lo), index(hi), {SortOrderRole});
        return true;
    }

    bool removeChapterRow(int row) {
        if (row < 0 || row >= static_cast<int>(m_chapters.size())) return false;
        beginRemoveRows(QModelIndex(), row, row);
        m_chapters.erase(m_chapters.begin() + row);
        endRemoveRows();
        if (row < static_cast<int>(m_chapters.size())) {
            for (int i = row; i < static_cast<int>(m_chapters.size()); ++i) {
                m_chapters[i].sort_order = i + 1;
            }
            emit dataChanged(index(row), index(static_cast<int>(m_chapters.size()) - 1), {SortOrderRole});
        }
        emit countChanged();
        return true;
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_chapters.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_chapters.size())) return QVariant();
        const auto& c = m_chapters[index.row()];
        if (role == IdRole) return QVariant::fromValue(static_cast<qint64>(c.id));
        if (role == SubjectRole) return QString::fromStdString(c.subject);
        if (role == SenderRole) return QString::fromStdString(c.sender);
        if (role == SortOrderRole) return c.sort_order;
        return QVariant();
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {IdRole, "id"}, {SubjectRole, "subject"}, {SenderRole, "sender"}, {SortOrderRole, "sortOrder"} };
    }
signals:
    void countChanged();
private:
    std::vector<Chapter> m_chapters;
};

// ──────────────────────────────────────────────────────────────────────────
// MinifluxCategoryModel
// ──────────────────────────────────────────────────────────────────────────
class MinifluxCategoryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, UnreadCountRole, DisplayTitleRole };
    explicit MinifluxCategoryModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setCategories(const std::vector<MinifluxCategory>& categories) {
        beginResetModel();
        m_categories = categories;
        endResetModel();
        emit countChanged();
    }

    const std::vector<MinifluxCategory>& getCategories() const { return m_categories; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_categories.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_categories.size())) return QVariant();
        const auto& c = m_categories[index.row()];
        if (role == IdRole) return QVariant::fromValue(static_cast<qint64>(c.id));
        if (role == TitleRole) return QString::fromStdString(c.title);
        if (role == UnreadCountRole) return c.unread_count;
        if (role == DisplayTitleRole) return QString("%1 (%2)").arg(QString::fromStdString(c.title)).arg(c.unread_count);
        return QVariant();
    }

    Q_INVOKABLE int indexOf(qint64 categoryId) const {
        for (int i = 0; i < static_cast<int>(m_categories.size()); ++i) {
            if (m_categories[i].id == categoryId) return i;
        }
        return -1;
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {IdRole, "id"},
            {TitleRole, "title"},
            {UnreadCountRole, "unreadCount"},
            {DisplayTitleRole, "displayTitle"}
        };
    }
signals:
    void countChanged();
private:
    std::vector<MinifluxCategory> m_categories;
};

// ──────────────────────────────────────────────────────────────────────────
// RssFeedModel
// ──────────────────────────────────────────────────────────────────────────
class RssFeedModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, UrlRole };
    explicit RssFeedModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setFeeds(const std::vector<RssFeed>& feeds) {
        beginResetModel();
        m_feeds = feeds;
        endResetModel();
        emit countChanged();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_feeds.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_feeds.size())) return QVariant();
        const auto& f = m_feeds[index.row()];
        if (role == IdRole) return QVariant::fromValue(static_cast<qint64>(f.id));
        if (role == TitleRole) return QString::fromStdString(f.title);
        if (role == UrlRole) return QString::fromStdString(f.url);
        return QVariant();
    }

    Q_INVOKABLE int indexOf(qint64 feedId) const {
        for (int i = 0; i < static_cast<int>(m_feeds.size()); ++i) {
            if (m_feeds[i].id == feedId) return i;
        }
        return -1;
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {IdRole, "id"}, {TitleRole, "title"}, {UrlRole, "url"} };
    }
signals:
    void countChanged();
private:
    std::vector<RssFeed> m_feeds;
};

// ──────────────────────────────────────────────────────────────────────────
// Unified ArticleListModel (shared by KMail, Miniflux, and RSS)
// ──────────────────────────────────────────────────────────────────────────
class ArticleListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        AuthorRole,
        DateRole,
        UrlRole,
        StarredRole,
        SelectedRole
    };
    explicit ArticleListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setArticles(std::vector<ArticleEntry> articles) {
        beginResetModel();
        m_articles = std::move(articles);
        m_selected.assign(m_articles.size(), false);
        endResetModel();
    }

    const std::vector<ArticleEntry>& getArticles() const { return m_articles; }

    void clearSelectionForRows(const std::vector<int>& rows) {
        if (rows.empty() || m_selected.empty()) return;
        int minRow = static_cast<int>(m_selected.size());
        int maxRow = -1;
        for (int r : rows) {
            if (r >= 0 && r < static_cast<int>(m_selected.size()) && m_selected[r]) {
                m_selected[r] = false;
                minRow = std::min(minRow, r);
                maxRow = std::max(maxRow, r);
            }
        }
        if (maxRow >= minRow) {
            emit dataChanged(index(minRow), index(maxRow), {SelectedRole});
        }
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_articles.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_articles.size())) return QVariant();
        const auto& a = m_articles[index.row()];
        if (role == IdRole) return QVariant::fromValue(static_cast<qint64>(a.id));
        if (role == TitleRole) return QString::fromStdString(a.title);
        if (role == AuthorRole) return QString::fromStdString(a.author);
        if (role == DateRole) return QString::fromStdString(a.date);
        if (role == UrlRole) return QString::fromStdString(a.url);
        if (role == StarredRole) return a.starred;
        if (role == SelectedRole) return m_selected[index.row()];
        return QVariant();
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_articles.size())) return false;
        if (role == SelectedRole) {
            bool b = value.toBool();
            if (m_selected[index.row()] != b) {
                m_selected[index.row()] = b;
                emit dataChanged(index, index, {SelectedRole});
            }
            return true;
        }
        return false;
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {IdRole, "id"},
            {TitleRole, "title"},
            {AuthorRole, "author"},
            {DateRole, "date"},
            {UrlRole, "url"},
            {StarredRole, "starred"},
            {SelectedRole, "selected"}
        };
    }

private:
    std::vector<ArticleEntry> m_articles;
    std::vector<bool> m_selected;
};

// ──────────────────────────────────────────────────────────────────────────
// ArticleFilterModel
// ──────────────────────────────────────────────────────────────────────────
class ArticleFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString filterString READ filterString WRITE setFilterString NOTIFY filterStringChanged)
    Q_PROPERTY(bool fuzzySearch READ fuzzySearch WRITE setFuzzySearch NOTIFY fuzzySearchChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectedCountChanged)
public:
    explicit ArticleFilterModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {
        connect(this, &QAbstractItemModel::modelReset, this, [this]() {
            m_lastClickedRow = -1;
            updateSelectedCount();
        });
        connect(this, &QAbstractItemModel::rowsInserted, this, [this]() {
            if (!m_bulkUpdating) updateSelectedCount();
        });
        connect(this, &QAbstractItemModel::rowsRemoved, this, [this]() {
            if (!m_bulkUpdating) updateSelectedCount();
        });
        connect(this, &QAbstractItemModel::dataChanged, this, [this]() {
            if (!m_bulkUpdating) updateSelectedCount();
        });
        connect(this, &QAbstractItemModel::layoutChanged, this, [this]() {
            if (!m_bulkUpdating) updateSelectedCount();
        });
    }

    void setSourceModel(QAbstractItemModel* source) override {
        if (sourceModel()) {
            disconnect(sourceModel(), nullptr, this, nullptr);
        }
        QSortFilterProxyModel::setSourceModel(source);
        m_lastClickedRow = -1;
        updateSelectedCount();
    }

    template <typename Fn>
    void refreshFilter(Fn&& update) {
        m_lastClickedRow = -1;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        update();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        update();
        invalidateFilter();
#endif
        updateSelectedCount();
    }

    QString filterString() const { return m_filterString; }
    void setFilterString(const QString& str) {
        if (m_filterString != str) {
            refreshFilter([&]() { m_filterString = str; });
            emit filterStringChanged();
        }
    }

    bool fuzzySearch() const { return m_fuzzySearch; }
    void setFuzzySearch(bool fuzzy) {
        if (m_fuzzySearch != fuzzy) {
            refreshFilter([&]() { m_fuzzySearch = fuzzy; });
            emit fuzzySearchChanged();
        }
    }

    int selectedCount() const { return m_selectedCount; }

    std::vector<int> selectedSourceRows() const {
        std::vector<int> rows;
        if (!sourceModel()) return rows;
        for (int r = 0; r < rowCount(); ++r) {
            QModelIndex proxyIdx = index(r, 0);
            if (data(proxyIdx, ArticleListModel::SelectedRole).toBool()) {
                rows.push_back(mapToSource(proxyIdx).row());
            }
        }
        return rows;
    }

    Q_INVOKABLE void selectAll() {
        setAllVisibleSelected(true);
    }

    Q_INVOKABLE void deselectAll() {
        setAllVisibleSelected(false);
    }

    Q_INVOKABLE void handleSelection(int proxyRow, int modifiers) {
        if (!sourceModel() || proxyRow < 0 || proxyRow >= rowCount()) return;

        bool isShift = (modifiers & Qt::ShiftModifier) != 0;
        if (isShift && m_lastClickedRow >= 0 && m_lastClickedRow < rowCount()) {
            int start = std::min(m_lastClickedRow, proxyRow);
            int end = std::max(m_lastClickedRow, proxyRow);
            m_bulkUpdating = true;
            for (int r = start; r <= end; ++r) {
                setData(index(r, 0), true, ArticleListModel::SelectedRole);
            }
            m_bulkUpdating = false;
            updateSelectedCount();
        } else {
            bool currentState = data(index(proxyRow, 0), ArticleListModel::SelectedRole).toBool();
            setData(index(proxyRow, 0), !currentState, ArticleListModel::SelectedRole);
            m_lastClickedRow = proxyRow;
        }
    }

signals:
    void filterStringChanged();
    void fuzzySearchChanged();
    void selectedCountChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        if (m_filterString.trimmed().isEmpty()) return true;

        QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);
        QString title = sourceModel()->data(idx, ArticleListModel::TitleRole).toString();
        QString author = sourceModel()->data(idx, ArticleListModel::AuthorRole).toString();

        return matches_article_filter(
            title.toStdString(), author.toStdString(), m_filterString.toStdString(), m_fuzzySearch);
    }

private:
    void setAllVisibleSelected(bool sel) {
        if (!sourceModel()) return;
        m_bulkUpdating = true;
        for (int r = 0; r < rowCount(); ++r) {
            setData(index(r, 0), sel, ArticleListModel::SelectedRole);
        }
        m_bulkUpdating = false;
        updateSelectedCount();
    }

    void updateSelectedCount() {
        int count = 0;
        if (sourceModel()) {
            for (int r = 0; r < rowCount(); ++r) {
                if (data(index(r, 0), ArticleListModel::SelectedRole).toBool()) {
                    count++;
                }
            }
        }
        if (m_selectedCount != count) {
            m_selectedCount = count;
            emit selectedCountChanged();
        }
    }

    QString m_filterString;
    bool m_fuzzySearch = true;
    int m_selectedCount = 0;
    int m_lastClickedRow = -1;
    bool m_bulkUpdating = false;
};

} // namespace endpaper
