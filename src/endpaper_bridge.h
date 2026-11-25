#pragma once

#include "akonadi_fetcher.h"
#include "build_pipeline.h"
#include "config.h"
#include "database.h"
#include "miniflux_fetcher.h"
#include "qml_models.h"
#include "rss_fetcher.h"

#include <QGuiApplication>
#include <QObject>
#include <QString>
#include <qqml.h>

namespace endpaper {

class HtmlExtractor;

class EndpaperBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusIsErrorChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY isBusyChanged)

    Q_PROPERTY(endpaper::BookModel* bookModel READ bookModel CONSTANT)
    Q_PROPERTY(endpaper::FolderModel* folderModel READ folderModel CONSTANT)
    Q_PROPERTY(endpaper::ArticleFilterModel* emailFilterModel READ emailFilterModel CONSTANT)
    Q_PROPERTY(endpaper::ChapterModel* chapterModel READ chapterModel CONSTANT)
    Q_PROPERTY(endpaper::MinifluxCategoryModel* minifluxCategoryModel READ minifluxCategoryModel CONSTANT)
    Q_PROPERTY(endpaper::ArticleFilterModel* minifluxFilterModel READ minifluxFilterModel CONSTANT)
    Q_PROPERTY(endpaper::RssFeedModel* rssFeedModel READ rssFeedModel CONSTANT)
    Q_PROPERTY(endpaper::ArticleFilterModel* rssFilterModel READ rssFilterModel CONSTANT)

    Q_PROPERTY(qint64 selectedBookId READ selectedBookId WRITE setSelectedBookId NOTIFY selectedBookIdChanged)
    Q_PROPERTY(QString selectedBookTitle READ selectedBookTitle NOTIFY selectedBookTitleChanged)
    Q_PROPERTY(QString resolvedOutputPath READ resolvedOutputPath NOTIFY resolvedOutputPathChanged)
    Q_PROPERTY(bool akonadiAvailable READ akonadiAvailable NOTIFY akonadiAvailableChanged)
    Q_PROPERTY(QString configFilePath READ configFilePath CONSTANT)

    Q_PROPERTY(bool autoFetchKMail READ autoFetchKMail WRITE setAutoFetchKMail NOTIFY autoFetchKMailChanged)
    Q_PROPERTY(bool autoFetchMiniflux READ autoFetchMiniflux WRITE setAutoFetchMiniflux NOTIFY autoFetchMinifluxChanged)
    Q_PROPERTY(bool cacheMetadata READ cacheMetadata WRITE setCacheMetadata NOTIFY cacheMetadataChanged)
    Q_PROPERTY(bool rememberState READ rememberState WRITE setRememberState NOTIFY rememberStateChanged)

    Q_PROPERTY(int lastPageIndex READ lastPageIndex WRITE setLastPageIndex NOTIFY lastPageIndexChanged)
    Q_PROPERTY(qint64 lastKMailCollectionId READ lastKMailCollectionId WRITE setLastKMailCollectionId NOTIFY lastKMailCollectionIdChanged)
    Q_PROPERTY(qint64 lastMinifluxCategoryId READ lastMinifluxCategoryId WRITE setLastMinifluxCategoryId NOTIFY lastMinifluxCategoryIdChanged)
    Q_PROPERTY(int lastMinifluxStatusFilter READ lastMinifluxStatusFilter WRITE setLastMinifluxStatusFilter NOTIFY lastMinifluxStatusFilterChanged)
    Q_PROPERTY(qint64 lastRssFeedId READ lastRssFeedId WRITE setLastRssFeedId NOTIFY lastRssFeedIdChanged)

    Q_PROPERTY(bool resolveStubs READ resolveStubs WRITE setResolveStubs NOTIFY resolveStubsChanged)
    Q_PROPERTY(int stubThreshold READ stubThreshold WRITE setStubThreshold NOTIFY stubThresholdChanged)
    Q_PROPERTY(int requestDelayMs READ requestDelayMs WRITE setRequestDelayMs NOTIFY requestDelayMsChanged)

    Q_PROPERTY(QString minifluxUrl READ minifluxUrl WRITE setMinifluxUrl NOTIFY minifluxUrlChanged)
    Q_PROPERTY(QString minifluxToken READ minifluxToken WRITE setMinifluxToken NOTIFY minifluxTokenChanged)
    Q_PROPERTY(QString defaultAuthor READ defaultAuthor WRITE setDefaultAuthor NOTIFY defaultAuthorChanged)
    Q_PROPERTY(QString defaultPublisher READ defaultPublisher WRITE setDefaultPublisher NOTIFY defaultPublisherChanged)
    Q_PROPERTY(QString defaultOutputFolder READ defaultOutputFolder WRITE setDefaultOutputFolder NOTIFY defaultOutputFolderChanged)
    Q_PROPERTY(int rssMaxArticles READ rssMaxArticles WRITE setRssMaxArticles NOTIFY rssMaxArticlesChanged)
    Q_PROPERTY(bool fuzzySearch READ fuzzySearch WRITE setFuzzySearch NOTIFY fuzzySearchChanged)
    Q_PROPERTY(bool buildAppend READ buildAppend WRITE setBuildAppend NOTIFY buildAppendChanged)
    Q_PROPERTY(bool buildSimplify READ buildSimplify WRITE setBuildSimplify NOTIFY buildSimplifyChanged)
    Q_PROPERTY(bool buildKeepImages READ buildKeepImages WRITE setBuildKeepImages NOTIFY buildKeepImagesChanged)

public:
    explicit EndpaperBridge(QObject* parent = nullptr);
    ~EndpaperBridge() override;

    void init(Config* config, Database* db, AkonadiFetcher* fetcher, RssFetcher* rssFetcher, MinifluxFetcher* minifluxFetcher, HtmlExtractor* htmlExtractor);

    QString statusMessage() const { return m_statusMessage; }
    bool statusIsError() const { return m_statusIsError; }
    bool isBusy() const { return m_isBusy; }

    BookModel* bookModel() const { return m_bookModel; }
    FolderModel* folderModel() const { return m_folderModel; }
    ChapterModel* chapterModel() const { return m_chapterModel; }
    MinifluxCategoryModel* minifluxCategoryModel() const { return m_minifluxCategoryModel; }
    RssFeedModel* rssFeedModel() const { return m_rssFeedModel; }

    ArticleFilterModel* emailFilterModel() const { return m_emailFilterModel; }
    ArticleFilterModel* minifluxFilterModel() const { return m_minifluxFilterModel; }
    ArticleFilterModel* rssFilterModel() const { return m_rssFilterModel; }

    qint64 selectedBookId() const { return m_selectedBookId; }
    void setSelectedBookId(qint64 id);
    QString selectedBookTitle() const;
    QString resolvedOutputPath() const;
    bool akonadiAvailable() const { return m_akonadiAvailable; }
    QString configFilePath() const;

    Q_INVOKABLE int currentModifiers() const {
        return static_cast<int>(QGuiApplication::keyboardModifiers());
    }

    Q_INVOKABLE void clearStatus();
    Q_INVOKABLE void refreshAkonadiStatus();

    // ── KMail actions ─────────────────────────────────────────────
    Q_INVOKABLE void fetchFolders();
    Q_INVOKABLE void fetchEmails(qint64 collectionId);
    Q_INVOKABLE void stageSelectedEmails();

    // ── Miniflux actions ──────────────────────────────────────────
    Q_INVOKABLE void fetchMinifluxCategories();
    Q_INVOKABLE void fetchMinifluxEntries(qint64 categoryId, int statusFilter);
    Q_INVOKABLE void stageSelectedMiniflux();

    // ── RSS actions ───────────────────────────────────────────────
    Q_INVOKABLE void addRssFeed(const QString& title, const QString& url);
    Q_INVOKABLE void removeRssFeed(qint64 feedId);
    Q_INVOKABLE void fetchRssFeeds();
    Q_INVOKABLE void fetchRssEntries(qint64 feedId);
    Q_INVOKABLE void stageSelectedRss();

    // ── Book & Build actions ──────────────────────────────────────
    Q_INVOKABLE void refreshBooks();
    Q_INVOKABLE void createBook(const QString& title);
    Q_INVOKABLE void deleteBook(qint64 bookId);
    Q_INVOKABLE void refreshChapters();
    Q_INVOKABLE void deleteChapter(qint64 chapterId);
    Q_INVOKABLE void moveChapterUp(qint64 chapterId);
    Q_INVOKABLE void moveChapterDown(qint64 chapterId);
    Q_INVOKABLE void reverseChapterOrder();
    Q_INVOKABLE void autoSortChapters();
    Q_INVOKABLE void clearChapters();
    Q_INVOKABLE void buildEpub(const QString& author, const QString& publisher, const QString& outPath, bool append, bool simplify, bool keepImages);

    Q_INVOKABLE void pageOpened(const QString& pageName);

    bool autoFetchKMail() const;
    void setAutoFetchKMail(bool enabled);
    bool autoFetchMiniflux() const;
    void setAutoFetchMiniflux(bool enabled);
    bool cacheMetadata() const;
    void setCacheMetadata(bool enabled);
    bool rememberState() const;
    void setRememberState(bool enabled);

    int lastPageIndex() const { return m_lastPageIndex; }
    void setLastPageIndex(int index);
    qint64 lastKMailCollectionId() const { return m_lastKMailCollectionId; }
    void setLastKMailCollectionId(qint64 id);
    qint64 lastMinifluxCategoryId() const { return m_lastMinifluxCategoryId; }
    void setLastMinifluxCategoryId(qint64 id);
    int lastMinifluxStatusFilter() const { return m_lastMinifluxStatusFilter; }
    void setLastMinifluxStatusFilter(int filter);
    qint64 lastRssFeedId() const { return m_lastRssFeedId; }
    void setLastRssFeedId(qint64 id);

    bool resolveStubs() const;
    void setResolveStubs(bool resolve);
    int stubThreshold() const;
    void setStubThreshold(int threshold);
    int requestDelayMs() const;
    void setRequestDelayMs(int ms);

    QString minifluxUrl() const;
    void setMinifluxUrl(const QString& url);
    QString minifluxToken() const;
    void setMinifluxToken(const QString& token);
    QString defaultAuthor() const;
    void setDefaultAuthor(const QString& author);
    QString defaultPublisher() const;
    void setDefaultPublisher(const QString& publisher);
    QString defaultOutputFolder() const;
    void setDefaultOutputFolder(const QString& folder);
    int rssMaxArticles() const;
    void setRssMaxArticles(int count);
    bool fuzzySearch() const;
    void setFuzzySearch(bool enabled);
    bool buildAppend() const { return m_buildAppend; }
    void setBuildAppend(bool enabled);
    bool buildSimplify() const;
    void setBuildSimplify(bool enabled);
    bool buildKeepImages() const;
    void setBuildKeepImages(bool enabled);

private:
    void setStatus(const QString& msg, bool isError = false);
    void setBusy(bool busy);
    void stageFromModel(ArticleListModel* model,
                        ArticleFilterModel* filterModel,
                        const StageOptions& opts,
                        const StubResolver& resolver,
                        const QString& sourceLabel);

    QString m_statusMessage;
    bool m_statusIsError = false;
    bool m_isBusy = false;
    bool m_akonadiAvailable = false;
    bool m_kmailAutoFetched = false;
    bool m_minifluxAutoFetched = false;
    bool m_buildAppend = false;

    qint64 m_selectedBookId = -1;
    int m_lastPageIndex = 0;
    qint64 m_lastKMailCollectionId = -1;
    qint64 m_lastMinifluxCategoryId = -1;
    int m_lastMinifluxStatusFilter = 0;
    qint64 m_lastRssFeedId = -1;

    WorkerPool m_workers;

    BookModel* m_bookModel = nullptr;
    FolderModel* m_folderModel = nullptr;
    ArticleListModel* m_emailModel = nullptr;
    ChapterModel* m_chapterModel = nullptr;
    MinifluxCategoryModel* m_minifluxCategoryModel = nullptr;
    ArticleListModel* m_minifluxEntryModel = nullptr;
    RssFeedModel* m_rssFeedModel = nullptr;
    ArticleListModel* m_rssEntryModel = nullptr;

    ArticleFilterModel* m_emailFilterModel = nullptr;
    ArticleFilterModel* m_minifluxFilterModel = nullptr;
    ArticleFilterModel* m_rssFilterModel = nullptr;

    Config* m_config = nullptr;
    Database* m_db = nullptr;
    AkonadiFetcher* m_fetcher = nullptr;
    RssFetcher* m_rssFetcher = nullptr;
    MinifluxFetcher* m_minifluxFetcher = nullptr;
    HtmlExtractor* m_htmlExtractor = nullptr;

signals:
    void statusMessageChanged();
    void statusIsErrorChanged();
    void isBusyChanged();
    void selectedBookIdChanged();
    void selectedBookTitleChanged();
    void resolvedOutputPathChanged();
    void akonadiAvailableChanged();
    void autoFetchKMailChanged();
    void autoFetchMinifluxChanged();
    void cacheMetadataChanged();
    void rememberStateChanged();
    void lastPageIndexChanged();
    void lastKMailCollectionIdChanged();
    void lastMinifluxCategoryIdChanged();
    void lastMinifluxStatusFilterChanged();
    void lastRssFeedIdChanged();
    void resolveStubsChanged();
    void stubThresholdChanged();
    void requestDelayMsChanged();
    void minifluxUrlChanged();
    void minifluxTokenChanged();
    void defaultAuthorChanged();
    void defaultPublisherChanged();
    void defaultOutputFolderChanged();
    void rssMaxArticlesChanged();
    void fuzzySearchChanged();
    void buildAppendChanged();
    void buildSimplifyChanged();
    void buildKeepImagesChanged();
};

} // namespace endpaper
