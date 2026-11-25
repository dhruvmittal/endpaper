#include <iostream>
#include <string>
#include <cassert>
#include "html_extractor.h"

using namespace endpaper;

void test_article_tag() {
    std::string html = "<html><body><nav>Menu</nav><article><h1>Title</h1><p>Main content here.</p></article><footer>Footer</footer></body></html>";
    std::string extracted = HtmlExtractor::extract_main_content(html);
    assert(extracted.find("Main content here") != std::string::npos);
    assert(extracted.find("Menu") == std::string::npos);
    std::cout << "test_article_tag passed\n";
}

void test_main_tag() {
    std::string html = "<div>Header</div><main>Actual content</main><div>Aside</div>";
    std::string extracted = HtmlExtractor::extract_main_content(html);
    assert(extracted == "Actual content");
    std::cout << "test_main_tag passed\n";
}

void test_class_heuristics() {
    std::string html = "<div class='sidebar'>Side</div><div class='post-content'>The meat of the article</div>";
    std::string extracted = HtmlExtractor::extract_main_content(html);
    assert(extracted == "The meat of the article");
    std::cout << "test_class_heuristics passed\n";
}

void test_id_heuristics() {
    std::string html = "<div id='content'>Important stuff</div>";
    std::string extracted = HtmlExtractor::extract_main_content(html);
    assert(extracted == "Important stuff");
    std::cout << "test_id_heuristics passed\n";
}

void test_nested_div_not_truncated() {
    std::string html = "<div class='sidebar'>Side</div>"
                       "<div class='post-content'><div class='intro'><p>First</p></div><p>Second</p></div>";
    std::string extracted = HtmlExtractor::extract_main_content(html);
    assert(extracted.find("First") != std::string::npos);
    assert(extracted.find("Second") != std::string::npos);
    assert(extracted.find("Side") == std::string::npos);
    std::cout << "test_nested_div_not_truncated passed\n";
}

void test_relative_url_resolution() {
    std::string html = "<html><body><article>"
                       "<img src=\"images/pic.png\">"
                       "<a href=\"/about\">About</a>"
                       "<a href=\"../other\">Other</a>"
                       "</article></body></html>";
    std::string extracted = HtmlExtractor::extract_main_content(
        html, "https://example.com/2026/03/article.html");
    assert(extracted.find("https://example.com/2026/03/images/pic.png") != std::string::npos);
    assert(extracted.find("https://example.com/about") != std::string::npos);
    assert(extracted.find("https://example.com/2026/other") != std::string::npos);
    std::cout << "test_relative_url_resolution passed\n";
}

void test_preserves_non_junk_compound_classes() {
    std::string html = "<html><body>"
                       "<article class=\"has-header-image no-sidebar comments-open\">"
                       "<p>Real Story Body</p></article>"
                       "</body></html>";
    std::string extracted = HtmlExtractor::extract_main_content(html);
    assert(extracted.find("Real Story Body") != std::string::npos);
    std::cout << "test_preserves_non_junk_compound_classes passed\n";
}

void test_skips_comments_article() {
    std::string html = "<html><body>"
                       "<main><p>Main Article Text</p></main>"
                       "<div id=\"comments\"><article><p>User Comment Reply</p></article></div>"
                       "</body></html>";
    std::string extracted = HtmlExtractor::extract_main_content(html);
    assert(extracted.find("Main Article Text") != std::string::npos);
    assert(extracted.find("User Comment Reply") == std::string::npos);
    std::cout << "test_skips_comments_article passed\n";
}

int main() {
    test_article_tag();
    test_main_tag();
    test_class_heuristics();
    test_id_heuristics();
    test_nested_div_not_truncated();
    test_relative_url_resolution();
    test_preserves_non_junk_compound_classes();
    test_skips_comments_article();
    std::cout << "All HtmlExtractor tests passed!\n";
    return 0;
}
