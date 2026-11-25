#include "html_extractor.h"
#include "http_client.h"

#include <lexbor/html/interface.h>
#include <lexbor/html/node.h>
#include <lexbor/html/parser.h>
#include <lexbor/html/serialize.h>

#include <algorithm>
#include <cctype>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace endpaper {

namespace {

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string get_attr_lower(lxb_dom_element_t* el, const char* name, size_t name_len) {
    size_t val_len = 0;
    const lxb_char_t* val = lxb_dom_element_get_attribute(
        el, reinterpret_cast<const lxb_char_t*>(name), name_len, &val_len);
    if (!val || val_len == 0) return "";
    return to_lower_ascii(std::string(reinterpret_cast<const char*>(val), val_len));
}

bool token_is_junk(const std::string& tok) {
    if (tok.empty()) return false;
    if (tok.rfind("no-", 0) == 0 || tok.rfind("has-", 0) == 0 || tok.rfind("with-", 0) == 0) {
        return false;
    }
    static const std::vector<const char*> exact = {
        "sidebar", "nav", "navigation", "menu", "comment", "comments", "footer", "header"
    };
    for (const char* w : exact) {
        if (tok == w) return true;
    }
    static const std::vector<const char*> prefixes = {
        "sidebar-", "comment-", "comments-area", "comments-list", "comments-section",
        "site-header", "page-header", "site-footer", "page-footer", "nav-", "menu-"
    };
    for (const char* p : prefixes) {
        if (tok.rfind(p, 0) == 0) return true;
    }
    static const std::vector<const char*> suffixes = {
        "-sidebar", "-comment", "-comments", "-nav", "-menu", "-footer", "-header"
    };
    for (const char* sf : suffixes) {
        size_t slen = std::char_traits<char>::length(sf);
        if (tok.size() >= slen && tok.compare(tok.size() - slen, slen, sf) == 0) {
            return true;
        }
    }
    return false;
}

bool is_junk_attr_value(const std::string& val) {
    if (val.empty()) return false;
    std::istringstream iss(val);
    std::string tok;
    while (iss >> tok) {
        if (token_is_junk(tok)) return true;
    }
    return false;
}

bool is_junk_container(const std::string& id, const std::string& cls) {
    return is_junk_attr_value(id) || is_junk_attr_value(cls);
}

size_t visible_text_length(lxb_dom_node_t* node) {
    if (!node) return 0;
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        size_t len = 0;
        const lxb_char_t* txt = lxb_dom_node_text_content(node, &len);
        if (!txt) return 0;
        size_t non_ws = 0;
        for (size_t i = 0; i < len; ++i) {
            if (!std::isspace(static_cast<unsigned char>(txt[i]))) {
                non_ws++;
            }
        }
        return non_ws;
    }
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (tag == LXB_TAG_SCRIPT || tag == LXB_TAG_STYLE || tag == LXB_TAG_NOSCRIPT) {
            return 0;
        }
    }
    size_t total = 0;
    for (lxb_dom_node_t* ch = lxb_dom_node_first_child(node); ch != nullptr; ch = lxb_dom_node_next(ch)) {
        total += visible_text_length(ch);
    }
    return total;
}

void resolve_urls_in_subtree(lxb_dom_node_t* node, const std::string& base_url) {
    if (!node || base_url.empty()) return;
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t* el = lxb_dom_interface_element(node);
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        const char* attr_name = nullptr;
        size_t attr_len = 0;
        if (tag == LXB_TAG_A) {
            attr_name = "href";
            attr_len = 4;
        } else if (tag == LXB_TAG_IMG || tag == LXB_TAG_SOURCE) {
            attr_name = "src";
            attr_len = 3;
        }
        if (attr_name != nullptr) {
            size_t val_len = 0;
            const lxb_char_t* val = lxb_dom_element_get_attribute(
                el, reinterpret_cast<const lxb_char_t*>(attr_name), attr_len, &val_len);
            if (val && val_len > 0) {
                std::string orig(reinterpret_cast<const char*>(val), val_len);
                std::string resolved = resolve_url(base_url, orig);
                if (resolved != orig) {
                    lxb_dom_element_set_attribute(
                        el, reinterpret_cast<const lxb_char_t*>(attr_name), attr_len,
                        reinterpret_cast<const lxb_char_t*>(resolved.data()), resolved.size());
                }
            }
        }
    }
    for (lxb_dom_node_t* ch = lxb_dom_node_first_child(node); ch != nullptr; ch = lxb_dom_node_next(ch)) {
        resolve_urls_in_subtree(ch, base_url);
    }
}

struct BestCandidate {
    lxb_dom_node_t* node = nullptr;
    size_t text_len = 0;
};

struct Candidates {
    BestCandidate article;
    BestCandidate main_tag;
    BestCandidate div_high;
    BestCandidate div_low;
};

void update_best(BestCandidate& best, lxb_dom_node_t* node, size_t len) {
    if (len > 0 && len > best.text_len) {
        best.node = node;
        best.text_len = len;
    }
}

void collect_candidates(lxb_dom_node_t* node, Candidates& cand) {
    if (!node) return;
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (tag == LXB_TAG_NAV || tag == LXB_TAG_ASIDE ||
            tag == LXB_TAG_FOOTER || tag == LXB_TAG_HEADER) {
            return;
        }

        lxb_dom_element_t* el = lxb_dom_interface_element(node);
        std::string id = get_attr_lower(el, "id", 2);
        std::string cls = get_attr_lower(el, "class", 5);

        if (is_junk_container(id, cls)) {
            return;
        }

        if (tag == LXB_TAG_ARTICLE) {
            update_best(cand.article, node, visible_text_length(node));
        } else if (tag == LXB_TAG_MAIN) {
            update_best(cand.main_tag, node, visible_text_length(node));
        } else if (tag == LXB_TAG_DIV || tag == LXB_TAG_SECTION) {
            bool high = (id == "content" || id == "main-content" || id == "article" || id == "post" ||
                         cls.find("post-content") != std::string::npos ||
                         cls.find("entry-content") != std::string::npos ||
                         cls.find("article-content") != std::string::npos);
            if (high) {
                update_best(cand.div_high, node, visible_text_length(node));
            } else if (cls.find("content") != std::string::npos ||
                       cls.find("post") != std::string::npos ||
                       cls.find("article") != std::string::npos) {
                update_best(cand.div_low, node, visible_text_length(node));
            }
        }
    }
    for (lxb_dom_node_t* ch = lxb_dom_node_first_child(node); ch != nullptr; ch = lxb_dom_node_next(ch)) {
        collect_candidates(ch, cand);
    }
}

lxb_status_t serialization_callback(const lxb_char_t* data, size_t len, void* ctx) {
    auto* result = static_cast<std::string*>(ctx);
    result->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

std::string serialize_inner_html(lxb_dom_node_t* parent) {
    std::string result;
    lxb_html_serialize_deep_cb(parent, serialization_callback, &result);
    return result;
}

} // namespace

std::future<std::string> HtmlExtractor::fetch_and_extract(const std::string& url) {
    return std::async(std::launch::async, [url]() -> std::string {
        HttpResponse resp = http_get(url);
        if (resp.status_code != 200) {
            throw std::runtime_error(
                "Article fetch HTTP " + std::to_string(resp.status_code) + " for " + url);
        }
        return extract_main_content(resp.body, resp.effective_url);
    });
}

std::string HtmlExtractor::extract_main_content(const std::string& html_str,
                                                const std::string& base_url) {
    if (html_str.empty()) return "";

    lxb_html_document_t* document = lxb_html_document_create();
    if (!document) return html_str;

    lxb_status_t status = lxb_html_document_parse(
        document,
        reinterpret_cast<const lxb_char_t*>(html_str.data()),
        html_str.size());
    if (status != LXB_STATUS_OK || !document->body) {
        lxb_html_document_destroy(document);
        return html_str;
    }

    lxb_dom_node_t* body_node = lxb_dom_interface_node(document->body);
    Candidates cand;
    collect_candidates(body_node, cand);

    lxb_dom_node_t* selected = nullptr;
    if (cand.article.node) selected = cand.article.node;
    else if (cand.main_tag.node) selected = cand.main_tag.node;
    else if (cand.div_high.node) selected = cand.div_high.node;
    else if (cand.div_low.node) selected = cand.div_low.node;
    else selected = body_node;

    if (!base_url.empty()) {
        resolve_urls_in_subtree(selected, base_url);
    }

    std::string extracted = serialize_inner_html(selected);
    lxb_html_document_destroy(document);
    return extracted.empty() ? html_str : extracted;
}

} // namespace endpaper
