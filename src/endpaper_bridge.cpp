#include "endpaper_bridge.h"
#include "epub_builder.h"
#include "html_sanitizer.h"

#include <thread>
#include <mutex>
#include <QMetaObject>
#include <QStandardPaths>
#include <filesystem>
#include <iostream>

using namespace endpaper;

EndpaperBridge::EndpaperBridge(QObject* parent) : QObject(parent) {
    m_bookModel = new BookModel(this);
    m_folderModel = new FolderModel(this);
    m_emailModel = new EmailModel(this);
    m_chapterModel = new ChapterModel(this);
    m_minifluxCategoryModel = new MinifluxCategoryModel(this);
    m_minifluxEntryModel = new MinifluxEntryModel(this);
    m_rssFeedModel = new RssFeedModel(this);
    m_rssEntryModel = new RssEntryModel(this);

    m_emailFilterModel = new ArticleFilterModel(this);
    m_emailFilterModel->setSourceModel(m_emailModel);

    m_minifluxFilterModel = new ArticleFilterModel(this);
    m_minifluxFilterModel->setSourceModel(m_minifluxEntryModel);

    m_rssFilterModel = new ArticleFilterModel(this);
    m_rssFilterModel->setSourceModel(m_rssEntryModel);
}

EndpaperBridge::~EndpaperBridge() = default;

void EndpaperBridge::init(Config* config, Database* db, AkonadiFetcher* fetcher, RssFetcher* rssFetcher, MinifluxFetcher* minifluxFetcher) {
    m_config = config;
    m_db = db;
    m_fetcher = fetcher;
    m_rssFetcher = rssFetcher;
    m_minifluxFetcher = minifluxFetcher;
    
    refreshBooks();
    fetchRssFeeds();
}

QString EndpaperBridge::statusMessage() const {
    return m_statusMessage;
}

bool EndpaperBridge::isBusy() const {
    return m_isBusy;
}

void EndpaperBridge::setStatus(const QString& msg) {
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged();
    }
}

void EndpaperBridge::setBusy(bool busy) {
    if (m_isBusy != busy) {
        m_isBusy = busy;
        emit isBusyChanged();
    }
}

void EndpaperBridge::setSelectedBookId(int id) {
    if (m_selectedBookId != id) {
        m_selectedBookId = id;
        emit selectedBookIdChanged();
        refreshChapters();
    }
}

void EndpaperBridge::refreshBooks() {
    if (!m_db) return;
    auto books = m_db->list_books();
    m_bookModel->setBooks(books);
    if (!books.empty() && m_selectedBookId == -1) {
        setSelectedBookId(books[0].id);
    }
}

void EndpaperBridge::createBook(const QString& title) {
    if (!m_db || title.isEmpty()) return;
    int64_t id = m_db->create_book(title.toStdString());
    refreshBooks();
    setSelectedBookId(id);
    setStatus("Book created: " + title);
}

void EndpaperBridge::deleteBook(int bookId) {
    if (!m_db) return;
    m_db->delete_book(bookId);
    if (m_selectedBookId == bookId) m_selectedBookId = -1;
    refreshBooks();
    refreshChapters();
}

void EndpaperBridge::refreshChapters() {
    if (!m_db || m_selectedBookId == -1) {
        m_chapterModel->setChapters({});
        return;
    }
    auto chapters = m_db->get_chapters(m_selectedBookId);
    m_chapterModel->setChapters(chapters);
}

void EndpaperBridge::deleteChapter(int chapterId) {
    if (!m_db) return;
    m_db->delete_chapter(chapterId);
    refreshChapters();
}

void EndpaperBridge::moveChapterUp(int chapterId) {
    if (!m_db || m_selectedBookId == -1) return;
    auto chaps = m_db->get_chapters(m_selectedBookId);
    for (size_t i = 1; i < chaps.size(); ++i) {
        if (chaps[i].id == chapterId) {
            m_db->update_sort_order(chaps[i].id, chaps[i-1].sort_order);
            m_db->update_sort_order(chaps[i-1].id, chaps[i].sort_order);
            refreshChapters();
            break;
        }
    }
}

void EndpaperBridge::moveChapterDown(int chapterId) {
    if (!m_db || m_selectedBookId == -1) return;
    auto chaps = m_db->get_chapters(m_selectedBookId);
    for (size_t i = 0; i < chaps.size(); ++i) {
        if (i < chaps.size() - 1 && chaps[i].id == chapterId) {
            m_db->update_sort_order(chaps[i].id, chaps[i+1].sort_order);
            m_db->update_sort_order(chaps[i+1].id, chaps[i].sort_order);
            refreshChapters();
            break;
        }
    }
}

void EndpaperBridge::fetchFolders() {
    if (!m_fetcher || isBusy()) return;
    setBusy(true);
    setStatus("Fetching KMail folders...");
    
    // We launch a future but Akonadi fetching already runs async in AkonadiFetcher
    // For simplicity, we can do it on a background thread and then post to main thread.
    std::thread([this]() {
        try {
            auto fut = m_fetcher->fetch_folders();
            auto folders = fut.get();
            QMetaObject::invokeMethod(this, [this, f = std::move(folders)]() {
                m_folderModel->setFolders(f);
                setStatus(QString("Loaded %1 folders").arg(f.size()));
                setBusy(false);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, err = QString::fromStdString(e.what())]() {
                setStatus("Error: " + err);
                setBusy(false);
            });
        }
    }).detach();
}

void EndpaperBridge::fetchEmails(int collectionId) {
    if (!m_fetcher || isBusy() || collectionId <= 0) return;
    setBusy(true);
    setStatus("Fetching emails for folder...");
    
    std::thread([this, collectionId]() {
        try {
            auto fut = m_fetcher->fetch_emails(collectionId);
            auto emails = fut.get();
            QMetaObject::invokeMethod(this, [this, e = std::move(emails)]() {
                m_emailModel->setEmails(e);
                setStatus(QString("Loaded %1 emails").arg(e.size()));
                setBusy(false);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, err = QString::fromStdString(e.what())]() {
                setStatus("Error: " + err);
                setBusy(false);
            });
        }
    }).detach();
}

void EndpaperBridge::selectAllEmails() {
    m_emailModel->selectAll();
}

void EndpaperBridge::deselectAllEmails() {
    m_emailModel->deselectAll();
}

void EndpaperBridge::stageSelectedEmails() {
    if (!m_db || m_selectedBookId == -1) {
        setStatus("Please select a book first!");
        return;
    }
    
    const auto& emails = m_emailModel->getEmails();
    int baseOrder = m_db->max_sort_order(m_selectedBookId);
    int staged = 0;
    
    for (size_t i = 0; i < emails.size(); ++i) {
        if (m_emailModel->isSelected(i)) {
            m_db->add_chapter(
                m_selectedBookId,
                emails[i].subject,
                emails[i].sender,
                sanitize_html(emails[i].html_body),
                baseOrder + staged + 1
            );
            staged++;
        }
    }
    
    setStatus(QString("Staged %1 emails").arg(staged));
    m_emailModel->deselectAll();
    refreshChapters();
}

// ── Miniflux ──────────────────────────────────────────────────────────

void EndpaperBridge::fetchMinifluxCategories() {
    if (!m_minifluxFetcher || isBusy()) return;
    
    std::string url = m_config->get("miniflux", "url", "");
    std::string token = m_config->get("miniflux", "token", "");
    if (url.empty() || token.empty()) {
        setStatus("Miniflux URL or token not set in Settings.");
        return;
    }
    
    setBusy(true);
    setStatus("Fetching Miniflux categories...");
    m_minifluxFetcher->set_credentials(url, token);
    
    std::thread([this]() {
        try {
            auto fut = m_minifluxFetcher->fetch_categories();
            auto cats = fut.get();
            QMetaObject::invokeMethod(this, [this, c = std::move(cats)]() {
                m_minifluxCategoryModel->setCategories(c);
                setStatus(QString("Loaded %1 Miniflux categories").arg(c.size()));
                setBusy(false);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, err = QString::fromStdString(e.what())]() {
                setStatus("Error: " + err);
                setBusy(false);
            });
        }
    }).detach();
}

void EndpaperBridge::fetchMinifluxEntries(int categoryId, int statusFilter) {
    if (!m_minifluxFetcher || isBusy()) return;
    
    std::string url = m_config->get("miniflux", "url", "");
    std::string token = m_config->get("miniflux", "token", "");
    if (url.empty() || token.empty()) {
        setStatus("Miniflux URL or token not set in Settings.");
        return;
    }
    m_minifluxFetcher->set_credentials(url, token);
    
    setBusy(true);
    setStatus("Fetching Miniflux entries...");
    
    MinifluxFetcher::FetchParams p;
    p.category_id = categoryId > 0 ? categoryId : -1;
    if (statusFilter == 0) p.status = "unread";
    else if (statusFilter == 1) p.starred = true;
    
    std::thread([this, p]() {
        try {
            auto fut = m_minifluxFetcher->fetch_entries(p);
            auto entries = fut.get();
            QMetaObject::invokeMethod(this, [this, e = std::move(entries)]() {
                m_minifluxEntryModel->setEntries(e);
                setStatus(QString("Loaded %1 Miniflux entries").arg(e.size()));
                setBusy(false);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, err = QString::fromStdString(e.what())]() {
                setStatus("Error: " + err);
                setBusy(false);
            });
        }
    }).detach();
}

void EndpaperBridge::stageSelectedMiniflux() {
    if (!m_db || m_selectedBookId == -1) {
        setStatus("Please select a book first!");
        return;
    }
    
    const auto& entries = m_minifluxEntryModel->getEntries();
    int baseOrder = m_db->max_sort_order(m_selectedBookId);
    int staged = 0;
    
    for (size_t i = 0; i < entries.size(); ++i) {
        if (m_minifluxEntryModel->isSelected(i)) {
            m_db->add_chapter(
                m_selectedBookId,
                entries[i].title,
                entries[i].author,
                sanitize_html(entries[i].content),
                baseOrder + staged + 1
            );
            staged++;
        }
    }
    
    setStatus(QString("Staged %1 Miniflux entries").arg(staged));
    m_minifluxEntryModel->deselectAll();
    refreshChapters();
}

void EndpaperBridge::selectAllMiniflux() { m_minifluxEntryModel->selectAll(); }
void EndpaperBridge::deselectAllMiniflux() { m_minifluxEntryModel->deselectAll(); }

// ── RSS ───────────────────────────────────────────────────────────────

void EndpaperBridge::addRssFeed(const QString& title, const QString& url) {
    if (!m_db || title.isEmpty() || url.isEmpty()) return;
    m_db->add_rss_feed(title.toStdString(), url.toStdString());
    fetchRssFeeds();
    setStatus("Added RSS feed: " + title);
}

void EndpaperBridge::removeRssFeed(int feedId) {
    if (!m_db) return;
    m_db->delete_rss_feed(feedId);
    fetchRssFeeds();
    setStatus("Removed RSS feed");
}

void EndpaperBridge::fetchRssFeeds() {
    if (!m_db) return;
    auto feeds = m_db->list_rss_feeds();
    m_rssFeedModel->setFeeds(feeds);
    // setStatus(QString("Loaded %1 RSS feeds").arg(feeds.size()));
}

void EndpaperBridge::fetchRssEntries(int feedId) {
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

    setBusy(true);
    setStatus("Fetching RSS entries...");
    
    std::thread([this, url]() {
        try {
            auto fut = m_rssFetcher->fetch_items(url, 0);
            auto entries = fut.get();
            QMetaObject::invokeMethod(this, [this, e = std::move(entries)]() {
                m_rssEntryModel->setEntries(e);
                setStatus(QString("Loaded %1 RSS entries").arg(e.size()));
                setBusy(false);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, err = QString::fromStdString(e.what())]() {
                setStatus("Error: " + err);
                setBusy(false);
            });
        }
    }).detach();
}

void EndpaperBridge::stageSelectedRss() {
    if (!m_db || m_selectedBookId == -1) {
        setStatus("Please select a book first!");
        return;
    }
    
    const auto& entries = m_rssEntryModel->getEntries();
    int baseOrder = m_db->max_sort_order(m_selectedBookId);
    int staged = 0;
    
    for (size_t i = 0; i < entries.size(); ++i) {
        if (m_rssEntryModel->isSelected(i)) {
            m_db->add_chapter(
                m_selectedBookId,
                entries[i].title,
                entries[i].author,
                sanitize_html(entries[i].content_html),
                baseOrder + staged + 1
            );
            staged++;
        }
    }
    
    setStatus(QString("Staged %1 RSS entries").arg(staged));
    m_rssEntryModel->deselectAll();
    refreshChapters();
}

void EndpaperBridge::selectAllRss() { m_rssEntryModel->selectAll(); }
void EndpaperBridge::deselectAllRss() { m_rssEntryModel->deselectAll(); }

static std::string slugify(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (std::isspace(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            if (!result.empty() && result.back() != '-') {
                result += '-';
            }
        }
    }
    while (!result.empty() && result.back() == '-') result.pop_back();
    return result.empty() ? "book" : result;
}

void EndpaperBridge::buildEpub(const QString& author, const QString& publisher, const QString& extraPath, bool append, bool simplify) {
    if (!m_db || m_selectedBookId == -1 || isBusy()) return;
    
    auto books = m_db->list_books();
    std::string bookTitle = "book";
    for (const auto& b : books) {
        if (b.id == m_selectedBookId) {
            bookTitle = b.title;
            break;
        }
    }
    
    auto chapters = m_db->get_chapters(m_selectedBookId);
    if (chapters.empty()) {
        setStatus("No chapters to build.");
        return;
    }
    
    setBusy(true);
    setStatus(QString("Building EPUB: %1").arg(QString::fromStdString(bookTitle)));
    
    std::string outPath = m_config->get("general", "default_folder", "");
    if (!extraPath.isEmpty()) outPath = extraPath.toStdString();
    
    // 1. Expand tilde
    if (!outPath.empty() && outPath[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home) outPath = std::string(home) + outPath.substr(1);
    }
    
    // 2. Default to Documents if empty
    if (outPath.empty()) {
        QString docPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!docPath.isEmpty()) {
            outPath = docPath.toStdString();
        } else {
            outPath = ".";
        }
    }

    // 3. Resolve final path: if it's a directory, append slug. If it's a file, keep it.
    std::filesystem::path p(outPath);
    if (std::filesystem::is_directory(p) || outPath.back() == '/' || p.extension() != ".epub") {
        if (!outPath.empty() && outPath.back() != '/') outPath += "/";
        outPath += slugify(bookTitle) + ".epub";
    }
    
    std::string stdAuthor = author.toStdString();
    std::string stdPublisher = publisher.toStdString();
    
    std::thread([this, bookTitle, outPath, stdAuthor, stdPublisher, chapters, append, simplify]() {
        try {
            std::vector<EpubChapter> epubChapters;
            for (const auto& c : chapters) {
                std::string content = simplify ? simplify_html(c.content_html) : c.content_html;
                epubChapters.push_back({c.subject, c.sender, content});
            }
            
            EpubMetadata meta;
            meta.title = bookTitle;
            meta.author = stdAuthor;
            meta.publisher = stdPublisher;
            
            if (append && std::filesystem::exists(outPath)) {
                EpubBuilder::append(outPath, epubChapters);
            } else {
                EpubBuilder::build(outPath, meta, epubChapters);
            }
            
            QMetaObject::invokeMethod(this, [this, outPath]() {
                setStatus(QString("Successfully built: %1").arg(QString::fromStdString(outPath)));
                setBusy(false);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, err = QString::fromStdString(e.what())]() {
                setStatus("Build Error: " + err);
                setBusy(false);
            });
        }
    }).detach();
}
