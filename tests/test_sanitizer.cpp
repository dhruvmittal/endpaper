/// Endpaper — HTML Sanitizer Tests
/// Verifies that the sanitizer properly strips images, styles, scripts,
/// and preserves structural HTML.

#include "html_sanitizer.h"

#include <cassert>
#include <iostream>
#include <string>

#define TEST(name)                                                 \
    do {                                                           \
        std::cout << "  TEST: " << #name << "... ";                \
        name();                                                    \
        std::cout << "PASS\n";                                     \
    } while (0)

using endpaper::sanitize_html;
using endpaper::simplify_html;
using endpaper::plain_text_to_html;

// ── Tests ─────────────────────────────────────────────────────────────────

void test_removes_img_tags() {
    std::string input = R"(<p>Hello</p><img src="photo.jpg" alt="pic"><p>World</p>)";
    std::string result = sanitize_html(input);
    assert(result.find("<img") == std::string::npos);
    assert(result.find("Hello") != std::string::npos);
    assert(result.find("World") != std::string::npos);
}

void test_removes_self_closing_img() {
    std::string input = R"(<div><img src="x.png" /><span>text</span></div>)";
    std::string result = sanitize_html(input);
    assert(result.find("<img") == std::string::npos);
    assert(result.find("text") != std::string::npos);
}

void test_removes_style_attributes() {
    std::string input = R"(<p style="color: red; font-size: 14px;">Hello</p>)";
    std::string result = sanitize_html(input);
    assert(result.find("style=") == std::string::npos);
    assert(result.find("Hello") != std::string::npos);
    assert(result.find("<p") != std::string::npos);
}

void test_removes_style_attributes_single_quotes() {
    std::string input = R"(<div style='margin: 10px'>content</div>)";
    std::string result = sanitize_html(input);
    assert(result.find("style=") == std::string::npos);
    assert(result.find("content") != std::string::npos);
}

void test_removes_style_blocks() {
    std::string input = R"(
        <style type="text/css">
            body { color: black; }
            .header { font-size: 20px; }
        </style>
        <p>Content here</p>
    )";
    std::string result = sanitize_html(input);
    assert(result.find("<style") == std::string::npos);
    assert(result.find("</style>") == std::string::npos);
    assert(result.find("font-size") == std::string::npos);
    assert(result.find("Content here") != std::string::npos);
}

void test_removes_script_blocks() {
    std::string input = R"(<script>alert('xss');</script><p>Safe</p>)";
    std::string result = sanitize_html(input);
    assert(result.find("<script") == std::string::npos);
    assert(result.find("alert") == std::string::npos);
    assert(result.find("Safe") != std::string::npos);
}

void test_removes_class_attributes() {
    std::string input = R"(<div class="email-body fancy"><p class="lead">text</p></div>)";
    std::string result = sanitize_html(input);
    assert(result.find("class=") == std::string::npos);
    assert(result.find("text") != std::string::npos);
}

void test_removes_html_comments() {
    std::string input = R"(<!-- hidden --><!--[if mso]>stuff<![endif]--><p>visible</p>)";
    std::string result = sanitize_html(input);
    assert(result.find("<!--") == std::string::npos);
    assert(result.find("hidden") == std::string::npos);
    assert(result.find("visible") != std::string::npos);
}

void test_preserves_structural_tags() {
    std::string input = R"(
        <h1>Title</h1>
        <p>Paragraph</p>
        <ul><li>Item 1</li><li>Item 2</li></ul>
        <blockquote>Quote</blockquote>
        <a href="http://example.com">Link</a>
        <table><tr><td>Cell</td></tr></table>
    )";
    std::string result = sanitize_html(input);
    assert(result.find("<h1>") != std::string::npos);
    assert(result.find("<p>") != std::string::npos);
    assert(result.find("<ul>") != std::string::npos);
    assert(result.find("<li>") != std::string::npos);
    assert(result.find("<blockquote>") != std::string::npos);
    assert(result.find("<a ") != std::string::npos);
    assert(result.find("<table>") != std::string::npos);
}

void test_complex_email_html() {
    std::string input = R"(
        <html>
        <head>
            <style>body { font-family: Arial; }</style>
        </head>
        <body>
            <div style="max-width: 600px;" class="email-container">
                <img src="header-logo.png" />
                <h1 style="color: #333;">Important Update</h1>
                <p style="line-height: 1.5;" class="body-text">
                    Dear user, here is your update.
                </p>
                <img src="cid:footer-image.jpg" alt="footer" />
                <!-- tracking pixel -->
                <img src="https://tracking.example.com/pixel.gif" width="1" height="1" />
                <p>Best regards,<br/>The Team</p>
            </div>
            <script>trackOpen();</script>
        </body>
        </html>
    )";
    std::string result = sanitize_html(input);

    // No images
    assert(result.find("<img") == std::string::npos);
    // No styles
    assert(result.find("style=") == std::string::npos);
    assert(result.find("<style") == std::string::npos);
    // No scripts
    assert(result.find("<script") == std::string::npos);
    // No classes
    assert(result.find("class=") == std::string::npos);
    // No comments
    assert(result.find("<!--") == std::string::npos);
    // Content preserved
    assert(result.find("Important Update") != std::string::npos);
    assert(result.find("Dear user") != std::string::npos);
    assert(result.find("Best regards") != std::string::npos);
}

void test_plain_text_to_html() {
    std::string input = "Line 1\nLine 2\n\nLine 4";
    std::string result = plain_text_to_html(input);
    assert(result.find("<p>Line 1</p>") != std::string::npos);
    assert(result.find("<p>Line 2</p>") != std::string::npos);
    assert(result.find("<br />") != std::string::npos);
    assert(result.find("<p>Line 4</p>") != std::string::npos);
}

void test_plain_text_escaping() {
    std::string input = "Hello <world> & \"friends\"";
    std::string result = plain_text_to_html(input);
    assert(result.find("&lt;world&gt;") != std::string::npos);
    assert(result.find("&amp;") != std::string::npos);
    assert(result.find("&quot;") != std::string::npos);
}

void test_simplify_strips_layout_tags() {
    std::string input = R"(<div><p>Hello</p><table><tr><td>Cell</td></tr></table><span>World</span></div>)";
    std::string result = simplify_html(input);
    assert(result.find("<p>Hello</p>") != std::string::npos);
    assert(result.find("Cell") != std::string::npos);
    assert(result.find("World") != std::string::npos);
    assert(result.find("<div") == std::string::npos);
    assert(result.find("<table") == std::string::npos);
    assert(result.find("<td") == std::string::npos);
    assert(result.find("<span") == std::string::npos);
}

void test_simplify_strips_all_attributes() {
    std::string input = R"(<p class="foo" id="bar">Text</p><h1 style="x:y">Title</h1><a href="junk">Link</a>)";
    std::string result = simplify_html(input);
    assert(result.find("<p>Text</p>") != std::string::npos);
    assert(result.find("<h1>Title</h1>") != std::string::npos);
    assert(result.find("<a href=\"junk\">Link</a>") != std::string::npos);
    assert(result.find("class=") == std::string::npos);
}

void test_simplify_removes_junk_sections() {
    std::string input = R"(
        <header>Email Header</header>
        <p>Real Content</p>
        <footer>Unsubscribe here</footer>
        <nav>Menu</nav>
    )";
    std::string result = simplify_html(input);
    assert(result.find("Real Content") != std::string::npos);
    assert(result.find("Email Header") == std::string::npos);
    assert(result.find("Unsubscribe") == std::string::npos);
    assert(result.find("Menu") == std::string::npos);
    assert(result.find("<header") == std::string::npos);
}

void test_simplify_collapses_br() {
    std::string input = R"(<p>Line 1</p><br><br><br><br><p>Line 2</p>)";
    std::string result = simplify_html(input);
    assert(result.find("<br/><br/>") != std::string::npos);
    assert(result.find("<br/><br/><br/>") == std::string::npos);
}

void test_simplify_separates_table_cells() {
    std::string input = R"(<table><tr><td>Alpha</td><td>Beta</td></tr><tr><td>Gamma</td></tr></table>)";
    std::string result = simplify_html(input);
    assert(result.find("AlphaBeta") == std::string::npos);
    assert(result.find("Alpha Beta") != std::string::npos);
    assert(result.find("<br/>") != std::string::npos);
    assert(result.find("Gamma") != std::string::npos);
}

// ── XHTML compliance tests ───────────────────────────────────────────────
// These guard against regressions that produce EPUB chapters that cannot be
// opened by strict XML-based e-readers (e.g. Crosspoint, Kobo).

void test_xhtml_nbsp_replaced() {
    // &nbsp; is HTML-only; XML parsers reject it as "undefined entity".
    // The sanitizer must replace it with the numeric equivalent &#160;.
    std::string input = "<p>word&nbsp;word</p>";
    std::string result = sanitize_html(input, /*keep_images=*/false);
    assert(result.find("&nbsp;") == std::string::npos && "&nbsp; must not appear in output");
    assert(result.find("&#160;") != std::string::npos && "&#160; must replace &nbsp;");
}

void test_xhtml_img_self_closed_when_keep_images() {
    // When images are kept, <img src="…"> must be emitted as <img src="…"/>
    // (void element, self-closing) so the XHTML is well-formed XML.
    std::string input = "<p>Text</p><img src=\"photo.jpg\">";
    std::string result = sanitize_html(input, /*keep_images=*/true);
    assert(result.find("<img") != std::string::npos && "img tag must be present");
    // Every <img must be followed eventually by /> not by just >
    auto pos = result.find("<img");
    while (pos != std::string::npos) {
        auto close = result.find('>', pos);
        assert(close != std::string::npos);
        assert(result[close - 1] == '/' && "img must be self-closed");
        pos = result.find("<img", pos + 1);
    }
}

void test_xhtml_already_self_closed_img_unchanged() {
    // An already-self-closed <img/> must not be double-closed to <img//>
    std::string input = "<p>a</p><img src=\"x.jpg\"/>";
    std::string result = sanitize_html(input, /*keep_images=*/true);
    assert(result.find("//>") == std::string::npos && "must not produce //>");
}

void test_xhtml_br_self_closed() {
    // <br> (no slash) must become <br/> for XHTML validity.
    std::string input = "<p>a<br>b</p>";
    std::string result = sanitize_html(input, /*keep_images=*/false);
    assert(result.find("<br>") == std::string::npos && "bare <br> must not appear");
    assert(result.find("<br/>") != std::string::npos && "<br/> must appear");
}

void test_simplify_removes_forms() {
    std::string input = R"(
        <p>Before</p>
        <form>
            <input type="text" />
            <button>Subscribe</button>
        </form>
        <button class="btn">Floating Button</button>
        <p>After</p>
    )";
    std::string result = simplify_html(input);
    assert(result.find("Before") != std::string::npos);
    assert(result.find("After") != std::string::npos);
    assert(result.find("Subscribe") == std::string::npos);
    assert(result.find("Floating Button") == std::string::npos);
    assert(result.find("<form") == std::string::npos);
    assert(result.find("<input") == std::string::npos);
    assert(result.find("<button") == std::string::npos);
}

void test_frameset_null_body_safe() {
    std::string fs_html = "<html><frameset><frame src='a.html'></frameset></html>";
    std::string s1 = sanitize_html(fs_html);
    std::string s2 = simplify_html(fs_html);
    assert(s1.empty() || s1.find("<script") == std::string::npos);
    assert(s2.empty() || s2.find("<script") == std::string::npos);
}

void test_strips_meta_and_link_tags() {
    std::string input = "<html><head><meta charset=\"utf-8\">"
                        "<link rel=\"stylesheet\" href=\"x.css\"></head>"
                        "<body><meta name=\"viewport\" content=\"width=device-width\">"
                        "<link rel=\"icon\" href=\"fav.ico\"><p>Keep</p></body></html>";
    std::string san = sanitize_html(input);
    std::string sim = simplify_html(input);
    assert(san.find("Keep") != std::string::npos);
    assert(sim.find("Keep") != std::string::npos);
    assert(san.find("<meta") == std::string::npos && san.find("<link") == std::string::npos);
    assert(sim.find("<meta") == std::string::npos && sim.find("<link") == std::string::npos);
}

void test_ensure_xhtml_quote_aware_gt() {
    std::string xhtml = endpaper::ensure_xhtml("<p><img title=\"a > b\" src=\"x.jpg\"></p>");
    assert(xhtml.find("<img title=\"a > b\" src=\"x.jpg\"/>") != std::string::npos);
}

void test_leading_whitespace_in_urls() {
    std::string input =
        "<p><a href=\" https://example.com/post \">Good</a>"
        "<a href=\"   javascript:alert(1)\">Bad</a>"
        "<img src=\"\thttps://example.com/pic.jpg\"></p>";
    std::string result = sanitize_html(input, /*keep_images=*/true);
    assert(result.find("https://example.com/post") != std::string::npos);
    assert(result.find("https://example.com/pic.jpg") != std::string::npos);
    assert(result.find("javascript:") == std::string::npos);
}

// ── Main ──────────────────────────────────────────────────────────────────

int main() {
    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║  Endpaper — HTML Sanitizer Tests    ║\n";
    std::cout << "╚══════════════════════════════════════╝\n\n";

    TEST(test_removes_img_tags);
    TEST(test_removes_self_closing_img);
    TEST(test_removes_style_attributes);
    TEST(test_removes_style_attributes_single_quotes);
    TEST(test_removes_style_blocks);
    TEST(test_removes_script_blocks);
    TEST(test_removes_class_attributes);
    TEST(test_removes_html_comments);
    TEST(test_preserves_structural_tags);
    TEST(test_complex_email_html);
    TEST(test_plain_text_to_html);
    TEST(test_plain_text_escaping);
    TEST(test_simplify_strips_layout_tags);
    TEST(test_simplify_strips_all_attributes);
    TEST(test_simplify_removes_junk_sections);
    TEST(test_simplify_collapses_br);
    TEST(test_simplify_separates_table_cells);
    TEST(test_simplify_removes_forms);
    TEST(test_xhtml_nbsp_replaced);
    TEST(test_xhtml_img_self_closed_when_keep_images);
    TEST(test_xhtml_already_self_closed_img_unchanged);
    TEST(test_xhtml_br_self_closed);
    TEST(test_frameset_null_body_safe);
    TEST(test_strips_meta_and_link_tags);
    TEST(test_ensure_xhtml_quote_aware_gt);
    TEST(test_leading_whitespace_in_urls);

    std::cout << "\n✓ All sanitizer tests passed!\n";
    return 0;
}
