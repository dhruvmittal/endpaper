#include "html_extractor.h"

#include <curl/curl.h>
#include <regex>
#include <future>
#include <iostream>

namespace endpaper {

namespace {

size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* mem = static_cast<std::string*>(userp);
    mem->append(static_cast<char*>(contents), realsize);
    return realsize;
}

} // namespace

class HtmlExtractorPrivate {
public:
    HtmlExtractorPrivate() = default;
};

HtmlExtractor::HtmlExtractor() : d(std::make_unique<HtmlExtractorPrivate>()) {}
HtmlExtractor::~HtmlExtractor() = default;

std::future<std::string> HtmlExtractor::fetch_and_extract(const std::string& url) {
    return std::async(std::launch::async, [url, this]() -> std::string {
        CURL* curl = curl_easy_init();
        if (!curl) return "";

        std::string buffer;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Endpaper/0.1 (Article Extractor; libcurl)");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

        CURLcode res = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && response_code == 200) {
            return extract_main_content(buffer);
        }
        
        return "";
    });
}

std::string HtmlExtractor::extract_main_content(const std::string& html_str) {
    // We use std::regex here for simple extraction as Lexbor might be overkill 
    // for this specific heuristic task, but Lexbor could be used if we wanted 
    // more robust DOM-based extraction. For now, sticking to the existing heuristics
    // but using standard library regex to remove Qt dependency.

    // Heuristics for article content
    // 1. <article> tag
    std::regex articleRegex(R"(<article\b[^>]*>([\s\S]*?)</article>)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(html_str, match, articleRegex)) return match[1].str();

    // 2. <main> tag
    std::regex mainRegex(R"(<main\b[^>]*>([\s\S]*?)</main>)", std::regex::icase);
    if (std::regex_search(html_str, match, mainRegex)) return match[1].str();

    // 3. Common content IDs/classes: content, post, article, entry
    std::vector<std::string> patterns = {
        R"(<div\b[^>]*id=['"]content['\"][^>]*>([\s\S]*?)</div>)",
        R"(<div\b[^>]*class=['\"][^'\"]*content[^'\"]*['\"][^>]*>([\s\S]*?)</div>)",
        R"(<div\b[^>]*class=['\"][^'\"]*post[^'\"]*['\"][^>]*>([\s\S]*?)</div>)",
        R"(<div\b[^>]*class=['\"][^'\"]*article[^'\"]*['\"][^>]*>([\s\S]*?)</div>)"
    };

    for (const auto& pattern : patterns) {
        std::regex re(pattern, std::regex::icase);
        if (std::regex_search(html_str, match, re)) return match[1].str();
    }

    // 4. Fallback to <body> content if found, otherwise the whole thing
    std::regex bodyRegex(R"(<body\b[^>]*>([\s\S]*?)</body>)", std::regex::icase);
    if (std::regex_search(html_str, match, bodyRegex)) return match[1].str();

    return html_str;
}

} // namespace endpaper
