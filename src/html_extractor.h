#pragma once

#include <string>
#include <future>
#include <memory>

namespace endpaper {

class HtmlExtractorPrivate;

/// Utility to fetch full HTML from a URL and extract the main article content.
class HtmlExtractor {
public:
    HtmlExtractor();
    ~HtmlExtractor();

    /// Fetch and extract content from a URL.
    /// Returns a future that resolves to the extracted HTML fragment.
    [[nodiscard]] std::future<std::string> fetch_and_extract(const std::string& url);

    /// Static helper to extract content from an HTML string.
    /// Uses heuristics (article tags, common class names).
    static std::string extract_main_content(const std::string& html);

private:
    std::unique_ptr<HtmlExtractorPrivate> d;
};

} // namespace endpaper
