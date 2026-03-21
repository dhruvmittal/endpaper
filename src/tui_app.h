#pragma once

#include "akonadi_fetcher.h"
#include "config.h"
#include "database.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"

namespace endpaper {

/// Main FTXUI-based TUI application.
/// Provides tabbed screens: Ingest (KMail), Miniflux, RSS, Books, Build, Settings.
class TuiApp {
public:
    TuiApp(Config& config, Database& db, AkonadiFetcher& fetcher, RssFetcher& rss_fetcher, MinifluxFetcher& miniflux_fetcher);

    /// Run the TUI (blocking). Returns when user quits.
    void run();

private:
    Config& config_;
    Database& db_;
    AkonadiFetcher& fetcher_;
    RssFetcher& rss_fetcher_;
    MinifluxFetcher& miniflux_fetcher_;
};

} // namespace endpaper
