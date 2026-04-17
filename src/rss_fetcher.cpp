#include "rss_fetcher.h"

#include <curl/curl.h>
#include <pugixml.hpp>
#include <iostream>
#include <algorithm>
#include <thread>

namespace endpaper {

namespace {

size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* mem = static_cast<std::string*>(userp);
    mem->append(static_cast<char*>(contents), realsize);
    return realsize;
}

struct CurlGlobalInit {
    CurlGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalInit() { curl_global_cleanup(); }
};

void init_curl_if_needed() {
    static CurlGlobalInit init;
}

} // namespace

class RssFetcherPrivate {
public:
    RssFetcherPrivate() {
        init_curl_if_needed();
    }
};

RssFetcher::RssFetcher() : d(std::make_unique<RssFetcherPrivate>()) {}
RssFetcher::~RssFetcher() = default;

std::future<std::vector<RssItem>> RssFetcher::fetch_items(const std::string& url, size_t max_items) {
    return std::async(std::launch::async, [url, max_items]() -> std::vector<RssItem> {
        CURL* curl = curl_easy_init();
        if (!curl) return {};

        std::string buffer;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Endpaper/0.1 (RSS Fetcher; libcurl)");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

        CURLcode res = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && response_code == 200) {
            return RssFetcher::parse_xml(buffer, max_items);
        }
        
        return {};
    });
}

std::vector<RssItem> RssFetcher::parse_xml(const std::string& xml_data, size_t max_items) {
    std::vector<RssItem> items;
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xml_data.c_str());

    if (!result) return items;

    // Try RSS 2.0
    pugi::xml_node channel = doc.child("rss").child("channel");
    if (channel) {
        for (pugi::xml_node item : channel.children("item")) {
            RssItem ri;
            ri.title = item.child_value("title");
            ri.author = item.child_value("author");
            if (ri.author.empty()) ri.author = item.child_value("dc:creator");
            
            ri.content_html = item.child_value("content:encoded");
            if (ri.content_html.empty()) ri.content_html = item.child_value("description");
            
            ri.link = item.child_value("link");
            ri.date = item.child_value("pubDate");
            items.push_back(std::move(ri));
            if (max_items > 0 && items.size() >= max_items) break;
        }
    } else {
        // Try Atom 1.0
        pugi::xml_node feed = doc.child("feed");
        if (feed) {
            for (pugi::xml_node entry : feed.children("entry")) {
                RssItem ri;
                ri.title = entry.child_value("title");
                pugi::xml_node author_node = entry.child("author");
                if (author_node) ri.author = author_node.child_value("name");
                
                pugi::xml_node content = entry.child("content");
                if (content) {
                    ri.content_html = content.text().get();
                }
                if (ri.content_html.empty()) {
                    ri.content_html = entry.child_value("summary");
                }
                
                ri.link = entry.child("link").attribute("href").value();
                ri.date = entry.child_value("updated");
                if (ri.date.empty()) ri.date = entry.child_value("published");
                items.push_back(std::move(ri));
                if (max_items > 0 && items.size() >= max_items) break;
            }
        }
    }
    return items;
}

} // namespace endpaper
