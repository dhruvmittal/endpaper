#include "miniflux_fetcher.h"
#include "http_client.h"

#include <nlohmann/json.hpp>
#include <future>
#include <mutex>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace endpaper {

namespace {

std::string short_snippet(std::string s) {
    for (char& c : s) {
        if (c == '\r' || c == '\n') c = ' ';
    }
    if (s.size() > 160) s.resize(160);
    return s;
}

std::string json_str(const json& obj, const char* key) {
    auto it = obj.find(key);
    if (it != obj.end() && it->is_string()) return it->get<std::string>();
    return "";
}

int json_int(const json& obj, const char* key, int def = 0) {
    auto it = obj.find(key);
    if (it != obj.end() && it->is_number_integer()) return it->get<int>();
    return def;
}

int64_t json_int64(const json& obj, const char* key, int64_t def = 0) {
    auto it = obj.find(key);
    if (it != obj.end() && it->is_number_integer()) return it->get<int64_t>();
    return def;
}

bool json_bool(const json& obj, const char* key, bool def = false) {
    auto it = obj.find(key);
    if (it != obj.end() && it->is_boolean()) return it->get<bool>();
    return def;
}

std::string miniflux_get(const std::string& url,
                         const std::string& token,
                         const std::atomic<bool>* cancel_flag = nullptr) {
    HttpOptions opts;
    opts.headers = {"X-Auth-Token: " + token};
    opts.user_agent = "Endpaper/0.1 (Miniflux Fetcher; libcurl)";
    opts.cancel_flag = cancel_flag;

    HttpResponse resp = http_get(url, opts);
    if (resp.status_code != 200) {
        std::string msg = "Miniflux HTTP " + std::to_string(resp.status_code);
        std::string snip = short_snippet(resp.body);
        if (!snip.empty()) msg += ": " + snip;
        throw std::runtime_error(msg);
    }
    return resp.body;
}

} // namespace

class MinifluxFetcherPrivate {
public:
    std::mutex mu;
    std::string server_url;
    std::string token;

    std::pair<std::string, std::string> credentials() {
        std::lock_guard<std::mutex> lock(mu);
        return {server_url, token};
    }
};

MinifluxFetcher::MinifluxFetcher() : d(std::make_unique<MinifluxFetcherPrivate>()) {}
MinifluxFetcher::~MinifluxFetcher() = default;

void MinifluxFetcher::set_credentials(const std::string& server_url, const std::string& token) {
    std::lock_guard<std::mutex> lock(d->mu);
    d->server_url = server_url;
    if (!d->server_url.empty() && d->server_url.back() == '/') {
        d->server_url.pop_back();
    }
    d->token = token;
}

std::future<std::vector<MinifluxCategory>> MinifluxFetcher::fetch_categories(
    const std::atomic<bool>* cancel_flag) {
    auto [server_url, token] = d->credentials();
    std::string url = server_url + "/v1/categories?counts=true";

    return std::async(std::launch::async, [url, token, cancel_flag]() -> std::vector<MinifluxCategory> {
        std::string buffer = miniflux_get(url, token, cancel_flag);
        try {
            auto j = json::parse(buffer);
            if (!j.is_array()) {
                throw std::runtime_error("Unexpected Miniflux categories response format");
            }
            std::vector<MinifluxCategory> categories;
            for (const auto& item : j) {
                if (!item.is_object()) continue;
                MinifluxCategory cat;
                cat.id = json_int64(item, "id");
                cat.title = json_str(item, "title");
                cat.unread_count = json_int(item, "total_unread");
                categories.push_back(std::move(cat));
            }
            return categories;
        } catch (const json::exception& e) {
            throw std::runtime_error(std::string("Miniflux JSON error: ") + e.what());
        }
    });
}

std::future<std::vector<MinifluxEntry>> MinifluxFetcher::fetch_entries(
    const FetchParams& params,
    const std::atomic<bool>* cancel_flag) {
    auto [server_url, token] = d->credentials();
    std::string base_url = server_url + "/v1/entries";

    return std::async(std::launch::async, [base_url, token, params, cancel_flag]() -> std::vector<MinifluxEntry> {
        std::stringstream url;
        url << base_url << "?direction=desc&limit=100";
        if (params.category_id != -1) url << "&category_id=" << params.category_id;
        if (!params.status.empty()) url << "&status=" << params.status;
        if (params.starred) url << "&starred=true";
        if (!params.search.empty()) url << "&search=" << url_escape(params.search);

        std::string buffer = miniflux_get(url.str(), token, cancel_flag);
        try {
            auto j = json::parse(buffer);
            if (!j.contains("entries") || !j["entries"].is_array()) {
                throw std::runtime_error("Unexpected Miniflux entries response format");
            }
            std::vector<MinifluxEntry> entries;
            for (const auto& item : j["entries"]) {
                if (!item.is_object()) continue;
                MinifluxEntry ent;
                ent.id = json_int64(item, "id");
                ent.title = json_str(item, "title");
                ent.author = json_str(item, "author");
                ent.content = json_str(item, "content");
                ent.url = json_str(item, "url");
                ent.status = json_str(item, "status");
                ent.starred = json_bool(item, "starred");
                ent.published_at = json_str(item, "published_at");

                if (ent.author.empty()) {
                    auto fit = item.find("feed");
                    if (fit != item.end() && fit->is_object()) {
                        ent.author = json_str(*fit, "title");
                    }
                }

                entries.push_back(std::move(ent));
            }
            return entries;
        } catch (const json::exception& e) {
            throw std::runtime_error(std::string("Miniflux JSON error: ") + e.what());
        }
    });
}

std::future<std::string> MinifluxFetcher::fetch_original_content(
    int64_t entry_id,
    const std::atomic<bool>* cancel_flag) {
    auto [server_url, token] = d->credentials();
    std::string url = server_url + "/v1/entries/" + std::to_string(entry_id) + "/fetch-content";

    return std::async(std::launch::async, [url, token, cancel_flag]() -> std::string {
        std::string buffer = miniflux_get(url, token, cancel_flag);
        try {
            auto j = json::parse(buffer);
            return json_str(j, "content");
        } catch (const json::exception& e) {
            throw std::runtime_error(std::string("Miniflux JSON error: ") + e.what());
        }
    });
}

} // namespace endpaper
