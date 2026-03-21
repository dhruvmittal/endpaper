#pragma once

#include <string>

namespace mail2book {

/// Sanitize HTML email content for clean text-only EPUB output.
///
/// Removes:
///   - All <img> tags (self-closing or not)
///   - All style="..." attributes from remaining tags
///   - All <style>...</style> blocks
///
/// Preserves structural HTML: <p>, <br>, <h1>-<h6>, <ul>, <ol>, <li>,
/// <a>, <blockquote>, <div>, <span>, <table>, <tr>, <td>, <th>, <pre>, <code>
[[nodiscard]] std::string sanitize_html(const std::string& html);

/// Aggressively simplify HTML for a "distraction-free" EPUB.
/// Removes almost all layout tags (div, table, etc.) and junk (Unsubscribe etc.)
[[nodiscard]] std::string simplify_html(const std::string& html);

/// Convert plain text to simple HTML (wrap lines in <p> tags).
[[nodiscard]] std::string plain_text_to_html(const std::string& text);

} // namespace mail2book
