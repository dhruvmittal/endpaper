#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QImage>
#include <QBuffer>
#include <iostream>
#include <string>
#include <regex>

#include "html_extractor.h"

using namespace endpaper;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    
    HtmlExtractor extractor;
    std::string url = "https://acoup.blog/2026/03/13/collections-warfare-in-dune-part-ii-the-fremen-jihad/";
    
    std::cout << "1. Fetching Article HTML via HtmlExtractor...\n";
    auto fut = extractor.fetch_and_extract(url);
    std::string content = fut.get();
    
    std::cout << "HTML Length: " << content.size() << "\n";
    
    std::cout << "2. Triggering internal regex search structure...\n";
    
    std::regex img_re(R"RE(<img\b[^>]*src=["']([^"']+)["'][^>]*>)RE", std::regex::icase);
    std::string new_content = content;
    
    int internal_images = 0;
    int fetched_properly = 0;
    int failed_fetches = 0;
    
    QNetworkAccessManager qnam;
    
    auto it = std::sregex_iterator(content.begin(), content.end(), img_re);
    for (; it != std::sregex_iterator(); ++it) {
        internal_images++;
        std::string full_tag = (*it)[0].str();
        std::string src = (*it)[1].str();
        bool success = false;
        
        if (src.find("http") == 0) {
            QEventLoop loop;
            QNetworkRequest req((QUrl(QString::fromStdString(src))));
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            QNetworkReply* reply = qnam.get(req);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
            
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                QImage img;
                if (img.loadFromData(data)) {
                    QByteArray jpegData;
                    QBuffer buffer(&jpegData);
                    buffer.open(QIODevice::WriteOnly);
                    if (img.save(&buffer, "JPEG")) {
                        success = true;
                        fetched_properly++;
                    }
                }
            }
            if(!success) {
                failed_fetches++;
                std::cout << "Failed image fetch / parse: " << src << "\n";
            }
            reply->deleteLater();
        }
    }
    
    std::cout << "Total extracted IMG tags: " << internal_images << "\n";
    std::cout << "Successfully parsed to JPEGs: " << fetched_properly << "\n";
    std::cout << "Failed parses points: " << failed_fetches << "\n";
    
    return 0;
}
