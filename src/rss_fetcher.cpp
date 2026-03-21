#include "rss_fetcher.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>

#include <pugixml.hpp>
#include <iostream>
#include <algorithm>

namespace endpaper {

class RssFetcherPrivate : public QObject {
    Q_OBJECT
public:
    QNetworkAccessManager* manager;

    RssFetcherPrivate() {
        manager = new QNetworkAccessManager(this);
    }
};

RssFetcher::RssFetcher() : d(std::make_unique<RssFetcherPrivate>()) {}
RssFetcher::~RssFetcher() = default;

std::future<std::vector<RssItem>> RssFetcher::fetch_items(const std::string& url, size_t max_items) {
    auto promise = std::make_shared<std::promise<std::vector<RssItem>>>();
    auto future = promise->get_future();

    std::thread([promise, url, max_items]() {
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
            QNetworkRequest request(QUrl(QString::fromStdString(url)));
            
            // In Qt6, we use RedirectPolicyAttribute instead of FollowRedirectsAttribute
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            request.setHeader(QNetworkRequest::UserAgentHeader, "Endpaper/0.1");

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
                auto items = RssFetcher::parse_xml(data.toStdString(), max_items);
                promise->set_value(std::move(items));
            } else {
                if (!timer.isActive()) {
                    promise->set_exception(std::make_exception_ptr(std::runtime_error("RSS fetch timeout")));
                } else {
                    promise->set_exception(std::make_exception_ptr(std::runtime_error(reply->errorString().toStdString())));
                }
            }

            reply->deleteLater();
            if (created_app) delete app;
        } catch (const std::exception& e) {
            try {
                promise->set_exception(std::make_exception_ptr(e));
            } catch (...) {}
        } catch (...) {
            try {
                promise->set_exception(std::make_exception_ptr(std::runtime_error("Unknown error in RSS fetch thread")));
            } catch (...) {}
        }
    }).detach();

    return future;
}

std::vector<RssItem> RssFetcher::parse_xml(const std::string& xml_data, size_t max_items) {
    std::vector<RssItem> items;
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xml_data.c_str());

    if (!result) return items;

    // Try RSS 2.0
    pugi::xml_node channel = doc.child("rss").child("channel");
    if (channel) {
        for (pugi::xml_node item : channel.children("item")) {
            RssItem ri;
            ri.title = item.child_value("title");
            ri.author = item.child_value("author");
            if (ri.author.empty()) ri.author = item.child_value("dc:creator");
            
            ri.content_html = item.child_value("content:encoded");
            if (ri.content_html.empty()) ri.content_html = item.child_value("description");
            
            ri.link = item.child_value("link");
            ri.date = item.child_value("pubDate");
            items.push_back(std::move(ri));
            if (max_items > 0 && items.size() >= max_items) break;
        }
    } else {
        // Try Atom 1.0
        pugi::xml_node feed = doc.child("feed");
        if (feed) {
            for (pugi::xml_node entry : feed.children("entry")) {
                RssItem ri;
                ri.title = entry.child_value("title");
                pugi::xml_node author_node = entry.child("author");
                if (author_node) ri.author = author_node.child_value("name");
                
                pugi::xml_node content = entry.child("content");
                if (content) {
                    ri.content_html = content.text().get();
                }
                if (ri.content_html.empty()) {
                    ri.content_html = entry.child_value("summary");
                }
                
                ri.link = entry.child("link").attribute("href").value();
                ri.date = entry.child_value("updated");
                if (ri.date.empty()) ri.date = entry.child_value("published");
                items.push_back(std::move(ri));
                if (max_items > 0 && items.size() >= max_items) break;
            }
        }
    }
    return items;
}

} // namespace endpaper

#include "rss_fetcher.moc"
