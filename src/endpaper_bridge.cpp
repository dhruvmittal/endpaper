#include "endpaper_bridge.h"
#include "html_extractor.h"

#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <memory>

using namespace endpaper;

namespace {

std::vector<MinifluxCategory> wrap_all_categories(const std::vector<MinifluxCategory>& cats) {
    std::vector<MinifluxCategory> out;
    out.reserve(cats.size() + 1);
    MinifluxCategory all;
    all.id = -1;
    all.title = "All Categories";
    all.unread_count = 0;
    for (const auto& cat : cats) {
        all.unread_count += cat.unread_count;
    }
    out.push_back(std::move(all));
    out.insert(out.end(), cats.begin(), cats.end());
    return out;
}

} // namespace

EndpaperBridge::EndpaperBridge(QObject* parent) : QObject(parent) {
    m_bookModel = new BookModel(this);
    m_folderModel = new FolderModel(this);
    m_emailModel = new ArticleListModel(this);
    m_chapterModel = new ChapterModel(this);
    m_minifluxCategoryModel = new MinifluxCategoryModel(this);
    m_minifluxEntryModel = new ArticleListModel(this);
    m_rssFeedModel = new RssFeedModel(this);
    m_rssEntryModel = new ArticleListModel(this);

    m_emailFilterModel = new ArticleFilterModel(this);
    m_emailFilterModel->setSourceModel(m_emailModel);

    m_minifluxFilterModel = new ArticleFilterModel(this);
    m_minifluxFilterModel->setSourceModel(m_minifluxEntryModel);

    m_rssFilterModel = new ArticleFilterModel(this);
    m_rssFilterModel->setSourceModel(m_rssEntryModel);
}

EndpaperBridge::~EndpaperBridge() {
    m_workers.request_stop();
    if (m_fetcher) m_fetcher->stop();
    m_workers.stop_and_join();
}

void EndpaperBridge::init(Config* config, Database* db, AkonadiFetcher* fetcher, RssFetcher* rssFetcher, MinifluxFetcher* minifluxFetcher, HtmlExtractor* htmlExtractor) {
    m_config = config;
    m_db = db;
    m_fetcher = fetcher;
    m_rssFetcher = rssFetcher;
    m_minifluxFetcher = minifluxFetcher;
    m_htmlExtractor = htmlExtractor;

    m_akonadiAvailable = m_fetcher ? m_fetcher->is_available() : false;

    bool fuzzy = fuzzySearch();
    m_emailFilterModel->setFuzzySearch(fuzzy);
    m_minifluxFilterModel->setFuzzySearch(fuzzy);
    m_rssFilterModel->setFuzzySearch(fuzzy);

    if (cacheMetadata()) {
        m_folderModel->setFolders(m_db->load_folder_cache());
        auto cats = m_db->load_category_cache();
        if (!cats.empty()) {
            m_minifluxCategoryModel->setCategories(wrap_all_categories(cats));
        }
    }

    if (rememberState()) {
        m_lastPageIndex = std::clamp(m_config->get_int("state", "last_page_index", 0), 0, 5);
        m_lastKMailCollectionId = m_config->get_int64("state", "last_kmail_collection_id", -1);
        m_lastMinifluxCategoryId = m_config->get_int64("state", "last_miniflux_category_id", -1);
        m_lastMinifluxStatusFilter = std::clamp(m_config->get_int("state", "last_miniflux_status_filter", 0), 0, 2);
        m_lastRssFeedId = m_config->get_int64("state", "last_rss_feed_id", -1);
        m_selectedBookId = m_config->get_int64("general", "selected_book_id", -1);
    }

    refreshBooks();
    refreshChapters();
    fetchRssFeeds();
}

void EndpaperBridge::setStatus(const QString& msg, bool isError) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, msg, isError]() {
            if (!m_workers.is_shutting_down()) setStatus(msg, isError);
        }, Qt::QueuedConnection);
        return;
    }
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged();
    }
    if (m_statusIsError != isError) {
        m_statusIsError = isError;
        emit statusIsErrorChanged();
    }
}

void EndpaperBridge::clearStatus() {
    setStatus("", false);
}

void EndpaperBridge::refreshAkonadiStatus() {
    bool avail = m_fetcher ? m_fetcher->is_available() : false;
    if (m_akonadiAvailable != avail) {
        m_akonadiAvailable = avail;
        emit akonadiAvailableChanged();
    }
}

void EndpaperBridge::setBusy(bool busy) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, busy]() {
            if (!m_workers.is_shutting_down()) setBusy(busy);
        }, Qt::QueuedConnection);
        return;
    }
    if (m_isBusy != busy) {
        m_isBusy = busy;
        emit isBusyChanged();
    }
}

QString EndpaperBridge::selectedBookTitle() const {
    if (m_selectedBookId == -1) return "";
    return m_bookModel->titleForId(m_selectedBookId);
}

QString EndpaperBridge::resolvedOutputPath() const {
    QString title = selectedBookTitle();
    return QString::fromStdString(resolve_epub_output_path(
        title.isEmpty() ? "book" : title.toStdString(),
        "",
        defaultOutputFolder().toStdString()));
}

QString EndpaperBridge::configFilePath() const {
    return m_config ? QString::fromStdString(m_config->config_file().string()) : QString();
}

void EndpaperBridge::setSelectedBookId(qint64 id) {
    if (m_selectedBookId != id) {
        m_selectedBookId = id;
        if (m_config && rememberState()) {
            m_config->set("general", "selected_book_id", std::to_string(id));
            m_config->save();
        }
        emit selectedBookIdChanged();
        emit selectedBookTitleChanged();
        emit resolvedOutputPathChanged();
        refreshChapters();
    }
}

void EndpaperBridge::refreshBooks() {
    if (!m_db) return;
    auto books = m_db->list_books();
    bool selectedExists = false;
    for (const auto& b : books) {
        if (b.id == m_selectedBookId) {
            selectedExists = true;
            break;
        }
    }
    m_bookModel->setBooks(books);

    if (books.empty()) {
        if (m_selectedBookId != -1) {
            setSelectedBookId(-1);
        } else {
            emit selectedBookTitleChanged();
            emit resolvedOutputPathChanged();
        }
    } else if (!selectedExists) {
        setSelectedBookId(books[0].id);
    } else {
        emit selectedBookTitleChanged();
        emit resolvedOutputPathChanged();
    }
}

void EndpaperBridge::createBook(const QString& title) {
    QString trimmed = title.trimmed();
    if (!m_db || isBusy() || trimmed.isEmpty()) {
        if (trimmed.isEmpty()) setStatus("Book title cannot be empty.", true);
        return;
    }
    int64_t id = m_db->create_book(trimmed.toStdString());
    refreshBooks();
    setSelectedBookId(id);
    setStatus("Book created: " + trimmed);
}

void EndpaperBridge::deleteBook(qint64 bookId) {
    if (!m_db || isBusy()) return;
    m_db->delete_book(bookId);
    if (m_selectedBookId == bookId) {
        m_selectedBookId = -1;
        emit selectedBookIdChanged();
        emit selectedBookTitleChanged();
    }
    refreshBooks();
    if (m_selectedBookId == -1) refreshChapters();
    setStatus("Book deleted.");
}

void EndpaperBridge::refreshChapters() {
    if (!m_db || m_selectedBookId == -1) {
        m_chapterModel->setChapters({});
        return;
    }
    m_chapterModel->setChapters(m_db->get_chapters(m_selectedBookId));
}

void EndpaperBridge::deleteChapter(qint64 chapterId) {
    if (!m_db || isBusy() || m_selectedBookId == -1) return;
    int row = m_chapterModel->indexOfId(chapterId);
    m_db->delete_chapter(chapterId);
    m_db->renumber_chapters(m_selectedBookId);
    if (row >= 0) {
        m_chapterModel->removeChapterRow(row);
    } else {
        refreshChapters();
    }
    refreshBooks();
}

void EndpaperBridge::moveChapterUp(qint64 chapterId) {
    if (!m_db || isBusy() || m_selectedBookId == -1) return;
    int row = m_chapterModel->indexOfId(chapterId);
    if (row > 0 && m_db->move_chapter(m_selectedBookId, chapterId, -1)) {
        m_chapterModel->moveChapterRow(row, row - 1);
    }
}

void EndpaperBridge::moveChapterDown(qint64 chapterId) {
    if (!m_db || isBusy() || m_selectedBookId == -1) return;
    int row = m_chapterModel->indexOfId(chapterId);
    if (row >= 0 && row + 1 < m_chapterModel->rowCount() &&
        m_db->move_chapter(m_selectedBookId, chapterId, +1)) {
        m_chapterModel->moveChapterRow(row, row + 1);
    }
}

void EndpaperBridge::reverseChapterOrder() {
    if (!m_db || isBusy() || m_selectedBookId == -1) return;
    m_db->reverse_chapter_order(m_selectedBookId);
    refreshChapters();
    setStatus("Chapter order reversed.");
}

void EndpaperBridge::autoSortChapters() {
    if (!m_db || isBusy() || m_selectedBookId == -1) return;
    m_db->auto_sort_chapters(m_selectedBookId);
    refreshChapters();
    setStatus("Chapters auto-sorted.");
}

void EndpaperBridge::clearChapters() {
    if (!m_db || isBusy() || m_selectedBookId == -1) return;
    m_db->delete_all_chapters(m_selectedBookId);
    refreshBooks();
    refreshChapters();
    setStatus("Cleared all chapters from book.");
}

// ── Shared staging helper ─────────────────────────────────────────────

void EndpaperBridge::stageFromModel(
    ArticleListModel* model,
    ArticleFilterModel* filterModel,
    const StageOptions& opts,
    const StubResolver& resolver,
    const QString& sourceLabel) {
    if (!m_db || m_selectedBookId == -1) {
        setStatus("Please select a book first!", true);
        return;
    }
    if (isBusy()) return;

    auto rows = filterModel->selectedSourceRows();
    if (rows.empty()) {
        setStatus("No " + sourceLabel + " selected.");
        return;
    }

    const auto& all = model->getArticles();
    std::vector<StageItem> batch;
    batch.reserve(rows.size());
    for (int r : rows) {
        if (r >= 0 && r < static_cast<int>(all.size())) {
            batch.push_back(StageItem{
                all[r].title, all[r].author, all[r].content_html, all[r].id, all[r].url
            });
        }
    }
    if (batch.empty()) return;

    qint64 bookId = m_selectedBookId;
    setBusy(true);
    setStatus("Staging " + QString::number(batch.size()) + " " + sourceLabel + "...");

    m_workers.launch([this, model, rows = std::move(rows), batch = std::move(batch),
                      bookId, opts, resolver, sourceLabel]() {
        try {
            StageResult res = stage_articles(
                *m_db, bookId, batch, opts, resolver,
                [this](const std::string& step) {
                    setStatus(QString::fromStdString(step));
                },
                m_workers.stop_flag());

            QMetaObject::invokeMethod(this, [this, model, rows, res, sourceLabel]() {
                if (m_workers.is_shutting_down()) return;
                model->clearSelectionForRows(rows);
                refreshBooks();
                refreshChapters();
                QString msg = QString("Staged %1 %2").arg(res.staged).arg(sourceLabel);
                if (res.stub_failures > 0) {
                    msg += QString(" (%1 full-text fetch failed; kept summary)").arg(res.stub_failures);
                }
                setStatus(msg, false);
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            setStatus("Staging Error: " + QString::fromStdString(e.what()), true);
            setBusy(false);
        } catch (...) {
            setStatus("Staging Error: unexpected failure", true);
            setBusy(false);
        }
    });
}

// ── KMail ─────────────────────────────────────────────────────────────

void EndpaperBridge::fetchFolders() {
    if (!m_fetcher || isBusy()) return;
    setBusy(true);
    setStatus("Fetching KMail folders...");

    // Initiate Akonadi job on main thread before waiting off-thread
    auto fut = std::make_shared<std::future<std::vector<FolderInfo>>>(m_fetcher->fetch_folders());
    bool shouldCache = cacheMetadata();

    m_workers.launch([this, fut, shouldCache]() {
        try {
            auto folders = fut->get();
            if (m_workers.is_shutting_down()) return;
            if (shouldCache) m_db->save_folder_cache(folders);

            QMetaObject::invokeMethod(this, [this, f = std::move(folders)]() {
                if (m_workers.is_shutting_down()) return;
                refreshAkonadiStatus();
                const auto& current = m_folderModel->getFolders();
                bool changed = (f.size() != current.size());
                if (!changed) {
                    for (size_t i = 0; i < f.size(); ++i) {
                        if (f[i].collection_id != current[i].collection_id || f[i].full_path != current[i].full_path) {
                            changed = true;
                            break;
                        }
                    }
                }
                if (changed) {
                    m_folderModel->setFolders(f);
                    setStatus(QString("Loaded %1 KMail folders.").arg(f.size()));
                } else {
                    setStatus(QString("KMail folders up to date (%1).").arg(f.size()));
                }
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, err = QString::fromStdString(e.what())]() {
                if (m_workers.is_shutting_down()) return;
                refreshAkonadiStatus();
                setStatus(err, true);
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (...) {
            setStatus("Error fetching KMail folders.", true);
            setBusy(false);
        }
    });
}

void EndpaperBridge::fetchEmails(qint64 collectionId) {
    if (!m_fetcher || isBusy() || collectionId <= 0) return;
    setBusy(true);
    setStatus("Fetching emails for folder...");

    auto fut = std::make_shared<std::future<std::vector<EmailItem>>>(m_fetcher->fetch_emails(collectionId));

    m_workers.launch([this, fut]() {
        try {
            auto emails = fut->get();
            if (m_workers.is_shutting_down()) return;

            std::vector<ArticleEntry> articles;
            articles.reserve(emails.size());
            for (auto& e : emails) {
                articles.push_back(ArticleEntry{
                    e.akonadi_id, std::move(e.subject), std::move(e.sender),
                    std::move(e.date), "", std::move(e.html_body), false
                });
            }

            QMetaObject::invokeMethod(this, [this, a = std::move(articles)]() mutable {
                if (m_workers.is_shutting_down()) return;
                refreshAkonadiStatus();
                size_t count = a.size();
                m_emailModel->setArticles(std::move(a));
                setStatus(QString("Loaded %1 emails").arg(count));
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, err = QString::fromStdString(e.what())]() {
                if (m_workers.is_shutting_down()) return;
                refreshAkonadiStatus();
                setStatus(err, true);
                setBusy(false);
            }, Qt::QueuedConnection);
        } catch (...) {
            setStatus("Error fetching emails.", true);
            setBusy(false);
        }
    });
}

void EndpaperBridge::stageSelectedEmails() {
    StageOptions opts;
    opts.resolve_stubs = false;
    stageFromModel(m_emailModel, m_emailFilterModel, opts, {}, "emails");
}

// ── Miniflux ──────────────────────────────────────────────────────────

void EndpaperBridge::fetchMinifluxCategories() {
    if (!m_minifluxFetcher || isBusy()) return;

    std::string url = m_config->get("miniflux", "url", "");
    std::string token = m_config->get("miniflux", "token", "");
    if (url.empty() || token.empty()) {
        setStatus("Miniflux URL or token not set in Settings.", true);
        return;
    }

    setBusy(true);
    setStatus("Fetching Miniflux categories...");
    m_minifluxFetcher->set_credentials(url, token);
    bool shouldCache = cacheMetadata();

    m_workers.launch([this, shouldCache]() {
        try {
            auto fut = m_minifluxFetcher->fetch_categories();
            auto cats = fut.get();
            if (m_workers.is_shutting_down()) return;
            if (shouldCache) m_db->save_category_cache(cats);

            QMetaObject::invokeMethod(this, [this, c = std::move(cats)]() {
                if (m_workers.is_shutting_down()) return;
                const auto& currentWrapped = m_minifluxCategoryModel->getCategories();
                std::vector<MinifluxCategory> current;
                if (currentWrapped.size() > 1) {
                    current.assign(currentWrapped.begin() + 1, currentWrapped.end());
                }

                bool changed = (c.size() != current.size());
                if (!changed) {
                    for (size_t i = 0; i < c.size(); ++i) {
                        if (c[i].id != current[i].id || c[i].title != current[i].title || c[i].unread_count != current[i].unread_count) {
                            changed = true;
                            break;
                        }
                    }
                }
                if (changed) {
                    m_minifluxCategoryModel->setCategories(wrap_all_categories(c));
                    setStatus(QString("Loaded %1 Miniflux categories.").arg(c.size()));
                } else {
                    setStatus(QString("Miniflux categories up to date (%1).").arg(c.size()));
                }
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

void EndpaperBridge::fetchMinifluxEntries(qint64 categoryId, int statusFilter) {
    if (!m_minifluxFetcher || isBusy()) return;

    std::string url = m_config->get("miniflux", "url", "");
    std::string token = m_config->get("miniflux", "token", "");
    if (url.empty() || token.empty()) {
        setStatus("Miniflux URL or token not set in Settings.", true);
        return;
    }
    m_minifluxFetcher->set_credentials(url, token);

    setBusy(true);
    setStatus("Fetching Miniflux entries...");

    MinifluxFetcher::FetchParams p;
    p.category_id = categoryId;
    if (statusFilter == 0) p.status = "unread";
    else if (statusFilter == 1) p.starred = true;

    m_workers.launch([this, p]() {
        try {
            auto fut = m_minifluxFetcher->fetch_entries(p);
            auto entries = fut.get();
            if (m_workers.is_shutting_down()) return;

            std::vector<ArticleEntry> articles;
            articles.reserve(entries.size());
            for (auto& e : entries) {
                articles.push_back(ArticleEntry{
                    e.id, std::move(e.title), std::move(e.author),
                    std::move(e.published_at), std::move(e.url), std::move(e.content), e.starred
                });
            }

            QMetaObject::invokeMethod(this, [this, a = std::move(articles)]() mutable {
                if (m_workers.is_shutting_down()) return;
                size_t count = a.size();
                m_minifluxEntryModel->setArticles(std::move(a));
                setStatus(QString("Loaded %1 Miniflux entries").arg(count));
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

void EndpaperBridge::stageSelectedMiniflux() {
    StageOptions opts;
    opts.resolve_stubs = resolveStubs();
    opts.stub_char_threshold = static_cast<size_t>(std::max(0, stubThreshold()));
    opts.request_delay_ms = std::max(0, requestDelayMs());

    StubResolver resolver = [this](const StageItem& it) {
        return m_minifluxFetcher->fetch_original_content(it.source_id).get();
    };
    stageFromModel(m_minifluxEntryModel, m_minifluxFilterModel, opts, resolver, "Miniflux entries");
}

// ── RSS ───────────────────────────────────────────────────────────────

void EndpaperBridge::addRssFeed(const QString& title, const QString& url) {
    if (isBusy()) return;
    QString cleanTitle = title.trimmed();
    QString cleanUrl = url.trimmed();
    if (!m_db || cleanTitle.isEmpty() || cleanUrl.isEmpty()) {
        setStatus("Feed title and URL are required.", true);
        return;
    }
    bool added = m_db->add_rss_feed(cleanTitle.toStdString(), cleanUrl.toStdString());
    if (!added) {
        setStatus("RSS feed already exists: " + cleanUrl, true);
        return;
    }
    fetchRssFeeds();
    setStatus("Added RSS feed: " + cleanTitle);
}

void EndpaperBridge::removeRssFeed(qint64 feedId) {
    if (!m_db || isBusy()) return;
    m_db->delete_rss_feed(feedId);
    if (m_lastRssFeedId == feedId) {
        setLastRssFeedId(-1);
        m_rssEntryModel->setArticles({});
    }
    fetchRssFeeds();
    setStatus("Removed RSS feed.");
}

void EndpaperBridge::fetchRssFeeds() {
    if (!m_db) return;
    m_rssFeedModel->setFeeds(m_db->list_rss_feeds());
}

void EndpaperBridge::fetchRssEntries(qint64 feedId) {
    if (!m_db || !m_rssFetcher || isBusy()) return;

    auto feeds = m_db->list_rss_feeds();
    std::string url;
    for (const auto& f : feeds) {
        if (f.id == feedId) {
            url = f.url;
            break;
        }
    }
    if (url.empty()) return;

    size_t maxItems = static_cast<size_t>(std::max(0, rssMaxArticles()));
    setBusy(true);
    setStatus("Fetching RSS entries...");

    m_workers.launch([this, url, maxItems]() {
        try {
            auto fut = m_rssFetcher->fetch_items(url, maxItems);
            auto entries = fut.get();
            if (m_workers.is_shutting_down()) return;

            std::vector<ArticleEntry> articles;
            articles.reserve(entries.size());
            for (auto& e : entries) {
                articles.push_back(ArticleEntry{
                    0, std::move(e.title), std::move(e.author),
                    std::move(e.date), std::move(e.link), std::move(e.content_html), false
                });
            }

            QMetaObject::invokeMethod(this, [this, a = std::move(articles)]() mutable {
                if (m_workers.is_shutting_down()) return;
                size_t count = a.size();
                m_rssEntryModel->setArticles(std::move(a));
                setStatus(QString("Loaded %1 RSS entries").arg(count));
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

void EndpaperBridge::stageSelectedRss() {
    StageOptions opts;
    opts.resolve_stubs = resolveStubs();
    opts.stub_char_threshold = static_cast<size_t>(std::max(0, stubThreshold()));
    opts.request_delay_ms = std::max(0, requestDelayMs());

    StubResolver resolver = [this](const StageItem& it) {
        return it.url.empty() ? std::string{} : m_htmlExtractor->fetch_and_extract(it.url).get();
    };
    stageFromModel(m_rssEntryModel, m_rssFilterModel, opts, resolver, "RSS entries");
}

// ── Build EPUB ────────────────────────────────────────────────────────

void EndpaperBridge::buildEpub(const QString& author, const QString& publisher, const QString& extraPath, bool append, bool simplify, bool keepImages) {
    if (!m_db || m_selectedBookId == -1 || isBusy()) return;

    QString qTitle = selectedBookTitle();
    std::string bookTitle = qTitle.isEmpty() ? "book" : qTitle.toStdString();
    auto chapters = m_db->get_chapters(m_selectedBookId);
    if (chapters.empty()) {
        setStatus("No chapters to build.", true);
        return;
    }

    // Build-level Append is session-only; simplify & keepImages persist together
    setBuildAppend(append);
    bool prefsChanged = false;
    if (buildSimplify() != simplify) {
        m_config->set("epub", "simplify", simplify ? "true" : "false");
        emit buildSimplifyChanged();
        prefsChanged = true;
    }
    if (buildKeepImages() != keepImages) {
        m_config->set("epub", "keep_images", keepImages ? "true" : "false");
        emit buildKeepImagesChanged();
        prefsChanged = true;
    }
    if (prefsChanged) m_config->save();

    BuildOptions opts;
    opts.author = author.trimmed().isEmpty() ? "Unknown" : author.trimmed().toStdString();
    opts.publisher = publisher.trimmed().isEmpty() ? "Endpaper" : publisher.trimmed().toStdString();
    opts.explicit_path = extraPath.trimmed().toStdString();
    opts.default_folder = defaultOutputFolder().toStdString();
    opts.append = append;
    opts.simplify = simplify;
    opts.keep_images = keepImages;

    setBusy(true);
    setStatus(QString("Building EPUB: %1").arg(QString::fromStdString(bookTitle)));

    m_workers.launch([this, bookTitle, chapters = std::move(chapters), opts]() {
        try {
            auto result = build_book_epub(
                bookTitle, chapters, opts,
                [this](const std::string& step) {
                    setStatus(QString::fromStdString(step));
                },
                m_workers.stop_flag());

            setStatus(QString::fromStdString(format_build_summary(result, opts.keep_images)), false);
            setBusy(false);
        } catch (const std::exception& e) {
            setStatus("Build Error: " + QString::fromStdString(e.what()), true);
            setBusy(false);
        } catch (...) {
            setStatus("Build Error: unexpected failure", true);
            setBusy(false);
        }
    });
}

void EndpaperBridge::pageOpened(const QString& pageName) {
    if (pageName == "kmail" && autoFetchKMail() && !m_kmailAutoFetched) {
        m_kmailAutoFetched = true;
        fetchFolders();
    } else if (pageName == "miniflux" && autoFetchMiniflux() && !m_minifluxAutoFetched) {
        m_minifluxAutoFetched = true;
        fetchMinifluxCategories();
    }
}

// ── Config properties ─────────────────────────────────────────────────

bool EndpaperBridge::autoFetchKMail() const {
    return m_config->get_bool("general", "auto_fetch_kmail", false);
}

void EndpaperBridge::setAutoFetchKMail(bool enabled) {
    if (autoFetchKMail() != enabled) {
        m_config->set("general", "auto_fetch_kmail", enabled ? "true" : "false");
        m_config->save();
        emit autoFetchKMailChanged();
    }
}

bool EndpaperBridge::autoFetchMiniflux() const {
    return m_config->get_bool("general", "auto_fetch_miniflux", false);
}

void EndpaperBridge::setAutoFetchMiniflux(bool enabled) {
    if (autoFetchMiniflux() != enabled) {
        m_config->set("general", "auto_fetch_miniflux", enabled ? "true" : "false");
        m_config->save();
        emit autoFetchMinifluxChanged();
    }
}

bool EndpaperBridge::cacheMetadata() const {
    return m_config->get_bool("general", "cache_metadata", true);
}

void EndpaperBridge::setCacheMetadata(bool enabled) {
    if (cacheMetadata() != enabled) {
        m_config->set("general", "cache_metadata", enabled ? "true" : "false");
        m_config->save();
        emit cacheMetadataChanged();
    }
}

bool EndpaperBridge::rememberState() const {
    return m_config->get_bool("general", "remember_state", true);
}

void EndpaperBridge::setRememberState(bool enabled) {
    if (rememberState() != enabled) {
        m_config->set("general", "remember_state", enabled ? "true" : "false");
        m_config->save();
        emit rememberStateChanged();
    }
}

void EndpaperBridge::setLastPageIndex(int index) {
    int clamped = std::clamp(index, 0, 5);
    if (m_lastPageIndex != clamped) {
        m_lastPageIndex = clamped;
        if (rememberState()) {
            m_config->set("state", "last_page_index", std::to_string(clamped));
            m_config->save();
        }
        emit lastPageIndexChanged();
    }
}

void EndpaperBridge::setLastKMailCollectionId(qint64 id) {
    if (m_lastKMailCollectionId != id) {
        m_lastKMailCollectionId = id;
        if (rememberState()) {
            m_config->set("state", "last_kmail_collection_id", std::to_string(id));
            m_config->save();
        }
        emit lastKMailCollectionIdChanged();
    }
}

void EndpaperBridge::setLastMinifluxCategoryId(qint64 id) {
    if (m_lastMinifluxCategoryId != id) {
        m_lastMinifluxCategoryId = id;
        if (rememberState()) {
            m_config->set("state", "last_miniflux_category_id", std::to_string(id));
            m_config->save();
        }
        emit lastMinifluxCategoryIdChanged();
    }
}

void EndpaperBridge::setLastMinifluxStatusFilter(int filter) {
    int clamped = std::clamp(filter, 0, 2);
    if (m_lastMinifluxStatusFilter != clamped) {
        m_lastMinifluxStatusFilter = clamped;
        if (rememberState()) {
            m_config->set("state", "last_miniflux_status_filter", std::to_string(clamped));
            m_config->save();
        }
        emit lastMinifluxStatusFilterChanged();
    }
}

void EndpaperBridge::setLastRssFeedId(qint64 id) {
    if (m_lastRssFeedId != id) {
        m_lastRssFeedId = id;
        if (rememberState()) {
            m_config->set("state", "last_rss_feed_id", std::to_string(id));
            m_config->save();
        }
        emit lastRssFeedIdChanged();
    }
}

bool EndpaperBridge::resolveStubs() const {
    return m_config->get_bool("fetching", "enable_stub_resolving", true);
}

void EndpaperBridge::setResolveStubs(bool resolve) {
    if (resolveStubs() != resolve) {
        m_config->set("fetching", "enable_stub_resolving", resolve ? "true" : "false");
        m_config->save();
        emit resolveStubsChanged();
    }
}

int EndpaperBridge::stubThreshold() const {
    return m_config->get_int("fetching", "stub_char_threshold", 1000);
}

void EndpaperBridge::setStubThreshold(int threshold) {
    if (threshold >= 0 && stubThreshold() != threshold) {
        m_config->set("fetching", "stub_char_threshold", std::to_string(threshold));
        m_config->save();
        emit stubThresholdChanged();
    }
}

int EndpaperBridge::requestDelayMs() const {
    return m_config->get_int("fetching", "request_delay_ms", 1000);
}

void EndpaperBridge::setRequestDelayMs(int ms) {
    if (ms >= 0 && requestDelayMs() != ms) {
        m_config->set("fetching", "request_delay_ms", std::to_string(ms));
        m_config->save();
        emit requestDelayMsChanged();
    }
}

QString EndpaperBridge::minifluxUrl() const {
    return QString::fromStdString(m_config->get("miniflux", "url", ""));
}

void EndpaperBridge::setMinifluxUrl(const QString& url) {
    QString trimmed = url.trimmed();
    if (minifluxUrl() != trimmed) {
        m_config->set("miniflux", "url", trimmed.toStdString());
        m_config->save();
        if (m_minifluxFetcher) {
            m_minifluxFetcher->set_credentials(trimmed.toStdString(), minifluxToken().toStdString());
        }
        emit minifluxUrlChanged();
    }
}

QString EndpaperBridge::minifluxToken() const {
    return QString::fromStdString(m_config->get("miniflux", "token", ""));
}

void EndpaperBridge::setMinifluxToken(const QString& token) {
    QString trimmed = token.trimmed();
    if (minifluxToken() != trimmed) {
        m_config->set("miniflux", "token", trimmed.toStdString());
        m_config->save();
        if (m_minifluxFetcher) {
            m_minifluxFetcher->set_credentials(minifluxUrl().toStdString(), trimmed.toStdString());
        }
        emit minifluxTokenChanged();
    }
}

QString EndpaperBridge::defaultAuthor() const {
    return QString::fromStdString(m_config->get("epub", "author", "Unknown"));
}

void EndpaperBridge::setDefaultAuthor(const QString& author) {
    if (defaultAuthor() != author) {
        m_config->set("epub", "author", author.toStdString());
        m_config->save();
        emit defaultAuthorChanged();
    }
}

QString EndpaperBridge::defaultPublisher() const {
    return QString::fromStdString(m_config->get("epub", "publisher", "Endpaper"));
}

void EndpaperBridge::setDefaultPublisher(const QString& publisher) {
    if (defaultPublisher() != publisher) {
        m_config->set("epub", "publisher", publisher.toStdString());
        m_config->save();
        emit defaultPublisherChanged();
    }
}

QString EndpaperBridge::defaultOutputFolder() const {
    return QString::fromStdString(m_config->get("general", "default_folder", ""));
}

void EndpaperBridge::setDefaultOutputFolder(const QString& folder) {
    QString trimmed = folder.trimmed();
    if (defaultOutputFolder() != trimmed) {
        m_config->set("general", "default_folder", trimmed.toStdString());
        m_config->save();
        emit defaultOutputFolderChanged();
        emit resolvedOutputPathChanged();
    }
}

int EndpaperBridge::rssMaxArticles() const {
    return m_config->get_int("rss", "max_articles", 50);
}

void EndpaperBridge::setRssMaxArticles(int count) {
    if (count >= 0 && rssMaxArticles() != count) {
        m_config->set("rss", "max_articles", std::to_string(count));
        m_config->save();
        emit rssMaxArticlesChanged();
    }
}

bool EndpaperBridge::fuzzySearch() const {
    return m_config->get_bool("ui", "fuzzy_search", true);
}

void EndpaperBridge::setFuzzySearch(bool enabled) {
    if (fuzzySearch() != enabled) {
        m_config->set("ui", "fuzzy_search", enabled ? "true" : "false");
        m_config->save();
        m_emailFilterModel->setFuzzySearch(enabled);
        m_minifluxFilterModel->setFuzzySearch(enabled);
        m_rssFilterModel->setFuzzySearch(enabled);
        emit fuzzySearchChanged();
    }
}

void EndpaperBridge::setBuildAppend(bool enabled) {
    if (m_buildAppend != enabled) {
        m_buildAppend = enabled;
        emit buildAppendChanged();
    }
}

bool EndpaperBridge::buildSimplify() const {
    return m_config->get_bool("epub", "simplify", true);
}

void EndpaperBridge::setBuildSimplify(bool enabled) {
    if (buildSimplify() != enabled) {
        m_config->set("epub", "simplify", enabled ? "true" : "false");
        m_config->save();
        emit buildSimplifyChanged();
    }
}

bool EndpaperBridge::buildKeepImages() const {
    return m_config->get_bool("epub", "keep_images", false);
}

void EndpaperBridge::setBuildKeepImages(bool enabled) {
    if (buildKeepImages() != enabled) {
        m_config->set("epub", "keep_images", enabled ? "true" : "false");
        m_config->save();
        emit buildKeepImagesChanged();
    }
}
