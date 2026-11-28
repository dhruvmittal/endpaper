#include "akonadi_fetcher.h"
#include "config.h"
#include "database.h"
#include "rss_fetcher.h"
#include "miniflux_fetcher.h"
#include "html_extractor.h"
#include "http_client.h"
#include "tui_app.h"

#include <QCoreApplication>

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    endpaper::init_http_client();
    // Qt requires QCoreApplication for Akonadi signal/slot mechanism
    QCoreApplication qt_app(argc, argv);
    qt_app.setApplicationName("endpaper");
    qt_app.setApplicationVersion("0.1.0");

    try {
        // 1. Initialize configuration (creates XDG directories)
        endpaper::Config config;

        // 2. Create a default config file if it doesn't exist
        if (!fs::exists(config.config_file())) {
            config.set("epub", "author", "Unknown");
            config.set("epub", "publisher", "Endpaper");
            config.set("general", "default_folder", "");
            config.save();
        }

        // 3. Initialize database
        endpaper::Database db(config.db_path());
        db.init();

        // 4. Initialize fetchers
        endpaper::AkonadiFetcher fetcher;
        endpaper::RssFetcher rss_fetcher;
        endpaper::MinifluxFetcher miniflux_fetcher;
        endpaper::HtmlExtractor html_extractor;

        if (!fetcher.is_available()) {
            std::cerr << "⚠ Warning: Akonadi server is not running.\n"
                      << "  Email fetching will not work.\n"
                      << "  Start it with: akonadictl start\n"
                      << "  Book management and EPUB building will "
                         "still work.\n\n";
        }

        // 5. Launch TUI
        endpaper::TuiApp app(config, db, fetcher, rss_fetcher, miniflux_fetcher, html_extractor);
        app.run();

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
