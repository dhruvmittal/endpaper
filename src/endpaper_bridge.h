#pragma once

#include "config.h"
#include "database.h"
#include "akonadi_fetcher.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"
#include "qml_models.h"

#include <QObject>
#include <QString>
#include <qqml.h>
#include "config.h"
#include <QGuiApplication>
#include <memory>

namespace endpaper {

class EndpaperBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY isBusyChanged)

    Q_PROPERTY(endpaper::BookModel* bookModel READ bookModel CONSTANT)
    Q_PROPERTY(endpaper::FolderModel* folderModel READ folderModel CONSTANT)
    Q_PROPERTY(endpaper::EmailModel* emailModel READ emailModel CONSTANT)
    Q_PROPERTY(endpaper::ArticleFilterModel* emailFilterModel READ emailFilterModel CONSTANT)
    Q_PROPERTY(endpaper::ChapterModel* chapterModel READ chapterModel CONSTANT)
    Q_PROPERTY(endpaper::MinifluxCategoryModel* minifluxCategoryModel READ minifluxCategoryModel CONSTANT)
    Q_PROPERTY(endpaper::MinifluxEntryModel* minifluxEntryModel READ minifluxEntryModel CONSTANT)
    Q_PROPERTY(endpaper::ArticleFilterModel* minifluxFilterModel READ minifluxFilterModel CONSTANT)
    Q_PROPERTY(endpaper::RssFeedModel* rssFeedModel READ rssFeedModel CONSTANT)
    Q_PROPERTY(endpaper::RssEntryModel* rssEntryModel READ rssEntryModel CONSTANT)
    Q_PROPERTY(endpaper::ArticleFilterModel* rssFilterModel READ rssFilterModel CONSTANT)

    Q_PROPERTY(qint64 selectedBookId READ selectedBookId WRITE setSelectedBookId NOTIFY selectedBookIdChanged)

    Q_PROPERTY(bool autoFetchKMail READ autoFetchKMail WRITE setAutoFetchKMail NOTIFY autoFetchKMailChanged)
    Q_PROPERTY(bool autoFetchMiniflux READ autoFetchMiniflux WRITE setAutoFetchMiniflux NOTIFY autoFetchMinifluxChanged)
    Q_PROPERTY(bool cacheMetadata READ cacheMetadata WRITE setCacheMetadata NOTIFY cacheMetadataChanged)
    Q_PROPERTY(bool rememberState READ rememberState WRITE setRememberState NOTIFY rememberStateChanged)

    Q_PROPERTY(int lastPageIndex READ lastPageIndex WRITE setLastPageIndex NOTIFY lastPageIndexChanged)
    Q_PROPERTY(qint64 lastKMailCollectionId READ lastKMailCollectionId WRITE setLastKMailCollectionId NOTIFY lastKMailCollectionIdChanged)
    Q_PROPERTY(qint64 lastMinifluxCategoryId READ lastMinifluxCategoryId WRITE setLastMinifluxCategoryId NOTIFY lastMinifluxCategoryIdChanged)
    Q_PROPERTY(int lastMinifluxStatusFilter READ lastMinifluxStatusFilter WRITE setLastMinifluxStatusFilter NOTIFY lastMinifluxStatusFilterChanged)
    Q_PROPERTY(qint64 lastRssFeedId READ lastRssFeedId WRITE setLastRssFeedId NOTIFY lastRssFeedIdChanged)

public:
    explicit EndpaperBridge(QObject* parent = nullptr);
    ~EndpaperBridge() override;

    // Must be called after instantiation to inject dependencies
    void init(Config* config, Database* db, AkonadiFetcher* fetcher, RssFetcher* rssFetcher, MinifluxFetcher* minifluxFetcher);

    QString statusMessage() const;
    bool isBusy() const;

    BookModel* bookModel() const { return m_bookModel; }
    FolderModel* folderModel() const { return m_folderModel; }
    EmailModel* emailModel() const { return m_emailModel; }
    ChapterModel* chapterModel() const { return m_chapterModel; }
    MinifluxCategoryModel* minifluxCategoryModel() const { return m_minifluxCategoryModel; }
    MinifluxEntryModel* minifluxEntryModel() const { return m_minifluxEntryModel; }
    RssFeedModel* rssFeedModel() const { return m_rssFeedModel; }
    RssEntryModel* rssEntryModel() const { return m_rssEntryModel; }

    ArticleFilterModel* emailFilterModel() const { return m_emailFilterModel; }
    ArticleFilterModel* minifluxFilterModel() const { return m_minifluxFilterModel; }
    ArticleFilterModel* rssFilterModel() const { return m_rssFilterModel; }

    qint64 selectedBookId() const { return m_selectedBookId; }
    void setSelectedBookId(qint64 id);

    Q_INVOKABLE int currentModifiers() const {
        return static_cast<int>(QGuiApplication::keyboardModifiers());
    }

    // --- KMail Methods ---──────────────────────────────────────────────
    Q_INVOKABLE void fetchFolders();
    Q_INVOKABLE void fetchEmails(qint64 collectionId);
    Q_INVOKABLE void stageSelectedEmails();
    Q_INVOKABLE void selectAllEmails();
    Q_INVOKABLE void deselectAllEmails();

    // ── Miniflux actions ──────────────────────────────────────────
    Q_INVOKABLE void fetchMinifluxCategories();
    Q_INVOKABLE void fetchMinifluxEntries(qint64 categoryId, int statusFilter);
    Q_INVOKABLE void stageSelectedMiniflux();
    Q_INVOKABLE void selectAllMiniflux();
    Q_INVOKABLE void deselectAllMiniflux();

    // ── RSS actions ───────────────────────────────────────────────
    Q_INVOKABLE void addRssFeed(const QString& title, const QString& url);
    Q_INVOKABLE void removeRssFeed(qint64 feedId);
    Q_INVOKABLE void fetchRssFeeds();
    Q_INVOKABLE void fetchRssEntries(qint64 feedId);
    Q_INVOKABLE void stageSelectedRss();
    Q_INVOKABLE void selectAllRss();
    Q_INVOKABLE void deselectAllRss();

    // ── Book actions ───────────────────────────────────────────────
    Q_INVOKABLE void refreshBooks();
    Q_INVOKABLE void createBook(const QString& title);
    Q_INVOKABLE void deleteBook(qint64 bookId);
    Q_INVOKABLE void refreshChapters();
    Q_INVOKABLE void deleteChapter(qint64 chapterId);
    Q_INVOKABLE void moveChapterUp(qint64 chapterId);
    Q_INVOKABLE void moveChapterDown(qint64 chapterId);
    Q_INVOKABLE void buildEpub(const QString& author, const QString& publisher, const QString& outPath, bool append, bool simplify);

    Q_INVOKABLE void pageOpened(const QString& pageName);

    bool autoFetchKMail() const;
    void setAutoFetchKMail(bool enabled);
    bool autoFetchMiniflux() const;
    void setAutoFetchMiniflux(bool enabled);
    bool cacheMetadata() const;
    void setCacheMetadata(bool enabled);
    bool rememberState() const;
    void setRememberState(bool enabled);

    int lastPageIndex() const;
    void setLastPageIndex(int index);
    qint64 lastKMailCollectionId() const;
    void setLastKMailCollectionId(qint64 id);
    qint64 lastMinifluxCategoryId() const;
    void setLastMinifluxCategoryId(qint64 id);
    int lastMinifluxStatusFilter() const;
    void setLastMinifluxStatusFilter(int filter);
    qint64 lastRssFeedId() const;
    void setLastRssFeedId(qint64 id);

private:
    void setStatus(const QString& msg);
    void setBusy(bool busy);

    QString m_statusMessage;
    bool m_isBusy = false;
    qint64 m_selectedBookId = -1;

    BookModel* m_bookModel = nullptr;
    FolderModel* m_folderModel = nullptr;
    EmailModel* m_emailModel = nullptr;
    ChapterModel* m_chapterModel = nullptr;
    MinifluxCategoryModel* m_minifluxCategoryModel = nullptr;
    MinifluxEntryModel* m_minifluxEntryModel = nullptr;
    RssFeedModel* m_rssFeedModel = nullptr;
    RssEntryModel* m_rssEntryModel = nullptr;

    ArticleFilterModel* m_emailFilterModel = nullptr;
    ArticleFilterModel* m_minifluxFilterModel = nullptr;
    ArticleFilterModel* m_rssFilterModel = nullptr;

    Config* m_config = nullptr;
    Database* m_db = nullptr;
    AkonadiFetcher* m_fetcher = nullptr;
    RssFetcher* m_rssFetcher = nullptr;
    MinifluxFetcher* m_minifluxFetcher = nullptr;

signals:
    void statusMessageChanged();
    void isBusyChanged();
    void selectedBookIdChanged();
    void autoFetchKMailChanged();
    void autoFetchMinifluxChanged();
    void cacheMetadataChanged();
    void rememberStateChanged();
    void lastPageIndexChanged();
    void lastKMailCollectionIdChanged();
    void lastMinifluxCategoryIdChanged();
    void lastMinifluxStatusFilterChanged();
    void lastRssFeedIdChanged();
};

} // namespace endpaper
