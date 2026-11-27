#include "akonadi_fetcher.h"
#include "html_sanitizer.h"

#include <Akonadi/CollectionFetchJob>
#include <Akonadi/CollectionFetchScope>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ServerManager>

#include <KMime/Message>
#include <KMime/Util>

#include <QMetaObject>
#include <QObject>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace endpaper {

namespace {

template <typename T>
struct MessagePtrType {
    using type = std::shared_ptr<T>;
};

template <typename T>
    requires requires { typename T::Ptr; }
struct MessagePtrType<T> {
    using type = typename T::Ptr;
};

using MessagePtr = typename MessagePtrType<KMime::Message>::type;

template <typename T>
std::future<T> make_error_future(const std::string& message) {
    std::promise<T> p;
    p.set_exception(std::make_exception_ptr(std::runtime_error(message)));
    return p.get_future();
}

} // namespace

// ── Pimpl ─────────────────────────────────────────────────────────────────

struct AkonadiFetcher::Impl {
    std::mutex mu;
    QThread thread;
    QObject* worker = nullptr;
    bool started = false;
    std::atomic<bool> stopping{false};

    ~Impl() {
        stop();
    }

    void stop() {
        QObject* w = nullptr;
        {
            std::lock_guard<std::mutex> lock(mu);
            stopping.store(true);
            if (!started) return;
            w = worker;
            started = false;
        }
        if (w) {
            QMetaObject::invokeMethod(w, [this, w]() {
                const auto jobs = w->findChildren<KJob*>(Qt::FindDirectChildrenOnly);
                for (KJob* j : jobs) {
                    delete j;
                }
                thread.quit();
            }, Qt::QueuedConnection);
        } else {
            thread.quit();
        }
        thread.wait();
        worker = nullptr;
    }

    QObject* ensure_worker() {
        std::lock_guard<std::mutex> lock(mu);
        if (stopping.load()) return nullptr;
        if (!started) {
            worker = new QObject();
            worker->moveToThread(&thread);
            QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
            thread.start();
            started = true;
        }
        return worker;
    }

    // Helper: find the best text body from a KMime message
    static std::string extract_body(const KMime::Message* msg) {
        if (!msg) return "";
        // Prefer text/html, fall back to text/plain
        if (const auto* html_part = msg->mainBodyPart("text/html")) {
            return html_part->decodedText().toStdString();
        }
        if (const auto* text_part = msg->mainBodyPart("text/plain")) {
            return plain_text_to_html(
                text_part->decodedText().toStdString());
        }
        // Last resort: raw decoded body
        return msg->decodedText().toStdString();
    }
};

// ── Constructor / Destructor ──────────────────────────────────────────────

AkonadiFetcher::AkonadiFetcher()
    : impl_(std::make_unique<Impl>()) {}

AkonadiFetcher::~AkonadiFetcher() = default;

void AkonadiFetcher::stop() {
    impl_->stop();
}

// ── Availability check ────────────────────────────────────────────────────

bool AkonadiFetcher::is_available() const {
    return Akonadi::ServerManager::isRunning();
}

// ── Fetch folders ─────────────────────────────────────────────────────────

void populate_folder_paths(std::vector<FolderInfo>& folders) {
    std::unordered_map<int64_t, size_t> by_id;
    by_id.reserve(folders.size());
    for (size_t i = 0; i < folders.size(); ++i) {
        by_id.emplace(folders[i].collection_id, i);
    }

    for (auto& f : folders) {
        if (f.parent_id == f.collection_id || by_id.find(f.parent_id) == by_id.end()) {
            f.parent_id = -1;
        }
    }

    for (size_t i = 0; i < folders.size(); ++i) {
        std::vector<std::string> parts;
        std::vector<int64_t> seen;
        const FolderInfo* cur = &folders[i];

        for (int depth = 0; depth < 32; ++depth) {
            if (!cur->name.empty()) {
                parts.push_back(cur->name);
            }
            seen.push_back(cur->collection_id);
            if (cur->parent_id == -1) break;
            if (std::find(seen.begin(), seen.end(), cur->parent_id) != seen.end()) break;
            auto it = by_id.find(cur->parent_id);
            if (it == by_id.end()) break;
            cur = &folders[it->second];
        }

        std::string full;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!full.empty()) full += '/';
            full += *it;
        }
        folders[i].full_path = full.empty() ? folders[i].name : std::move(full);
    }
}

bool is_system_mail_folder(const std::string& name) {
    size_t a = 0;
    while (a < name.size() && std::isspace(static_cast<unsigned char>(name[a]))) ++a;
    size_t b = name.size();
    while (b > a && std::isspace(static_cast<unsigned char>(name[b - 1]))) --b;

    std::string lower;
    lower.reserve(b - a);
    for (size_t i = a; i < b; ++i) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(name[i]))));
    }

    static const std::unordered_set<std::string> kSystemFolders = {
        "trash", "bin", "deleted", "deleted items", "deleted messages",
        "sent", "sent-mail", "sent mail", "sent items", "sent messages",
        "drafts", "draft", "outbox", "templates",
        "junk", "junk email", "junk e-mail", "spam", "bulk mail"
    };
    return kSystemFolders.find(lower) != kSystemFolders.end();
}

std::future<std::vector<FolderInfo>> AkonadiFetcher::fetch_folders() {
    if (!is_available()) {
        return make_error_future<std::vector<FolderInfo>>(
            "Akonadi server is not running (try: akonadictl start)");
    }

    QObject* worker = impl_->ensure_worker();
    if (!worker) {
        return make_error_future<std::vector<FolderInfo>>("Akonadi fetcher is shutting down");
    }

    auto prom = std::make_shared<std::promise<std::vector<FolderInfo>>>();
    auto fut = prom->get_future();

    QMetaObject::invokeMethod(worker, [worker, prom]() {
        auto* job = new Akonadi::CollectionFetchJob(
            Akonadi::Collection::root(),
            Akonadi::CollectionFetchJob::Recursive,
            worker);

        job->fetchScope().setContentMimeTypes({"message/rfc822"});

        QObject::connect(job, &Akonadi::CollectionFetchJob::result,
                         worker, [prom](KJob* j) {
            if (j->error()) {
                prom->set_exception(std::make_exception_ptr(
                    std::runtime_error("KMail folder fetch error: " + j->errorString().toStdString())));
                return;
            }
            try {
                auto* fetch_job = qobject_cast<Akonadi::CollectionFetchJob*>(j);
                const auto collections = fetch_job->collections();
                std::vector<FolderInfo> folders;
                folders.reserve(static_cast<size_t>(collections.size()));
                for (const auto& col : collections) {
                    FolderInfo fi;
                    fi.collection_id = col.id();
                    QString disp = col.displayName().trimmed();
                    if (disp.isEmpty()) disp = col.name().trimmed();
                    fi.name = disp.toStdString();

                    auto parent = col.parentCollection();
                    fi.parent_id = (parent.isValid() && parent != Akonadi::Collection::root())
                        ? parent.id()
                        : -1;
                    folders.push_back(std::move(fi));
                }
                populate_folder_paths(folders);
                prom->set_value(std::move(folders));
            } catch (...) {
                prom->set_exception(std::current_exception());
            }
        });

        job->start();
    }, Qt::QueuedConnection);

    return fut;
}

// ── Fetch emails ──────────────────────────────────────────────────────────

std::future<std::vector<EmailItem>>
AkonadiFetcher::fetch_emails(int64_t collection_id) {
    if (!is_available()) {
        return make_error_future<std::vector<EmailItem>>(
            "Akonadi server is not running (try: akonadictl start)");
    }

    QObject* worker = impl_->ensure_worker();
    if (!worker) {
        return make_error_future<std::vector<EmailItem>>("Akonadi fetcher is shutting down");
    }

    auto prom = std::make_shared<std::promise<std::vector<EmailItem>>>();
    auto fut = prom->get_future();

    QMetaObject::invokeMethod(worker, [worker, prom, collection_id]() {
        Akonadi::Collection col(collection_id);
        auto* job = new Akonadi::ItemFetchJob(col, worker);
        job->fetchScope().fetchFullPayload(true);

        QObject::connect(job, &Akonadi::ItemFetchJob::result,
                         worker, [prom](KJob* j) {
            if (j->error()) {
                prom->set_exception(std::make_exception_ptr(
                    std::runtime_error("KMail email fetch error: " + j->errorString().toStdString())));
                return;
            }
            try {
                auto* fetch_job = qobject_cast<Akonadi::ItemFetchJob*>(j);
                const auto items = fetch_job->items();
                std::vector<EmailItem> emails;
                emails.reserve(static_cast<size_t>(items.size()));
                for (const auto& item : items) {
                    MessagePtr msg;

                    // Akonadi serializer already parsed the message when hasPayload is true;
                    // calling parse() a second time strips multipart sub-contents.
                    if (item.hasPayload<MessagePtr>()) {
                        msg = item.payload<MessagePtr>();
                        if (!msg) continue;
                    } else {
                        QByteArray rawData = item.payloadData();
                        if (rawData.isEmpty()) {
                            continue;
                        }
                        msg = MessagePtr(new KMime::Message);
                        msg->setContent(KMime::CRLFtoLF(rawData));
                        msg->parse();
                    }

                    const KMime::Message& cmsg = *msg;

                    EmailItem ei;
                    ei.akonadi_id = item.id();
                    ei.subject = cmsg.subject()
                        ? cmsg.subject()->asUnicodeString().toStdString()
                        : "(no subject)";
                    ei.sender = cmsg.from()
                        ? cmsg.from()->asUnicodeString().toStdString()
                        : "(unknown sender)";
                    ei.date = cmsg.date()
                        ? cmsg.date()->asUnicodeString().toStdString()
                        : "";
                    ei.html_body = Impl::extract_body(&cmsg);

                    emails.push_back(std::move(ei));
                }
                prom->set_value(std::move(emails));
            } catch (...) {
                prom->set_exception(std::current_exception());
            }
        });

        job->start();
    }, Qt::QueuedConnection);

    return fut;
}

} // namespace endpaper
