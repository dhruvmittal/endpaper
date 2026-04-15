#include "miniflux_fetcher.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>
#include <thread>
#include <future>
#include <sstream>

using json = nlohmann::json;

namespace endpaper {

namespace {

size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* mem = static_cast<std::string*>(userp);
    mem->append(static_cast<char*>(contents), realsize);
    return realsize;
}

} // namespace

class MinifluxFetcherPrivate {
public:
    std::string server_url;
    std::string token;

    MinifluxFetcherPrivate() = default;
};

MinifluxFetcher::MinifluxFetcher() : d(std::make_unique<MinifluxFetcherPrivate>()) {}
MinifluxFetcher::~MinifluxFetcher() = default;

void MinifluxFetcher::set_credentials(const std::string& server_url, const std::string& token) {
    d->server_url = server_url;
    if (!d->server_url.empty() && d->server_url.back() == '/') {
        d->server_url.pop_back();
    }
    d->token = token;
}

std::future<std::vector<MinifluxCategory>> MinifluxFetcher::fetch_categories() {
    std::string url = d->server_url + "/v1/categories?counts=true";
    std::string token = d->token;

    return std::async(std::launch::async, [url, token]() -> std::vector<MinifluxCategory> {
        CURL* curl = curl_easy_init();
        if (!curl) return {};

        std::string buffer;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("X-Auth-Token: " + token).c_str());
        headers = curl_slist_append(headers, "User-Agent: Endpaper/0.1 (Miniflux Fetcher; libcurl)");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

        CURLcode res = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && response_code == 200) {
            try {
                auto j = json::parse(buffer);
                std::vector<MinifluxCategory> categories;
                for (const auto& item : j) {
                    MinifluxCategory cat;
                    cat.id = item["id"].get<int64_t>();
                    cat.title = item["title"].get<std::string>();
                    cat.unread_count = item["total_unread"].get<int>();
                    categories.push_back(std::move(cat));
                }
                return categories;
            } catch (...) {
                return {};
            }
        }
        return {};
    });
}

std::future<std::vector<MinifluxEntry>> MinifluxFetcher::fetch_entries(const FetchParams& params) {
    std::string base_url = d->server_url + "/v1/entries";
    std::string token = d->token;

    return std::async(std::launch::async, [base_url, token, params]() -> std::vector<MinifluxEntry> {
        CURL* curl = curl_easy_init();
        if (!curl) return {};

        std::stringstream url;
        url << base_url << "?direction=desc&limit=100";
        if (params.category_id != -1) url << "&category_id=" << params.category_id;
        if (!params.status.empty()) url << "&status=" << params.status;
        if (params.starred) url << "&starred=true";
        if (!params.search.empty()) {
            char* escaped = curl_easy_escape(curl, params.search.c_str(), params.search.length());
            url << "&search=" << escaped;
            curl_free(escaped);
        }

        std::string buffer;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("X-Auth-Token: " + token).c_str());
        headers = curl_slist_append(headers, "User-Agent: Endpaper/0.1");

        curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

        CURLcode res = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && response_code == 200) {
            try {
                auto j = json::parse(buffer);
                std::vector<MinifluxEntry> entries;
                for (const auto& item : j["entries"]) {
                    MinifluxEntry ent;
                    ent.id = item["id"].get<int64_t>();
                    ent.title = item["title"].get<std::string>();
                    ent.author = item.value("author", "");
                    ent.content = item.value("content", "");
                    ent.url = item.value("url", "");
                    ent.status = item.value("status", "");
                    ent.starred = item.value("starred", false);
                    ent.published_at = item.value("published_at", "");
                    
                    if (ent.author.empty() && item.contains("feed")) {
                        ent.author = item["feed"].value("title", "");
                    }
                    
                    entries.push_back(std::move(ent));
                }
                return entries;
            } catch (...) {
                return {};
            }
        }
        return {};
    });
}

std::future<std::string> MinifluxFetcher::fetch_original_content(int64_t entry_id) {
    std::string url = d->server_url + "/v1/entries/" + std::to_string(entry_id) + "/fetch-content";
    std::string token = d->token;

    return std::async(std::launch::async, [url, token]() -> std::string {
        CURL* curl = curl_easy_init();
        if (!curl) return "";

        std::string buffer;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("X-Auth-Token: " + token).c_str());
        headers = curl_slist_append(headers, "User-Agent: Endpaper/0.1");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

        CURLcode res = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && response_code == 200) {
            try {
                auto j = json::parse(buffer);
                return j.value("content", "");
            } catch (...) {
                return "";
            }
        }
        return "";
    });
}

} // namespace endpaper
