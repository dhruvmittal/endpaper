#include "akonadi_fetcher.h"
#include "html_sanitizer.h"

#include <Akonadi/CollectionFetchJob>
#include <Akonadi/CollectionFetchScope>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ServerManager>

#include <KMime/Message>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <iostream>
#include <thread>

namespace endpaper {

// ── Pimpl ─────────────────────────────────────────────────────────────────

struct AkonadiFetcher::Impl {
    // Helper: find the best text body from a KMime message
    static std::string extract_body(const KMime::Message::Ptr& msg) {
        // Prefer text/html, fall back to text/plain
        if (auto* html_part = msg->mainBodyPart("text/html")) {
            return html_part->decodedText().toStdString();
        }
        if (auto* text_part = msg->mainBodyPart("text/plain")) {
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

// ── Availability check ────────────────────────────────────────────────────

bool AkonadiFetcher::is_available() const {
    return Akonadi::ServerManager::isRunning();
}

// ── Fetch folders ─────────────────────────────────────────────────────────

std::future<std::vector<FolderInfo>> AkonadiFetcher::fetch_folders() {
    return std::async(std::launch::async, []() -> std::vector<FolderInfo> {
        std::vector<FolderInfo> folders;

        // Create a local event loop for synchronous waiting on the job
        QEventLoop loop;

        auto* job = new Akonadi::CollectionFetchJob(
            Akonadi::Collection::root(),
            Akonadi::CollectionFetchJob::Recursive);

        job->fetchScope().setContentMimeTypes({"message/rfc822"});

        QObject::connect(job, &Akonadi::CollectionFetchJob::result,
                         &loop, [&](KJob* j) {
            if (j->error()) {
                std::cerr << "[endpaper] Collection fetch error: "
                          << j->errorString().toStdString() << "\n";
            } else {
                auto* fetch_job =
                    qobject_cast<Akonadi::CollectionFetchJob*>(j);
                const auto collections = fetch_job->collections();
                for (const auto& col : collections) {
                    FolderInfo fi;
                    fi.collection_id = col.id();
                    fi.name = col.displayName().toStdString();

                    // Build full path by walking parents
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
            }
            loop.quit();
        });

        job->start();
        loop.exec();

        return folders;
    });
}

// ── Fetch emails ──────────────────────────────────────────────────────────

std::future<std::vector<EmailItem>>
AkonadiFetcher::fetch_emails(int64_t collection_id) {
    return std::async(std::launch::async,
                      [this, collection_id]() -> std::vector<EmailItem> {
        std::vector<EmailItem> emails;

        QEventLoop loop;

        Akonadi::Collection col(collection_id);
        auto* job = new Akonadi::ItemFetchJob(col);
        job->fetchScope().fetchFullPayload(true);

        QObject::connect(job, &Akonadi::ItemFetchJob::result,
                         &loop, [&](KJob* j) {
            if (j->error()) {
                std::cerr << "[endpaper] Item fetch error: "
                          << j->errorString().toStdString() << "\n";
            } else {
                auto* fetch_job =
                    qobject_cast<Akonadi::ItemFetchJob*>(j);
                const auto items = fetch_job->items();
                for (const auto& item : items) {
                    KMime::Message::Ptr msg;

                    // Try typed payload first (works if serializer
                    // plugin is loaded)
                    if (item.hasPayload<KMime::Message::Ptr>()) {
                        msg = item.payload<KMime::Message::Ptr>();
                    } else {
                        // Fallback: parse raw payload bytes directly.
                        // This bypasses the serializer plugin entirely.
                        QByteArray rawData = item.payloadData();
                        if (rawData.isEmpty()) {
                            continue;
                        }
                        msg = KMime::Message::Ptr(new KMime::Message);
                        msg->setContent(rawData);
                    }

                    msg->parse();

                    EmailItem ei;
                    ei.akonadi_id = item.id();
                    ei.subject = msg->subject()
                        ? msg->subject()->asUnicodeString().toStdString()
                        : "(no subject)";
                    ei.sender = msg->from()
                        ? msg->from()->asUnicodeString().toStdString()
                        : "(unknown sender)";
                    ei.date = msg->date()
                        ? msg->date()->asUnicodeString().toStdString()
                        : "";
                    ei.html_body = Impl::extract_body(msg);

                    emails.push_back(std::move(ei));
                }
            }
            loop.quit();
        });

        job->start();
        loop.exec();

        return emails;
    });
}

} // namespace endpaper
