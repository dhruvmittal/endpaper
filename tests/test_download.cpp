#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QImage>
#include <QBuffer>
#include <QUrl>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::string src = "https://i0.wp.com/acoup.blog/wp-content/uploads/2026/03/dune-jihad-2-1.png?resize=1024%2C420&#038;ssl=1";
    
    std::cout << "Fetching: " << src << std::endl;
    QNetworkAccessManager qnam;
    QEventLoop loop;
    QNetworkRequest req((QUrl(QString::fromStdString(src))));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = qnam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    std::cout << "Error code: " << reply->error() << std::endl;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        std::cout << "Received " << data.size() << " bytes.\n";
        QImage img;
        if (img.loadFromData(data)) {
            std::cout << "Image parsed correctly. Size: " << img.width() << "x" << img.height() << std::endl;
        } else {
            std::cout << "Failed to parse image from " << data.size() << " bytes.\n";
        }
    } else {
        std::cout << "Error: " << reply->errorString().toStdString() << "\n";
    }
    return 0;
}
