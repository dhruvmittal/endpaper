#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mail2book {

struct EpubChapter {
    std::string title;       // chapter title (email subject)
    std::string author;      // email sender
    std::string body_html;   // sanitized HTML content
};

struct EpubMetadata {
    std::string title;
    std::string author    = "Unknown";
    std::string publisher = "Mail2Book";
    std::string language  = "en";
    std::string uuid;        // auto-generated if empty
};

/// Build EPUB 3.0 compliant archives from chapter data.
class EpubBuilder {
public:
    /// Build a new EPUB file from chapters.
    /// Uses atomic write: writes to .tmp then renames.
    static void build(const std::filesystem::path& output_path,
                      const EpubMetadata& meta,
                      const std::vector<EpubChapter>& chapters);

    /// Append new chapters to an existing EPUB file.
    /// Extracts the archive, updates manifest/spine/toc, re-zips.
    static void append(const std::filesystem::path& epub_path,
                       const std::vector<EpubChapter>& new_chapters);

private:
    // EPUB 3.0 template generators
    static std::string make_container_xml();
    static std::string make_content_opf(const EpubMetadata& meta,
                                        int chapter_count);
    static std::string make_toc_xhtml(const std::string& title,
                                      const std::vector<EpubChapter>& chapters,
                                      int start_index = 0);
    static std::string make_chapter_xhtml(const EpubChapter& chapter,
                                          int index);
    static std::string make_stylesheet();
    static std::string generate_uuid();

    // libzip helpers
    static void create_epub_zip(const std::filesystem::path& zip_path,
                                const EpubMetadata& meta,
                                const std::vector<EpubChapter>& chapters);
};

} // namespace mail2book
