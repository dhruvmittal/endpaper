#include "rss_fetcher.h"
#include "http_client.h"

#include <pugixml.hpp>
#include <sstream>
#include <stdexcept>

namespace endpaper {

namespace {

std::string serialize_atom_content(pugi::xml_node content) {
    if (!content) return "";
    std::string direct = content.text().get();
    if (!direct.empty()) return direct;

    // Atom <content type="xhtml"> nests XHTML nodes inside a <div> element
    if (content.first_child()) {
        std::ostringstream oss;
        for (pugi::xml_node ch : content.children()) {
            ch.print(oss, "", pugi::format_raw);
        }
        return oss.str();
    }
    return "";
}

std::vector<RssItem> parse_xml_doc(const pugi::xml_document& doc, size_t max_items) {
    std::vector<RssItem> items;

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

                ri.content_html = serialize_atom_content(entry.child("content"));
                if (ri.content_html.empty()) {
                    ri.content_html = serialize_atom_content(entry.child("summary"));
                }

                // Prefer <link rel="alternate"> or <link> without rel over <link rel="self">
                std::string fallback_href;
                for (pugi::xml_node ln : entry.children("link")) {
                    std::string href = ln.attribute("href").value();
                    if (href.empty()) continue;
                    if (fallback_href.empty()) fallback_href = href;
                    std::string rel = ln.attribute("rel").value();
                    if (rel.empty() || rel == "alternate") {
                        ri.link = href;
                        break;
                    }
                }
                if (ri.link.empty()) ri.link = fallback_href;

                ri.date = entry.child_value("updated");
                if (ri.date.empty()) ri.date = entry.child_value("published");
                items.push_back(std::move(ri));
                if (max_items > 0 && items.size() >= max_items) break;
            }
        }
    }
    return items;
}

} // namespace

std::future<std::vector<RssItem>> RssFetcher::fetch_items(const std::string& url,
                                                          size_t max_items,
                                                          const std::atomic<bool>* cancel_flag) {
    return std::async(std::launch::async, [url, max_items, cancel_flag]() -> std::vector<RssItem> {
        HttpOptions opts;
        opts.user_agent = "Endpaper/0.1 (RSS Fetcher; libcurl)";
        opts.cancel_flag = cancel_flag;
        HttpResponse resp = http_get(url, opts);
        if (resp.status_code != 200) {
            throw std::runtime_error("RSS HTTP " + std::to_string(resp.status_code) + " for " + url);
        }

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_string(resp.body.c_str());
        if (!result) {
            throw std::runtime_error(std::string("RSS XML parse error: ") + result.description());
        }
        if (!doc.child("rss").child("channel") && !doc.child("feed")) {
            throw std::runtime_error("Unrecognized RSS/Atom feed format from " + url);
        }
        return parse_xml_doc(doc, max_items);
    });
}

std::vector<RssItem> RssFetcher::parse_xml(const std::string& xml_data, size_t max_items) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xml_data.c_str());
    if (!result) return {};
    return parse_xml_doc(doc, max_items);
}

} // namespace endpaper
