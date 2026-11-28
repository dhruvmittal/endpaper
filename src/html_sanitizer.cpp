#include "html_sanitizer.h"

#include <lexbor/html/parser.h>
#include <lexbor/html/serialize.h>
#include <lexbor/html/interface.h>
#include <lexbor/html/node.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace endpaper {

namespace {

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_safe_protocol(const std::string& url) {
    size_t start = 0;
    while (start < url.size() && static_cast<unsigned char>(url[start]) <= 0x20) {
        ++start;
    }
    if (start >= url.size()) return true;

    std::string lower_url = to_lower_ascii(url.substr(start));
    if (lower_url.find(':') == std::string::npos) return true;

    return (lower_url.rfind("http://", 0) == 0 ||
            lower_url.rfind("https://", 0) == 0 ||
            lower_url.rfind("mailto:", 0) == 0);
}

void clean_node(lxb_dom_node_t* node, bool keep_images, bool aggressive) {
    if (node == nullptr) return;

    lxb_dom_node_t* child = lxb_dom_node_first_child(node);
    while (child != nullptr) {
        lxb_dom_node_t* next = lxb_dom_node_next(child);

        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t* element = lxb_dom_interface_element(child);
            lxb_tag_id_t tag_id = lxb_dom_node_tag_id(child);

            bool remove_entirely = false;
            bool strip_but_keep_children = false;

            if (tag_id == LXB_TAG_SCRIPT || tag_id == LXB_TAG_STYLE ||
                tag_id == LXB_TAG_FORM || tag_id == LXB_TAG_BUTTON ||
                tag_id == LXB_TAG_INPUT || tag_id == LXB_TAG_TEXTAREA ||
                tag_id == LXB_TAG_SELECT || tag_id == LXB_TAG_IFRAME ||
                tag_id == LXB_TAG_META || tag_id == LXB_TAG_LINK ||
                tag_id == LXB_TAG_BASE || tag_id == LXB_TAG_FOOTER ||
                tag_id == LXB_TAG_HEADER || tag_id == LXB_TAG_NAV ||
                tag_id == LXB_TAG_ASIDE) {
                remove_entirely = true;
            } else if (!keep_images && (tag_id == LXB_TAG_IMG || tag_id == LXB_TAG_PICTURE || tag_id == LXB_TAG_SOURCE)) {
                remove_entirely = true;
            }

            if (!remove_entirely && aggressive) {
                if (tag_id == LXB_TAG_DIV || tag_id == LXB_TAG_SPAN ||
                    tag_id == LXB_TAG_TABLE || tag_id == LXB_TAG_TR ||
                    tag_id == LXB_TAG_TD || tag_id == LXB_TAG_TH ||
                    tag_id == LXB_TAG_TBODY || tag_id == LXB_TAG_THEAD ||
                    tag_id == LXB_TAG_TFOOT || tag_id == LXB_TAG_ARTICLE ||
                    tag_id == LXB_TAG_SECTION || tag_id == LXB_TAG_CENTER ||
                    tag_id == LXB_TAG_FONT) {
                    strip_but_keep_children = true;
                }
            }

            if (remove_entirely) {
                lxb_dom_node_remove(child);
                lxb_dom_node_destroy(child);
                child = next;
                continue;
            } else if (strip_but_keep_children) {
                lxb_dom_node_t* inner = lxb_dom_node_first_child(child);
                lxb_dom_node_t* first_inner = inner;
                while (inner != nullptr) {
                    lxb_dom_node_t* inner_next = lxb_dom_node_next(inner);
                    lxb_dom_node_remove(inner);
                    lxb_dom_node_insert_before(child, inner);
                    inner = inner_next;
                }
                if ((tag_id == LXB_TAG_TD || tag_id == LXB_TAG_TH) &&
                    next != nullptr && next->type == LXB_DOM_NODE_TYPE_ELEMENT &&
                    (lxb_dom_node_tag_id(next) == LXB_TAG_TD || lxb_dom_node_tag_id(next) == LXB_TAG_TH)) {
                    lxb_dom_text_t* sp = lxb_dom_document_create_text_node(
                        child->owner_document, reinterpret_cast<const lxb_char_t*>(" "), 1);
                    if (sp != nullptr) {
                        lxb_dom_node_insert_before(child, lxb_dom_interface_node(sp));
                    }
                } else if (tag_id == LXB_TAG_TR && first_inner != nullptr) {
                    lxb_dom_element_t* br = lxb_dom_document_create_element(
                        child->owner_document, reinterpret_cast<const lxb_char_t*>("br"), 2, nullptr);
                    if (br != nullptr) {
                        lxb_dom_node_insert_before(child, lxb_dom_interface_node(br));
                    }
                }
                lxb_dom_node_remove(child);
                lxb_dom_node_destroy(child);
                child = first_inner ? first_inner : next;
                continue;
            } else {
                lxb_dom_attr_t* attr = lxb_dom_element_first_attribute(element);
                std::vector<lxb_dom_attr_t*> to_remove;

                while (attr != nullptr) {
                    lxb_dom_attr_t* next_attr = lxb_dom_element_next_attribute(attr);
                    size_t name_len = 0;
                    const lxb_char_t* name = lxb_dom_attr_local_name(attr, &name_len);
                    std::string attr_name = to_lower_ascii(
                        std::string(reinterpret_cast<const char*>(name), name_len));

                    bool safe = false;
                    if (tag_id == LXB_TAG_A && attr_name == "href") {
                        size_t val_len = 0;
                        const lxb_char_t* val = lxb_dom_attr_value(attr, &val_len);
                        if (val && is_safe_protocol(std::string(reinterpret_cast<const char*>(val), val_len))) {
                            safe = true;
                        }
                    } else if (tag_id == LXB_TAG_IMG && attr_name == "src") {
                        size_t val_len = 0;
                        const lxb_char_t* val = lxb_dom_attr_value(attr, &val_len);
                        if (val && is_safe_protocol(std::string(reinterpret_cast<const char*>(val), val_len))) {
                            safe = true;
                        }
                    }

                    if (attr_name.rfind("on", 0) == 0 || attr_name == "style" || attr_name == "class" || attr_name == "id" || !safe) {
                        if (!aggressive && (attr_name == "width" || attr_name == "height" || attr_name == "alt" || attr_name == "title")) {
                            // keep
                        } else {
                            to_remove.push_back(attr);
                        }
                    }
                    attr = next_attr;
                }

                for (auto* a : to_remove) {
                    lxb_dom_element_attr_remove(element, a);
                }

                clean_node(child, keep_images, aggressive);
            }
        } else if (child->type == LXB_DOM_NODE_TYPE_COMMENT) {
            lxb_dom_node_remove(child);
            lxb_dom_node_destroy(child);
            child = next;
            continue;
        } else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            // keep
        } else {
            lxb_dom_node_remove(child);
            lxb_dom_node_destroy(child);
            child = next;
            continue;
        }

        child = next;
    }
}

lxb_status_t serialization_callback(const lxb_char_t* data, size_t len, void* ctx) {
    auto* result = static_cast<std::string*>(ctx);
    result->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

std::string process_html(const std::string& html, bool keep_images, bool aggressive) {
    if (html.empty()) return "";

    lxb_html_document_t* document = lxb_html_document_create();
    if (document == nullptr) return html;

    lxb_status_t status = lxb_html_document_parse(
        document,
        reinterpret_cast<const lxb_char_t*>(html.data()),
        html.length());
    if (status != LXB_STATUS_OK || document->body == nullptr) {
        lxb_html_document_destroy(document);
        return "";
    }

    lxb_dom_node_t* body_node = lxb_dom_interface_node(document->body);
    clean_node(body_node, keep_images, aggressive);

    std::string result;
    lxb_html_serialize_deep_cb(body_node, serialization_callback, &result);
    lxb_html_document_destroy(document);

    result = ensure_xhtml(result);

    if (aggressive) {
        static const std::regex collapse_br_re(R"((<br\s*/?>\s*){3,})");
        result = std::regex_replace(result, collapse_br_re, "<br/><br/>");
    }

    return result;
}

bool is_void_html_tag(const std::string& lower_tag) {
    static const std::unordered_set<std::string> void_tags = {
        "area", "base", "br", "col", "embed", "hr", "img",
        "input", "link", "meta", "param", "source", "track", "wbr"
    };
    return void_tags.count(lower_tag) > 0;
}

} // namespace

std::string ensure_xhtml(const std::string& html) {
    if (html.empty()) return "";

    std::string out;
    out.reserve(html.size() + 32);

    size_t i = 0;
    const size_t n = html.size();
    while (i < n) {
        if (html[i] != '<') {
            out.push_back(html[i++]);
            continue;
        }

        // Preserve comments and declarations as-is
        if (i + 1 < n && (html[i + 1] == '!' || html[i + 1] == '?' || html[i + 1] == '/')) {
            size_t end = html.find('>', i + 1);
            if (end == std::string::npos) {
                out.append(html, i, n - i);
                break;
            }
            out.append(html, i, end - i + 1);
            i = end + 1;
            continue;
        }

        size_t name_start = i + 1;
        size_t name_end = name_start;
        while (name_end < n && std::isalpha(static_cast<unsigned char>(html[name_end]))) {
            name_end++;
        }
        if (name_end == name_start) {
            out.push_back(html[i++]);
            continue;
        }

        std::string tag_name = to_lower_ascii(html.substr(name_start, name_end - name_start));

        // Scan to closing '>' respecting single and double quoted attribute values
        size_t pos = name_end;
        char in_quote = '\0';
        while (pos < n) {
            char c = html[pos];
            if (in_quote != '\0') {
                if (c == in_quote) in_quote = '\0';
            } else {
                if (c == '"' || c == '\'') in_quote = c;
                else if (c == '>') break;
            }
            pos++;
        }

        if (pos >= n) {
            out.append(html, i, n - i);
            break;
        }

        if (is_void_html_tag(tag_name)) {
            if (pos > i && html[pos - 1] == '/') {
                out.append(html, i, pos - i + 1);
            } else {
                out.append(html, i, pos - i);
                out.append("/>");
            }
        } else {
            out.append(html, i, pos - i + 1);
        }
        i = pos + 1;
    }

    static const std::regex nbsp_re("&nbsp;", std::regex::icase);
    return std::regex_replace(out, nbsp_re, "&#160;");
}

std::string sanitize_html(const std::string& html, bool keep_images) {
    return process_html(html, keep_images, false);
}

std::string simplify_html(const std::string& html, bool keep_images) {
    return process_html(html, keep_images, true);
}

std::string plain_text_to_html(const std::string& text) {
    std::ostringstream html;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            html << "<br />\n";
        } else {
            std::string escaped;
            for (char c : line) {
                switch (c) {
                    case '&':  escaped += "&amp;";  break;
                    case '<':  escaped += "&lt;";   break;
                    case '>':  escaped += "&gt;";   break;
                    case '"':  escaped += "&quot;";  break;
                    default:   escaped += c;         break;
                }
            }
            html << "<p>" << escaped << "</p>\n";
        }
    }

    return html.str();
}

} // namespace endpaper
