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
    assert(result.find("<a>Link</a>") != std::string::npos);
    assert(result.find("class=") == std::string::npos);
    assert(result.find("href=") == std::string::npos);
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
    assert(result.find("<br /><br />") != std::string::npos);
    assert(result.find("<br /><br /><br />") == std::string::npos);
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

    std::cout << "\n✓ All sanitizer tests passed!\n";
    return 0;
}
