#include "akonadi_fetcher.h"
#include "config.h"
#include "database.h"
#include "endpaper_bridge.h"
#include "html_extractor.h"
#include "http_client.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#ifdef ENDPAPER_HAS_I18NQML
#include <KLocalizedQmlContext>
#else
#include <KLocalizedContext>
#endif
#include <KLocalizedString>

int main(int argc, char* argv[]) {
    endpaper::init_http_client();

    QGuiApplication app(argc, argv);
    app.setApplicationName("endpaper-qt");
    app.setOrganizationName("Endpaper");
    app.setOrganizationDomain("endpaper.local");
    app.setApplicationVersion("0.1.0");

    KLocalizedString::setApplicationDomain("endpaper-qt");

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle("org.kde.breeze");
    }

    endpaper::Config config;
    endpaper::Database db(config.db_path());
    db.init();

    endpaper::AkonadiFetcher fetcher;
    endpaper::RssFetcher rss_fetcher;
    endpaper::MinifluxFetcher miniflux_fetcher;
    endpaper::HtmlExtractor html_extractor;

    std::string url = config.get("miniflux", "url", "");
    std::string token = config.get("miniflux", "token", "");
    if (!url.empty()) miniflux_fetcher.set_credentials(url, token);

    // Declare bridge before engine so engine/QML views tear down before bridge/models
    endpaper::EndpaperBridge bridge;
    bridge.init(&config, &db, &fetcher, &rss_fetcher, &miniflux_fetcher, &html_extractor);

    QQmlApplicationEngine engine;
#ifdef ENDPAPER_HAS_I18NQML
    KLocalization::setupLocalizedContext(&engine);
#else
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
#endif

    for (const char* envName : {"QML_IMPORT_PATH", "QML2_IMPORT_PATH"}) {
        const QString qmlPathEnv = qEnvironmentVariable(envName);
        for (const QString& path : qmlPathEnv.split(':', Qt::SkipEmptyParts)) {
            engine.addImportPath(path);
        }
    }

    qmlRegisterSingletonInstance("org.kde.endpaper", 1, 0, "Bridge", &bridge);

    const QUrl url_main("qrc:/org/kde/endpaper/src/qml/main.qml");
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url_main);

    return app.exec();
}
