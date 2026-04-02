#pragma once

#include "akonadi_fetcher.h"
#include "config.h"
#include "database.h"
#include "miniflux_fetcher.h"
#include "rss_fetcher.h"
#include "html_extractor.h"

namespace endpaper {

/// Main FTXUI-based TUI application.
/// Provides tabbed screens: Ingest (KMail), Miniflux, RSS, Books, Build, Settings.
class TuiApp {
public:
    TuiApp(Config& config, Database& db, AkonadiFetcher& fetcher, RssFetcher& rss_fetcher, MinifluxFetcher& miniflux_fetcher, HtmlExtractor& html_extractor);

    /// Run the TUI (blocking). Returns when user quits.
    void run();

private:
    Config& config_;
    Database& db_;
    AkonadiFetcher& fetcher_;
    RssFetcher& rss_fetcher_;
    MinifluxFetcher& miniflux_fetcher_;
    HtmlExtractor& html_extractor_;
};

} // namespace endpaper
