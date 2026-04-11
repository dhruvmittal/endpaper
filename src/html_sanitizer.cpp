#include "html_sanitizer.h"

#include <regex>
#include <sstream>
#include <cstdint>

namespace endpaper {

std::string sanitize_html(const std::string& html, bool keep_images) {
    std::string result = html;

    // 1. Remove <style>...</style> blocks (case-insensitive, including content)
    {
        std::regex style_block(R"(<style[^>]*>[\s\S]*?</style\s*>)",
                               std::regex::icase);
        result = std::regex_replace(result, style_block, "");
    }

    // 2. Remove all <img ...> tags (self-closing or not)
    if (!keep_images) {
        std::regex img_tag(R"(<img\b[^>]*\/?>)", std::regex::icase);
        result = std::regex_replace(result, img_tag, "");
    }

    // 3. Remove all <picture>...</picture> blocks
    if (!keep_images) {
        std::regex picture_block(R"(<picture[^>]*>[\s\S]*?</picture\s*>)",
                                 std::regex::icase);
        result = std::regex_replace(result, picture_block, "");
    }

    // 4. Remove <script>...</script> blocks
    {
        std::regex script_block(R"(<script[^>]*>[\s\S]*?</script\s*>)",
                                std::regex::icase);
        result = std::regex_replace(result, script_block, "");
    }

    // 5. Remove style="..." attributes from all tags
    {
        std::regex style_attr(R"(\s+style\s*=\s*"[^"]*")",
                              std::regex::icase);
        result = std::regex_replace(result, style_attr, "");
    }
    // Also handle style='...' (single quotes)
    {
        std::regex style_attr_sq(R"(\s+style\s*=\s*'[^']*')",
                                 std::regex::icase);
        result = std::regex_replace(result, style_attr_sq, "");
    }

    // 6. Remove class="..." attributes (web formatting cleanup)
    {
        std::regex class_attr(R"(\s+class\s*=\s*"[^"]*")",
                              std::regex::icase);
        result = std::regex_replace(result, class_attr, "");
    }
    {
        std::regex class_attr_sq(R"(\s+class\s*=\s*'[^']*')",
                                 std::regex::icase);
        result = std::regex_replace(result, class_attr_sq, "");
    }

    // 7. Remove <!--...--> comments
    {
        std::regex comment(R"(<!--[\s\S]*?-->)");
        result = std::regex_replace(result, comment, "");
    }

    // 8. Remove title="..." and data-*="..." attributes
    {
        std::regex problematic_attr(R"(\s+(title|data-[a-z0-9\-]+)\s*=\s*"[^"]*")",
                                     std::regex::icase);
        result = std::regex_replace(result, problematic_attr, "");
    }
    {
        std::regex problematic_attr_sq(R"(\s+(title|data-[a-z0-9\-]+)\s*=\s*'[^']*')",
                                        std::regex::icase);
        result = std::regex_replace(result, problematic_attr_sq, "");
    }
    // And also ensure no title/data attributes remain if they were unquoted
    {
        std::regex unquoted_attr(R"(\s+(title|data-[a-z0-9\-]+)\s*=[^\s/>]+)",
                                 std::regex::icase);
        result = std::regex_replace(result, unquoted_attr, "");
    }

    // 9. Fix self-closing tags (XHTML compliance)
    {
        // Convert <br> to <br /> (case-insensitive, avoiding already closed ones)
        std::regex br_tag(R"(<br\s*(?!/)>)", std::regex::icase);
        result = std::regex_replace(result, br_tag, "<br />");
        
        // Convert <hr> to <hr />
        std::regex hr_tag(R"(<hr\s*(?!/)>)", std::regex::icase);
        result = std::regex_replace(result, hr_tag, "<hr />");
    }

    // 10. Normalize HTML5 elements (figure, figcaption) to <div> for old readers
    {
        std::regex figure_open(R"(<figure\b[^>]*>)", std::regex::icase);
        result = std::regex_replace(result, figure_open, "<div class=\"figure\">");
        
        std::regex figure_close(R"(</figure\s*?>)", std::regex::icase);
        result = std::regex_replace(result, figure_close, "</div>");
        
        std::regex figcaption_open(R"(<figcaption\b[^>]*>)", std::regex::icase);
        result = std::regex_replace(result, figcaption_open, "<div class=\"figcaption\">");
        
        std::regex figcaption_close(R"(</figcaption\s*?>)", std::regex::icase);
        result = std::regex_replace(result, figcaption_close, "</div>");
    }

    // 11. Convert non-ASCII UTF-8 characters to numeric entities
    {
        std::string escaped;
        escaped.reserve(result.size());
        for (size_t i = 0; i < result.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(result[i]);
            if (c <= 127) {
                escaped += static_cast<char>(c);
            } else {
                int len = 0;
                std::uint32_t cp = 0;
                if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
                else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
                else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
                
                if (len > 1 && i + len <= result.size()) {
                    bool valid = true;
                    for (int j = 1; j < len; ++j) {
                        unsigned char next = static_cast<unsigned char>(result[i + j]);
                        if ((next & 0xC0) != 0x80) { valid = false; break; }
                        cp = (cp << 6) | (next & 0x3F);
                    }
                    if (valid) {
                        escaped += "&#" + std::to_string(cp) + ";";
                        i += len - 1;
                    } else {
                        escaped += static_cast<char>(c);
                    }
                } else {
                    escaped += static_cast<char>(c);
                }
            }
        }
        result = std::move(escaped);
    }

    // 12. Remove empty h1-h6 and p tags
    {
        std::regex empty_tags(R"(<(h[1-6]|p)\b[^>]*>\s*</\1>)", std::regex::icase);
        result = std::regex_replace(result, empty_tags, "");
    }

    // 13. Collapse multiple blank lines into one
    {
        std::regex multi_blank(R"(\n{3,})");
        result = std::regex_replace(result, multi_blank, "\n\n");
    }

    return result;
}

std::string simplify_html(const std::string& html, bool keep_images) {
    // 1. Start with normal sanitization (removes images, styles, scripts)
    std::string result = sanitize_html(html, keep_images);

    // 2. Remove common newsletter junk blocks (Unsubscribe, Manage, etc.)
    // We target blocks (div/table/section) that contain these keywords nearby.
    {
        // Remove blocks that look like footers
        std::regex junk_patterns(R"(Unsubscribe|Manage your email settings|Manage your subscription|Powered by [a-zA-Z]+|View in browser|Open in app|Sent from my|© [12][0-9]{3})",
                                std::regex::icase);
        
        // This is a heuristic: if we find a junk pattern, try to remove the surrounding block
        // Since we don't have a DOM, we just remove the lines containing them if they are short,
        // or just the words themselves for now to be safe.
        // Better: Remove elements that are likely footers.
        std::regex footer_tags(R"(<(footer|header|nav|aside)[^>]*>[\s\S]*?</\1>)", std::regex::icase);
        result = std::regex_replace(result, footer_tags, "");
    }

    // 3. Strip layout-only tags but KEEP their content
    // tags to remove: div, span, table, tr, td, th, tbody, thead, tfoot, article, section, center, font
    {
        std::regex layout_tags(R"(<\/?(div|span|table|tr|td|th|tbody|thead|tfoot|article|section|center|font)\b[^>]*>)",
                               std::regex::icase);
        result = std::regex_replace(result, layout_tags, "");
    }
    
    // 3.5 Strip web forms, buttons, inputs, textareas, selects, and iframes completely
    {
        std::regex form_block(R"(<form\b[^>]*>[\s\S]*?</form\s*>)", std::regex::icase);
        result = std::regex_replace(result, form_block, "");

        std::regex button_block(R"(<button\b[^>]*>[\s\S]*?</button\s*>)", std::regex::icase);
        result = std::regex_replace(result, button_block, "");
        
        std::regex input_tag(R"(<input\b[^>]*\/?>)", std::regex::icase);
        result = std::regex_replace(result, input_tag, "");

        std::regex textarea_block(R"(<textarea\b[^>]*>[\s\S]*?</textarea\s*>)", std::regex::icase);
        result = std::regex_replace(result, textarea_block, "");

        std::regex select_block(R"(<select\b[^>]*>[\s\S]*?</select\s*>)", std::regex::icase);
        result = std::regex_replace(result, select_block, "");
        
        std::regex iframe_block(R"(<iframe\b[^>]*>[\s\S]*?</iframe\s*>)", std::regex::icase);
        result = std::regex_replace(result, iframe_block, "");
    }

    // 4. Strip ALL remaining attributes from text structure tags, BUT preserve <a> and <img>
    {
        // Target specifically text and block elements for attribute stripping,
        // avoiding 'a' and 'img' which inherently require attributes (href / src)
        std::regex attr_stripper(R"(<(p|b|i|em|strong|h[1-6]|blockquote|ul|ol|li)\b[^>]*?(/?)>)", std::regex::icase);
        result = std::regex_replace(result, attr_stripper, "<$1$2>");
    }

    // 5. Final cleanup
    {
        std::regex multi_br(R"((<br\s*\/?>\s*){3,})", std::regex::icase);
        result = std::regex_replace(result, multi_br, "<br /><br />");
        
        std::regex multi_blank(R"(\n{3,})");
        result = std::regex_replace(result, multi_blank, "\n\n");
    }

    return result;
}

std::string plain_text_to_html(const std::string& text) {
    std::ostringstream html;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            html << "<br />\n";
        } else {
            // Basic HTML escaping
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
