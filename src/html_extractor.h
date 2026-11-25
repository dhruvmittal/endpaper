#pragma once

#include <future>
#include <string>

namespace endpaper {

/// Utility to fetch full HTML from a URL and extract the main article content.
class HtmlExtractor {
public:
    HtmlExtractor() = default;
    ~HtmlExtractor() = default;

    /// Fetch and extract content from a URL.
    /// Returns a future that resolves to the extracted HTML fragment.
    [[nodiscard]] std::future<std::string> fetch_and_extract(const std::string& url);

    /// Static helper to extract content from an HTML string.
    /// Uses Lexbor DOM traversal and optionally resolves relative URLs against base_url.
    static std::string extract_main_content(const std::string& html,
                                            const std::string& base_url = "");
};

} // namespace endpaper
