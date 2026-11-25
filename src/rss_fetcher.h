#pragma once

#include <future>
#include <string>
#include <vector>

namespace endpaper {

struct RssItem {
    std::string title;
    std::string author;
    std::string content_html;
    std::string link;
    std::string date;
};

/// Fetches and parses RSS/Atom feeds using libcurl and pugixml.
class RssFetcher {
public:
    RssFetcher() = default;
    ~RssFetcher() = default;

    /// Fetch items from a feed URL.
    /// Returns a future that resolves to the list of items.
    /// @param max_items Maximum articles to parse (0 = unlimited).
    [[nodiscard]] std::future<std::vector<RssItem>> fetch_items(const std::string& url, size_t max_items = 0);

    /// Static helper for unit testing and internal parsing.
    /// @param max_items Maximum articles to parse (0 = unlimited).
    static std::vector<RssItem> parse_xml(const std::string& xml_data, size_t max_items = 0);
};

} // namespace endpaper
