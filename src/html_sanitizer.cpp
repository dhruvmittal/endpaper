#include "html_sanitizer.h"

#include <regex>
#include <sstream>

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

    // 8. Collapse multiple blank lines into one
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
