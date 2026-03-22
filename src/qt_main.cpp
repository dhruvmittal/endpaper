#include "config.h"
#include "database.h"
#include "akonadi_fetcher.h"
#include "rss_fetcher.h"
#include "miniflux_fetcher.h"
#include "endpaper_bridge.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QDirIterator>
#include <QDebug>

int main(int argc, char* argv[]) {
    // Enable High DPI Support
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QGuiApplication app(argc, argv);
    app.setApplicationName("endpaper-qt");
    app.setOrganizationName("Endpaper");
    app.setOrganizationDomain("endpaper.local");
    app.setApplicationVersion("0.1.0");

    // Use Breeze style by default for Kirigami if not set
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle("org.kde.breeze");
    }

    // Standard business logic initialization
    endpaper::Config config;
    endpaper::Database db(config.db_path());
    db.init();

    endpaper::AkonadiFetcher fetcher;
    endpaper::RssFetcher rss_fetcher;
    endpaper::MinifluxFetcher miniflux_fetcher;
    
    // Set Miniflux credentials if present
    std::string url = config.get("miniflux", "url", "");
    std::string token = config.get("miniflux", "token", "");
    if (!url.empty()) miniflux_fetcher.set_credentials(url, token);

    // QML Engine setup
    QQmlApplicationEngine engine;
    
    // Explicitly add QML_IMPORT_PATHs from the environment
    const QString qmlPathEnv = qEnvironmentVariable("QML_IMPORT_PATH");
    for (const QString& path : qmlPathEnv.split(':', Qt::SkipEmptyParts)) {
        engine.addImportPath(path);
    }
    
    // Instantiate Bridge as singleton (injected into context here as a simple property since we create the instance)
    // Wait, since we marked it as QML_SINGLETON, we could just let QML register it. 
    // But we need to inject dependencies. The best way is to instantiate it, init it, and set it as context property or use qmlRegisterSingletonInstance.
    endpaper::EndpaperBridge bridge;
    bridge.init(&config, &db, &fetcher, &rss_fetcher, &miniflux_fetcher);
    
    engine.rootContext()->setContextProperty("Bridge", &bridge);

    QDirIterator it(":", QDirIterator::Subdirectories);
    qDebug() << "--- Available QRC Paths ---";
    while (it.hasNext()) {
        qDebug() << it.next();
    }
    qDebug() << "---------------------------";

    const QUrl url_main("qrc:/org/kde/endpaper/src/qml/main.qml");
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url_main);

    return app.exec();
}
