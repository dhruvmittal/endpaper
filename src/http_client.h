#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace endpaper {

/// Initialize libcurl once on the main thread before worker threads start.
void init_http_client();

struct HttpOptions {
    std::vector<std::string> headers;
    long timeout_secs = 30;
    size_t max_bytes = 25ULL * 1024ULL * 1024ULL;
    std::string user_agent =
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Endpaper/0.1";
};

struct HttpResponse {
    long status_code = 0;
    std::string body;
    std::string effective_url;
};

/// Perform an HTTP(S) GET request with size cap and protocol restrictions.
/// Throws std::runtime_error on libcurl initialization/network/size failures.
HttpResponse http_get(const std::string& url, const HttpOptions& opts = {});

/// URL-encode a query parameter using libcurl.
[[nodiscard]] std::string url_escape(const std::string& s);

/// Resolve a relative URL against base_url using libcurl's CURLU parser.
[[nodiscard]] std::string resolve_url(const std::string& base_url,
                                      const std::string& raw_rel);

} // namespace endpaper
