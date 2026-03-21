#include "akonadi_fetcher.h"
#include "config.h"
#include "database.h"
#include "rss_fetcher.h"
#include "miniflux_fetcher.h"
#include "tui_app.h"

#include <QCoreApplication>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

/// Simple file-based logger for errors.
class ErrorLogger {
public:
    explicit ErrorLogger(const fs::path& log_path)
        : log_file_(log_path, std::ios::app) {
        if (!log_file_) {
            std::cerr << "Warning: Cannot open log file: "
                      << log_path.string() << "\n";
        }
    }

    void log(const std::string& msg) {
        if (log_file_) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            log_file_ << std::ctime(&time);
            // Remove trailing newline from ctime
            log_file_.seekp(-1, std::ios_base::cur);
            log_file_ << " | " << msg << "\n";
            log_file_.flush();
        }
    }

private:
    std::ofstream log_file_;
};

int main(int argc, char* argv[]) {
    // Qt requires QCoreApplication for Akonadi signal/slot mechanism
    QCoreApplication qt_app(argc, argv);
    qt_app.setApplicationName("mail2book");
    qt_app.setApplicationVersion("0.1.0");

    try {
        // 1. Initialize configuration (creates XDG directories)
        mail2book::Config config;

        // 2. Set up error logging
        ErrorLogger logger(config.log_path());

        // 3. Create a default config file if it doesn't exist
        if (!fs::exists(config.config_file())) {
            config.set("epub", "author", "Unknown");
            config.set("epub", "publisher", "Mail2Book");
            config.set("general", "default_folder", "");
            config.save();
        }

        // 4. Initialize database
        mail2book::Database db(config.db_path());
        db.init();

        // 5. Initialize fetchers
        mail2book::AkonadiFetcher fetcher;
        mail2book::RssFetcher rss_fetcher;
        mail2book::MinifluxFetcher miniflux_fetcher;

        if (!fetcher.is_available()) {
            std::cerr << "⚠ Warning: Akonadi server is not running.\n"
                      << "  Email fetching will not work.\n"
                      << "  Start it with: akonadictl start\n"
                      << "  Book management and EPUB building will "
                         "still work.\n\n";
            logger.log("Akonadi server not running at startup");
        }

        // 6. Launch TUI
        mail2book::TuiApp app(config, db, fetcher, rss_fetcher, miniflux_fetcher);
        app.run();

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
