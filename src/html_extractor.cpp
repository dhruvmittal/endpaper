#include "html_extractor.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QRegularExpression>
#include <QDebug>
#include <thread>

namespace endpaper {

class HtmlExtractorPrivate : public QObject {
    Q_OBJECT
public:
    HtmlExtractorPrivate() = default;
};

HtmlExtractor::HtmlExtractor() : d(std::make_unique<HtmlExtractorPrivate>()) {}
HtmlExtractor::~HtmlExtractor() = default;

std::future<std::string> HtmlExtractor::fetch_and_extract(const std::string& url) {
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    std::thread([promise, url]() {
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
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            request.setHeader(QNetworkRequest::UserAgentHeader, "Endpaper/0.1 (Article Extractor)");

            QNetworkReply* reply = manager.get(request);
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(30000); // 30s timeout

            loop.exec();

            if (reply->error() == QNetworkReply::NoError && timer.isActive()) {
                QString html = QString::fromUtf8(reply->readAll());
                std::string extracted = extract_main_content(html.toStdString());
                promise->set_value(extracted);
            } else {
                promise->set_value(""); // Fallback to empty if error
            }

            reply->deleteLater();
            if (created_app) delete app;
        } catch (...) {
            try { promise->set_value(""); } catch (...) {}
        }
    }).detach();

    return future;
}

std::string HtmlExtractor::extract_main_content(const std::string& html_str) {
    QString html = QString::fromStdString(html_str);

    // Heuristics for article content
    // 1. <article> tag
    QRegularExpression articleRegex("<article\\b[^>]*>(.*?)</article>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    auto match = articleRegex.match(html);
    if (match.hasMatch()) return match.captured(1).toStdString();

    // 2. <main> tag
    QRegularExpression mainRegex("<main\\b[^>]*>(.*?)</main>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    match = mainRegex.match(html);
    if (match.hasMatch()) return match.captured(1).toStdString();

    // 3. Common content IDs/classes: content, post, article, entry
    QStringList patterns = {
        "<div\\b[^>]*id=['\"]content['\"][^>]*>(.*?)</div>",
        "<div\\b[^>]*class=['\"][^'\"]*content[^'\"]*['\"][^>]*>(.*?)</div>",
        "<div\\b[^>]*class=['\"][^'\"]*post[^'\"]*['\"][^>]*>(.*?)</div>",
        "<div\\b[^>]*class=['\"][^'\"]*article[^'\"]*['\"][^>]*>(.*?)</div>"
    };

    for (const auto& pattern : patterns) {
        QRegularExpression regex(pattern, QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
        match = regex.match(html);
        if (match.hasMatch()) return match.captured(1).toStdString();
    }

    // 4. Fallback to <body> content if found, otherwise the whole thing
    QRegularExpression bodyRegex("<body\\b[^>]*>(.*?)</body>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    match = bodyRegex.match(html);
    if (match.hasMatch()) return match.captured(1).toStdString();

    return html_str;
}

} // namespace endpaper

#include "html_extractor.moc"
