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

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>

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
                    fi.name = col.displayName().toStdString();

                    std::string path = fi.name;
                    auto parent = col.parentCollection();
                    while (parent.isValid() &&
                           parent != Akonadi::Collection::root()) {
                        path = parent.displayName().toStdString() + "/" + path;
                        parent = parent.parentCollection();
                    }
                    fi.full_path = path;
                    folders.push_back(std::move(fi));
                }
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
