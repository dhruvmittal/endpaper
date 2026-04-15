#include "html_sanitizer.h"

#include <lexbor/html/parser.h>
#include <lexbor/html/serialize.h>
#include <lexbor/html/interface.h>
#include <lexbor/html/node.h>
#include <lexbor/core/str.h>

#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <regex>

namespace endpaper {

namespace {

bool is_safe_protocol(const std::string& url) {
    if (url.empty()) return true;
    
    std::string lower_url = url;
    std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
    
    if (lower_url.find(':') == std::string::npos) return true;
    
    return (lower_url.find("http://") == 0 || 
            lower_url.find("https://") == 0 || 
            lower_url.find("mailto:") == 0);
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
                tag_id == LXB_TAG_FOOTER || tag_id == LXB_TAG_HEADER || 
                tag_id == LXB_TAG_NAV || tag_id == LXB_TAG_ASIDE) {
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
                lxb_dom_node_remove(child);
                lxb_dom_node_destroy(child);
                child = first_inner ? first_inner : next;
                continue;
            } else {
                lxb_dom_attr_t* attr = lxb_dom_element_first_attribute(element);
                std::vector<lxb_dom_attr_t*> to_remove;
                
                while (attr != nullptr) {
                    lxb_dom_attr_t* next_attr = lxb_dom_element_next_attribute(attr);
                    size_t name_len;
                    const lxb_char_t* name = lxb_dom_attr_local_name(attr, &name_len);
                    std::string attr_name(reinterpret_cast<const char*>(name), name_len);
                    std::transform(attr_name.begin(), attr_name.end(), attr_name.begin(), ::tolower);

                    bool safe = false;
                    if (tag_id == LXB_TAG_A && attr_name == "href") {
                        size_t val_len;
                        const lxb_char_t* val = lxb_dom_attr_value(attr, &val_len);
                        if (val && is_safe_protocol(std::string(reinterpret_cast<const char*>(val), val_len))) {
                            safe = true;
                        }
                    } else if (tag_id == LXB_TAG_IMG && attr_name == "src") {
                        safe = true;
                    }

                    if (attr_name.find("on") == 0 || attr_name == "style" || attr_name == "class" || attr_name == "id" || !safe) {
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

lxb_status_t serialization_callback(const lxb_char_t *data, size_t len, void *ctx) {
    std::string* result = static_cast<std::string*>(ctx);
    result->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

std::string process_html(const std::string& html, bool keep_images, bool aggressive) {
    if (html.empty()) return "";

    lxb_html_document_t* document = lxb_html_document_create();
    if (document == nullptr) return html;

    lxb_status_t status = lxb_html_document_parse(document, 
                                                 reinterpret_cast<const lxb_char_t*>(html.c_str()), 
                                                 html.length());
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(document);
        return html;
    }

    lxb_dom_node_t* body_node = lxb_dom_interface_node(document->body);
    clean_node(body_node, keep_images, aggressive);

    std::string result;
    lxb_html_serialize_deep_cb(body_node, serialization_callback, &result);

    lxb_html_document_destroy(document);
    
    size_t start = result.find("<body>");
    if (start != std::string::npos) {
        start += 6;
        size_t end = result.rfind("</body>");
        if (end != std::string::npos) {
            result = result.substr(start, end - start);
        } else {
            result = result.substr(start);
        }
    } else if ((start = result.find("<BODY>")) != std::string::npos) {
        start += 6;
        size_t end = result.rfind("</BODY>");
        if (end != std::string::npos) {
            result = result.substr(start, end - start);
        } else {
            result = result.substr(start);
        }
    }

    // Post-processing for XHTML compliance (EPUB)
    // Self-close all HTML void elements.  Lexbor serializes them as <tag> (no
    // slash), which is valid HTML5 but illegal in XHTML/XML used by EPUB readers.
    // std::regex (ECMAScript) does not support lookbehinds, so we use a manual
    // loop and skip tags that are already self-closed (end with "/>").
    {
        std::regex void_re(
            "<(br|img|hr|input|area|base|col|embed|param|source|track|wbr)(\\s[^>]*)?>",
            std::regex::icase);
        std::string fixed;
        fixed.reserve(result.size());
        auto it = result.cbegin();
        std::smatch m;
        while (std::regex_search(it, result.cend(), m, void_re)) {
            fixed.append(m.prefix().first, m.prefix().second);
            const std::string tag = m[0].str();
            // Already self-closed (e.g. <img/> or <img ... />) — keep as-is.
            if (tag.size() >= 2 && tag[tag.size() - 2] == '/') {
                fixed += tag;
            } else {
                // Insert '/' before the closing '>'.
                fixed += tag.substr(0, tag.size() - 1) + "/>";
            }
            it = m.suffix().first;
        }
        fixed.append(it, result.cend());
        result = std::move(fixed);
    }

    // Replace the HTML-only named entity &nbsp; with its numeric XML equivalent.
    // XML only has &amp; &lt; &gt; &quot; &apos; built-in; &nbsp; is HTML-specific
    // and will cause "undefined entity" errors in strict XML parsers used by e-readers.
    result = std::regex_replace(result, std::regex("&nbsp;"), "&#160;");

    if (aggressive) {
        // Collapse runs of 3+ <br/> tags to at most 2
        result = std::regex_replace(result, std::regex("(<br/>\\s*){3,}"), "<br/><br/>");
    }

    return result;
}

} // namespace

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
