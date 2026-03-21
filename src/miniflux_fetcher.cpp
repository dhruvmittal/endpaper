#include "miniflux_fetcher.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <thread>
#include <iostream>

namespace endpaper {

class MinifluxFetcherPrivate : public QObject {
    Q_OBJECT
public:
    std::string server_url;
    std::string token;

    MinifluxFetcherPrivate() = default;
};

MinifluxFetcher::MinifluxFetcher() : d(std::make_unique<MinifluxFetcherPrivate>()) {}
MinifluxFetcher::~MinifluxFetcher() = default;

void MinifluxFetcher::set_credentials(const std::string& server_url, const std::string& token) {
    d->server_url = server_url;
    // Strip trailing slash if present
    if (!d->server_url.empty() && d->server_url.back() == '/') {
        d->server_url.pop_back();
    }
    d->token = token;
}

std::future<std::vector<MinifluxCategory>> MinifluxFetcher::fetch_categories() {
    auto promise = std::make_shared<std::promise<std::vector<MinifluxCategory>>>();
    auto future = promise->get_future();

    std::string base_url = d->server_url;
    std::string token = d->token;

    std::thread([promise, base_url, token]() {
        try {
            int argc = 0;
            char** argv = nullptr;
            QCoreApplication* app = QCoreApplication::instance();
            bool created_app = false;
            if (!app) {
                app = new QCoreApplication(argc, argv);
                created_app = true;
            }

            QNetworkAccessManager manager;
            QNetworkRequest request(QUrl(QString::fromStdString(base_url + "/v1/categories?counts=true")));
            request.setHeader(QNetworkRequest::UserAgentHeader, "Endpaper/0.1");
            request.setRawHeader("X-Auth-Token", QByteArray::fromStdString(token));

            QNetworkReply* reply = manager.get(request);
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(30000);

            loop.exec();

            if (reply->error() == QNetworkReply::NoError && timer.isActive()) {
                QByteArray data = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                QJsonArray arr = doc.array();

                std::vector<MinifluxCategory> categories;
                for (const auto& val : arr) {
                    QJsonObject obj = val.toObject();
                    MinifluxCategory cat;
                    cat.id = obj["id"].toVariant().toLongLong();
                    cat.title = obj["title"].toString().toStdString();
                    cat.unread_count = obj["total_unread"].toInt();
                    categories.push_back(std::move(cat));
                }
                promise->set_value(std::move(categories));
            } else {
                std::string err = !timer.isActive() ? "Timeout" : reply->errorString().toStdString();
                promise->set_exception(std::make_exception_ptr(std::runtime_error("Miniflux API error: " + err)));
            }

            reply->deleteLater();
            if (created_app) delete app;
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    }).detach();

    return future;
}

std::future<std::vector<MinifluxEntry>> MinifluxFetcher::fetch_entries(const FetchParams& params) {
    auto promise = std::make_shared<std::promise<std::vector<MinifluxEntry>>>();
    auto future = promise->get_future();

    std::string base_url = d->server_url;
    std::string token = d->token;

    std::thread([promise, base_url, token, params]() {
        try {
            int argc = 0;
            char** argv = nullptr;
            QCoreApplication* app = QCoreApplication::instance();
            bool created_app = false;
            if (!app) {
                app = new QCoreApplication(argc, argv);
                created_app = true;
            }

            QUrl url(QString::fromStdString(base_url + "/v1/entries"));
            QUrlQuery query;
            query.addQueryItem("direction", "desc");
            query.addQueryItem("limit", "100"); // Default limit for safety

            if (params.category_id != -1) {
                query.addQueryItem("category_id", QString::number(params.category_id));
            }
            if (!params.status.empty()) {
                query.addQueryItem("status", QString::fromStdString(params.status));
            }
            if (params.starred) {
                query.addQueryItem("starred", "true");
            }
            if (!params.search.empty()) {
                query.addQueryItem("search", QString::fromStdString(params.search));
            }
            
            url.setQuery(query);

            QNetworkAccessManager manager;
            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::UserAgentHeader, "Endpaper/0.1");
            request.setRawHeader("X-Auth-Token", QByteArray::fromStdString(token));

            QNetworkReply* reply = manager.get(request);
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(30000);

            loop.exec();

            if (reply->error() == QNetworkReply::NoError && timer.isActive()) {
                QByteArray data = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                QJsonObject root = doc.object();
                QJsonArray entries_arr = root["entries"].toArray();

                std::vector<MinifluxEntry> entries;
                for (const auto& val : entries_arr) {
                    QJsonObject obj = val.toObject();
                    MinifluxEntry ent;
                    ent.id = obj["id"].toVariant().toLongLong();
                    ent.title = obj["title"].toString().toStdString();
                    ent.author = obj["author"].toString().toStdString();
                    ent.content = obj["content"].toString().toStdString();
                    ent.url = obj["url"].toString().toStdString();
                    ent.status = obj["status"].toString().toStdString();
                    ent.starred = obj["starred"].toBool();
                    ent.published_at = obj["published_at"].toString().toStdString();
                    
                    if (ent.author.empty()) {
                        QJsonObject feed_obj = obj["feed"].toObject();
                        ent.author = feed_obj["title"].toString().toStdString();
                    }
                    
                    entries.push_back(std::move(ent));
                }
                promise->set_value(std::move(entries));
            } else {
                std::string err = !timer.isActive() ? "Timeout" : reply->errorString().toStdString();
                promise->set_exception(std::make_exception_ptr(std::runtime_error("Miniflux API error: " + err)));
            }

            reply->deleteLater();
            if (created_app) delete app;
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    }).detach();

    return future;
}

std::future<std::string> MinifluxFetcher::fetch_original_content(int64_t entry_id) {
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    std::string base_url = d->server_url;
    std::string token = d->token;

    std::thread([promise, base_url, token, entry_id]() {
        try {
            int argc = 0;
            char** argv = nullptr;
            QCoreApplication* app = QCoreApplication::instance();
            bool created_app = false;
            if (!app) {
                app = new QCoreApplication(argc, argv);
                created_app = true;
            }

            QNetworkAccessManager manager;
            QNetworkRequest request(QUrl(QString::fromStdString(base_url + "/v1/entries/" + std::to_string(entry_id) + "/fetch-content")));
            request.setHeader(QNetworkRequest::UserAgentHeader, "Endpaper/0.1");
            request.setRawHeader("X-Auth-Token", QByteArray::fromStdString(token));

            QNetworkReply* reply = manager.get(request);
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(30000);

            loop.exec();

            if (reply->error() == QNetworkReply::NoError && timer.isActive()) {
                QByteArray data = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                QJsonObject obj = doc.object();
                std::string content = obj["content"].toString().toStdString();
                promise->set_value(std::move(content));
            } else {
                std::string err = !timer.isActive() ? "Timeout" : reply->errorString().toStdString();
                promise->set_exception(std::make_exception_ptr(std::runtime_error("Miniflux API error (fetch-content): " + err)));
            }

            reply->deleteLater();
            if (created_app) delete app;
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    }).detach();

    return future;
}

} // namespace endpaper

#include "miniflux_fetcher.moc"
