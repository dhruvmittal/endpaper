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

int main() {
    test_article_tag();
    test_main_tag();
    test_class_heuristics();
    test_id_heuristics();
    std::cout << "All HtmlExtractor tests passed!\n";
    return 0;
}
