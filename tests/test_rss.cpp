#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>
#include "../src/rss_fetcher.h"
#include "../src/database.h"
#include "../src/config.h"

using namespace mail2book;
namespace fs = std::filesystem;

void test_rss_parsing() {
    std::cout << "  TEST: test_rss_20_parsing... " << std::flush;
    
    std::string rss_xml = R"RE(
        <?xml version="1.0" encoding="UTF-8" ?>
        <rss version="2.0">
        <channel>
            <title>Test Feed</title>
            <item>
                <title>Article 1</title>
                <link>http://example.com/1</link>
                <description>Content 1</description>
                <author>Author 1</author>
            </item>
        </channel>
        </rss>
    )RE";

    auto items = RssFetcher::parse_xml(rss_xml);
    assert(items.size() == 1);
    assert(items[0].title == "Article 1");
    assert(items[0].link == "http://example.com/1");
    assert(items[0].content_html == "Content 1");
    assert(items[0].author == "Author 1");
    
    std::cout << "PASS" << std::endl;

    std::cout << "  TEST: test_atom_parsing... " << std::flush;
    
    std::string atom_xml = R"RE(
        <?xml version="1.0" encoding="utf-8"?>
        <feed xmlns="http://www.w3.org/2005/Atom">
            <title>Test Atom</title>
            <entry>
                <title>Atom Item</title>
                <link href="http://example.com/atom"/>
                <content type="html">Atom Content</content>
                <author><name>Atom Author</name></author>
            </entry>
        </feed>
    )RE";

    items = RssFetcher::parse_xml(atom_xml);
    assert(items.size() == 1);
    assert(items[0].title == "Atom Item");
    assert(items[0].link == "http://example.com/atom");
    assert(items[0].content_html == "Atom Content");
    assert(items[0].author == "Atom Author");

    std::cout << "PASS" << std::endl;
}

void test_rss_large_feed() {
    std::cout << "  TEST: test_rss_large_feed... " << std::flush;
    
    std::string rss_xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?><rss version=\"2.0\"><channel><title>Large Feed</title>";
    for (int i = 0; i < 25; ++i) {
        rss_xml += "<item><title>Article " + std::to_string(i) + "</title><link>http://example.com/" + std::to_string(i) + "</link></item>";
    }
    rss_xml += "</channel></rss>";

    // Test unlimited
    auto items = RssFetcher::parse_xml(rss_xml, 0);
    assert(items.size() == 25);

    // Test limit 15
    items = RssFetcher::parse_xml(rss_xml, 15);
    assert(items.size() == 15);

    std::cout << "PASS" << std::endl;
}

void test_database_rss() {
    std::cout << "  TEST: test_database_rss_feeds... " << std::flush;
    
    fs::path test_db = "test_rss_db.db";
    if (fs::exists(test_db)) fs::remove(test_db);

    {
        Database db(test_db);
        db.init();

        db.add_rss_feed("My Feed", "http://example.com/feed");
        auto feeds = db.list_rss_feeds();
        assert(feeds.size() == 1);
        assert(feeds[0].title == "My Feed");
        assert(feeds[0].url == "http://example.com/feed");

        db.delete_rss_feed(feeds[0].id);
        feeds = db.list_rss_feeds();
        assert(feeds.empty());
    }

    if (fs::exists(test_db)) fs::remove(test_db);
    std::cout << "PASS" << std::endl;
}

int main() {
    std::cout << "\n╔══════════════════════════════════════╗" << std::endl;
    std::cout << "║  Mail2Book — RSS Fetcher Tests       ║" << std::endl;
    std::cout << "╚══════════════════════════════════════╝" << std::endl;

    try {
        test_rss_parsing();
        test_rss_large_feed();
        test_database_rss();
        std::cout << "\n✓ All RSS tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
