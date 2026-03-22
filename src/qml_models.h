#pragma once

#include "database.h"
#include "akonadi_fetcher.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"

#include <QAbstractListModel>
#include <qqml.h>
#include <QSortFilterProxyModel>

namespace endpaper {

// ──────────────────────────────────────────────────────────────────────────
// BookModel
// ──────────────────────────────────────────────────────────────────────────
class BookModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, CreatedAtRole };
    explicit BookModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setBooks(const std::vector<Book>& books) {
        beginResetModel();
        m_books = books;
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_books.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_books.size()) return QVariant();
        const auto& b = m_books[index.row()];
        if (role == IdRole) return QVariant::fromValue(static_cast<qint64>(b.id));
        if (role == TitleRole) return QString::fromStdString(b.title);
        if (role == CreatedAtRole) return QString::fromStdString(b.created_at);
        return QVariant();
    }

    Q_INVOKABLE int indexOf(int bookId) const {
        for (int i = 0; i < static_cast<int>(m_books.size()); ++i) {
            if (m_books[i].id == bookId) return i;
        }
        return -1;
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {IdRole, "id"}, {TitleRole, "title"}, {CreatedAtRole, "createdAt"} };
    }
private:
    std::vector<Book> m_books;
};

// ──────────────────────────────────────────────────────────────────────────
// FolderModel (KMail Folders)
// ──────────────────────────────────────────────────────────────────────────
class FolderModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { CollectionIdRole = Qt::UserRole + 1, NameRole, FullPathRole };
    explicit FolderModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setFolders(const std::vector<FolderInfo>& folders) {
        beginResetModel();
        m_folders = folders;
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_folders.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_folders.size()) return QVariant();
        const auto& f = m_folders[index.row()];
        if (role == CollectionIdRole) return QVariant::fromValue(static_cast<qint64>(f.collection_id));
        if (role == NameRole) return QString::fromStdString(f.name);
        if (role == FullPathRole) return QString::fromStdString(f.full_path);
        return QVariant();
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {CollectionIdRole, "collectionId"}, {NameRole, "name"}, {FullPathRole, "fullPath"} };
    }
private:
    std::vector<FolderInfo> m_folders;
};

// ──────────────────────────────────────────────────────────────────────────
// EmailModel
// ──────────────────────────────────────────────────────────────────────────
class EmailModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { AkonadiIdRole = Qt::UserRole + 1, TitleRole, AuthorRole, DateRole, SelectedRole };
    explicit EmailModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setEmails(const std::vector<EmailItem>& emails) {
        beginResetModel();
        m_emails = emails;
        m_selected.assign(emails.size(), false);
        endResetModel();
    }

    const std::vector<EmailItem>& getEmails() const { return m_emails; }

    Q_INVOKABLE void toggleSelected(int row) {
        if (row >= 0 && row < m_selected.size()) {
            m_selected[row] = !m_selected[row];
            emit dataChanged(index(row), index(row), {SelectedRole});
        }
    }
    
    Q_INVOKABLE void selectAll() {
        if (m_selected.empty()) return;
        m_selected.assign(m_selected.size(), true);
        emit dataChanged(index(0), index(m_selected.size() - 1), {SelectedRole});
    }

    Q_INVOKABLE void deselectAll() {
         if (m_selected.empty()) return;
         m_selected.assign(m_selected.size(), false);
         emit dataChanged(index(0), index(m_selected.size() - 1), {SelectedRole});
    }

    bool isSelected(int row) const {
        if (row >= 0 && row < m_selected.size()) return m_selected[row];
        return false;
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_emails.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_emails.size()) return QVariant();
        const auto& e = m_emails[index.row()];
        if (role == AkonadiIdRole) return QVariant::fromValue(static_cast<qint64>(e.akonadi_id));
        if (role == TitleRole) return QString::fromStdString(e.subject);
        if (role == AuthorRole) return QString::fromStdString(e.sender);
        if (role == DateRole) return QString::fromStdString(e.date);
        if (role == SelectedRole) return m_selected[index.row()];
        return QVariant();
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override {
        if (!index.isValid() || index.row() >= m_emails.size()) return false;
        if (role == SelectedRole) {
            m_selected[index.row()] = value.toBool();
            emit dataChanged(index, index, {SelectedRole});
            return true;
        }
        return false;
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {AkonadiIdRole, "akonadiId"}, {TitleRole, "title"}, {AuthorRole, "author"}, {DateRole, "date"}, {SelectedRole, "selected"} };
    }
private:
    std::vector<EmailItem> m_emails;
    std::vector<bool> m_selected;
};

// ──────────────────────────────────────────────────────────────────────────
// ChapterModel
// ──────────────────────────────────────────────────────────────────────────
class ChapterModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { IdRole = Qt::UserRole + 1, SubjectRole, SenderRole, SortOrderRole };
    explicit ChapterModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setChapters(const std::vector<Chapter>& chapters) {
        beginResetModel();
        m_chapters = chapters;
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_chapters.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_chapters.size()) return QVariant();
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
private:
    std::vector<Chapter> m_chapters;
};

// ──────────────────────────────────────────────────────────────────────────
// MinifluxCategoryModel
// ──────────────────────────────────────────────────────────────────────────
class MinifluxCategoryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, UnreadCountRole };
    explicit MinifluxCategoryModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setCategories(const std::vector<MinifluxCategory>& categories) {
        beginResetModel();
        m_categories = categories;
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_categories.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_categories.size()) return QVariant();
        const auto& c = m_categories[index.row()];
        if (role == IdRole) return QVariant::fromValue(static_cast<qint64>(c.id));
        if (role == TitleRole) return QString::fromStdString(c.title);
        if (role == UnreadCountRole) return c.unread_count;
        return QVariant();
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {IdRole, "id"}, {TitleRole, "title"}, {UnreadCountRole, "unreadCount"} };
    }
private:
    std::vector<MinifluxCategory> m_categories;
};

// ──────────────────────────────────────────────────────────────────────────
// MinifluxEntryModel
// ──────────────────────────────────────────────────────────────────────────
class MinifluxEntryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, AuthorRole, UrlRole, SelectedRole, StarredRole, PublishedAtRole };
    explicit MinifluxEntryModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setEntries(const std::vector<MinifluxEntry>& entries) {
        beginResetModel();
        m_entries = entries;
        m_selected.assign(entries.size(), false);
        endResetModel();
    }

    const std::vector<MinifluxEntry>& getEntries() const { return m_entries; }

    Q_INVOKABLE void toggleSelected(int row) {
        if (row >= 0 && row < m_selected.size()) {
            m_selected[row] = !m_selected[row];
            emit dataChanged(index(row), index(row), {SelectedRole});
        }
    }
    
    Q_INVOKABLE void selectAll() {
        if (m_selected.empty()) return;
        m_selected.assign(m_selected.size(), true);
        emit dataChanged(index(0), index(m_selected.size() - 1), {SelectedRole});
    }

    Q_INVOKABLE void deselectAll() {
         if (m_selected.empty()) return;
         m_selected.assign(m_selected.size(), false);
         emit dataChanged(index(0), index(m_selected.size() - 1), {SelectedRole});
    }

    bool isSelected(int row) const {
        if (row >= 0 && row < m_selected.size()) return m_selected[row];
        return false;
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_entries.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_entries.size()) return QVariant();
        const auto& e = m_entries[index.row()];
        if (role == IdRole) return QVariant::fromValue(static_cast<qint64>(e.id));
        if (role == TitleRole) return QString::fromStdString(e.title);
        if (role == AuthorRole) return QString::fromStdString(e.author);
        if (role == UrlRole) return QString::fromStdString(e.url);
        if (role == StarredRole) return e.starred;
        if (role == PublishedAtRole) return QString::fromStdString(e.published_at);
        if (role == SelectedRole) return m_selected[index.row()];
        return QVariant();
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override {
        if (!index.isValid() || index.row() >= m_entries.size()) return false;
        if (role == SelectedRole) {
            m_selected[index.row()] = value.toBool();
            emit dataChanged(index, index, {SelectedRole});
            return true;
        }
        return false;
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {IdRole, "id"}, {TitleRole, "title"}, {AuthorRole, "author"}, {UrlRole, "url"}, {StarredRole, "starred"}, {PublishedAtRole, "publishedAt"}, {SelectedRole, "selected"} };
    }
private:
    std::vector<MinifluxEntry> m_entries;
    std::vector<bool> m_selected;
};

// ──────────────────────────────────────────────────────────────────────────
// RssFeedModel
// ──────────────────────────────────────────────────────────────────────────
class RssFeedModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, UrlRole };
    explicit RssFeedModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setFeeds(const std::vector<RssFeed>& feeds) {
        beginResetModel();
        m_feeds = feeds;
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_feeds.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_feeds.size()) return QVariant();
        const auto& f = m_feeds[index.row()];
        if (role == IdRole) return QVariant::fromValue(static_cast<qint64>(f.id));
        if (role == TitleRole) return QString::fromStdString(f.title);
        if (role == UrlRole) return QString::fromStdString(f.url);
        return QVariant();
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {IdRole, "id"}, {TitleRole, "title"}, {UrlRole, "url"} };
    }
private:
    std::vector<RssFeed> m_feeds;
};

// ──────────────────────────────────────────────────────────────────────────
// RssEntryModel
// ──────────────────────────────────────────────────────────────────────────
class RssEntryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { TitleRole = Qt::UserRole + 1, AuthorRole, UrlRole, PublishedAtRole, SelectedRole };
    explicit RssEntryModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setEntries(const std::vector<RssItem>& entries) {
        beginResetModel();
        m_entries = entries;
        m_selected.assign(entries.size(), false);
        endResetModel();
    }

    const std::vector<RssItem>& getEntries() const { return m_entries; }

    Q_INVOKABLE void toggleSelected(int row) {
        if (row >= 0 && row < m_selected.size()) {
            m_selected[row] = !m_selected[row];
            emit dataChanged(index(row), index(row), {SelectedRole});
        }
    }
    
    Q_INVOKABLE void selectAll() {
        if (m_selected.empty()) return;
        m_selected.assign(m_selected.size(), true);
        emit dataChanged(index(0), index(m_selected.size() - 1), {SelectedRole});
    }

    Q_INVOKABLE void deselectAll() {
         if (m_selected.empty()) return;
         m_selected.assign(m_selected.size(), false);
         emit dataChanged(index(0), index(m_selected.size() - 1), {SelectedRole});
    }

    bool isSelected(int row) const {
        if (row >= 0 && row < m_selected.size()) return m_selected[row];
        return false;
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(m_entries.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_entries.size()) return QVariant();
        const auto& e = m_entries[index.row()];
        if (role == TitleRole) return QString::fromStdString(e.title);
        if (role == AuthorRole) return QString::fromStdString(e.author);
        if (role == UrlRole) return QString::fromStdString(e.link);
        if (role == PublishedAtRole) return QString::fromStdString(e.date);
        if (role == SelectedRole) return m_selected[index.row()];
        return QVariant();
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override {
        if (!index.isValid() || index.row() >= m_entries.size()) return false;
        if (role == SelectedRole) {
            m_selected[index.row()] = value.toBool();
            emit dataChanged(index, index, {SelectedRole});
            return true;
        }
        return false;
    }

    QHash<int, QByteArray> roleNames() const override {
        return { {TitleRole, "title"}, {AuthorRole, "author"}, {UrlRole, "url"}, {PublishedAtRole, "publishedAt"}, {SelectedRole, "selected"} };
    }
private:
    std::vector<RssItem> m_entries;
    std::vector<bool> m_selected;
};

// ──────────────────────────────────────────────────────────────────────────
// ArticleFilterModel
// ──────────────────────────────────────────────────────────────────────────
class ArticleFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString filterString READ filterString WRITE setFilterString NOTIFY filterStringChanged)
public:
    explicit ArticleFilterModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
    
    QString filterString() const { return m_filterString; }
    void setFilterString(const QString& str) {
        if (m_filterString != str) {
            m_filterString = str;
            invalidateFilter();
            emit filterStringChanged();
        }
    }

    Q_INVOKABLE void selectAll() {
        if (!sourceModel()) return;
        int selRole = sourceModel()->roleNames().key("selected", -1);
        if (selRole == -1) return;
        for (int r = 0; r < rowCount(); ++r) {
            setData(index(r, 0), true, selRole);
        }
    }

    Q_INVOKABLE void deselectAll() {
        if (!sourceModel()) return;
        int selRole = sourceModel()->roleNames().key("selected", -1);
        if (selRole == -1) return;
        for (int r = 0; r < rowCount(); ++r) {
            setData(index(r, 0), false, selRole);
        }
    }

    Q_INVOKABLE void handleSelection(int proxyRow, int modifiers) {
        if (!sourceModel() || proxyRow < 0 || proxyRow >= rowCount()) return;
        int selRole = sourceModel()->roleNames().key("selected", -1);
        if (selRole == -1) return;

        bool isShift = (modifiers & Qt::ShiftModifier) != 0;
        bool isCtrl = (modifiers & Qt::ControlModifier) != 0;

        if (isShift && m_lastClickedRow >= 0 && m_lastClickedRow < rowCount()) {
            int start = std::min(m_lastClickedRow, proxyRow);
            int end = std::max(m_lastClickedRow, proxyRow);
            
            bool targetState = true;
            for (int r = start; r <= end; ++r) {
                setData(index(r, 0), targetState, selRole);
            }
        } else {
            bool currentState = data(index(proxyRow, 0), selRole).toBool();
            setData(index(proxyRow, 0), !currentState, selRole);
            m_lastClickedRow = proxyRow;
        }
    }

signals:
    void filterStringChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        if (m_filterString.trimmed().isEmpty()) return true;
        
        QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);
        
        int titleRole = sourceModel()->roleNames().key("title", -1);
        int authorRole = sourceModel()->roleNames().key("author", -1);
        
        QString title = titleRole != -1 ? sourceModel()->data(idx, titleRole).toString() : "";
        QString author = authorRole != -1 ? sourceModel()->data(idx, authorRole).toString() : "";
        
        QStringList terms = m_filterString.split(" ", Qt::SkipEmptyParts);
        for (const QString& term : terms) {
            if (!title.contains(term, Qt::CaseInsensitive) && 
                !author.contains(term, Qt::CaseInsensitive)) {
                return false;
            }
        }
        return true;
    }

private:
    QString m_filterString;
    int m_lastClickedRow = -1;
};

} // namespace endpaper
