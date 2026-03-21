#pragma once

#include <string>
#include <vector>
#include <future>
#include <memory>

namespace mail2book {

struct MinifluxCategory {
    int64_t id;
    std::string title;
    int unread_count = 0;
};

struct MinifluxEntry {
    int64_t id;
    std::string title;
    std::string author;
    std::string content;
    std::string url;
    std::string status;
    bool starred;
    std::string published_at;
};

class MinifluxFetcherPrivate;

/// Fetches categories and entries from a Miniflux server via REST API.
class MinifluxFetcher {
public:
    MinifluxFetcher();
    ~MinifluxFetcher();

    /// Set server URL and API token for subsequent requests.
    void set_credentials(const std::string& server_url, const std::string& token);

    /// Fetch all categories.
    [[nodiscard]] std::future<std::vector<MinifluxCategory>> fetch_categories();

    /// Filtering parameters for fetching entries.
    struct FetchParams {
        int64_t category_id = -1; // -1 for all
        std::string status;       // "read", "unread", or empty for all
        bool starred = false;
        std::string search;
    };

    /// Fetch entries matching the given parameters.
    [[nodiscard]] std::future<std::vector<MinifluxEntry>> fetch_entries(const FetchParams& params);

    /// Fetch the original article content for a specific entry.
    [[nodiscard]] std::future<std::string> fetch_original_content(int64_t entry_id);

private:
    std::unique_ptr<MinifluxFetcherPrivate> d;
};

} // namespace mail2book
