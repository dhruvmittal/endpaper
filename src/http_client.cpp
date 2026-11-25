#include "http_client.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <stdexcept>

namespace endpaper {

namespace {

std::once_flag g_curl_init_flag;

struct WriteState {
    std::string body;
    size_t max_bytes = 0;
    bool overflow = false;
};

size_t write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* st = static_cast<WriteState*>(userp);
    if (st->max_bytes > 0 && st->body.size() + realsize > st->max_bytes) {
        st->overflow = true;
        return 0;
    }
    st->body.append(static_cast<char*>(contents), realsize);
    return realsize;
}

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

void init_http_client() {
    std::call_once(g_curl_init_flag, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

HttpResponse http_get(const std::string& url, const HttpOptions& opts) {
    init_http_client();

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize libcurl");
    }

    WriteState state;
    state.max_bytes = opts.max_bytes;

    struct curl_slist* header_list = nullptr;
    for (const auto& h : opts.headers) {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, opts.timeout_secs);
    if (opts.max_bytes > 0) {
        curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(opts.max_bytes));
    }
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, opts.user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);

    CURLcode res = curl_easy_perform(curl);
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    char* eff_url = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
    std::string effective = (eff_url && eff_url[0] != '\0') ? std::string(eff_url) : url;

    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    if (state.overflow) {
        throw std::runtime_error("Response exceeded max size limit for " + url);
    }
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("Network error: ") + curl_easy_strerror(res));
    }

    return HttpResponse{status_code, std::move(state.body), std::move(effective)};
}

std::string url_escape(const std::string& s) {
    init_http_client();
    CURL* curl = curl_easy_init();
    if (!curl) return s;
    char* escaped = curl_easy_escape(curl, s.c_str(), static_cast<int>(s.length()));
    std::string out = escaped ? std::string(escaped) : s;
    if (escaped) curl_free(escaped);
    curl_easy_cleanup(curl);
    return out;
}

std::string resolve_url(const std::string& base_url, const std::string& raw_rel) {
    if (base_url.empty() || raw_rel.empty()) return raw_rel;

    size_t start = raw_rel.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return raw_rel;
    size_t end = raw_rel.find_last_not_of(" \t\r\n");
    std::string rel = raw_rel.substr(start, end - start + 1);

    if (rel[0] == '#') return rel;
    std::string lower_rel = to_lower_ascii(rel);
    if (lower_rel.rfind("http://", 0) == 0 || lower_rel.rfind("https://", 0) == 0 ||
        lower_rel.rfind("mailto:", 0) == 0 || lower_rel.rfind("data:", 0) == 0 ||
        lower_rel.rfind("javascript:", 0) == 0) {
        return rel;
    }

    CURLU* u = curl_url();
    if (!u) return rel;

    std::string out = rel;
    if (curl_url_set(u, CURLUPART_URL, base_url.c_str(), 0) == CURLUE_OK &&
        curl_url_set(u, CURLUPART_URL, rel.c_str(), 0) == CURLUE_OK) {
        char* resolved = nullptr;
        if (curl_url_get(u, CURLUPART_URL, &resolved, 0) == CURLUE_OK && resolved) {
            out = resolved;
            curl_free(resolved);
        }
    }
    curl_url_cleanup(u);
    return out;
}

} // namespace endpaper
