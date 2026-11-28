#include "akonadi_fetcher.h"
#include "config.h"
#include "database.h"
#include "html_extractor.h"
#include "http_client.h"
#include "main_window.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QStandardPaths>

int main(int argc, char* argv[]) {
    endpaper::init_http_client();

    QApplication app(argc, argv);
    app.setApplicationName("endpaper-qt");
    app.setOrganizationName("Endpaper");
    app.setOrganizationDomain("endpaper.local");
    app.setApplicationVersion("0.1.0");

    const QString desktopId = QStringLiteral("page.codeberg.gnuslashdhruv.endpaper-qt");
    if (!QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                desktopId + QStringLiteral(".desktop")).isEmpty()) {
        QGuiApplication::setDesktopFileName(desktopId);
    }
    app.setWindowIcon(QIcon::fromTheme(
        QStringLiteral("accessories-ebook-reader"),
        QIcon::fromTheme(QStringLiteral("application-epub+zip"))));

    endpaper::Config config;
    endpaper::Database db(config.db_path());
    db.init();

    endpaper::AkonadiFetcher fetcher;
    endpaper::RssFetcher rss_fetcher;
    endpaper::MinifluxFetcher miniflux_fetcher;
    endpaper::HtmlExtractor html_extractor;

    std::string url = config.get("miniflux", "url", "");
    std::string token = config.get("miniflux", "token", "");
    if (!url.empty()) {
        miniflux_fetcher.set_credentials(url, token);
    }

    endpaper::MainWindow window(config, db, fetcher, rss_fetcher, miniflux_fetcher, html_extractor);
    window.show();

    return app.exec();
}
