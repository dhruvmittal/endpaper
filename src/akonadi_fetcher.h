#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <vector>

namespace endpaper {

struct EmailItem {
    int64_t akonadi_id;
    std::string subject;
    std::string sender;
    std::string html_body;   // HTML content (or converted plain text)
    std::string date;
};

struct FolderInfo {
    int64_t collection_id;
    int64_t parent_id = -1;
    std::string name;
    std::string full_path;   // e.g. "Local Folders/Inbox"
};

/// Resolve hierarchical full_path strings from parent_id links in-place.
void populate_folder_paths(std::vector<FolderInfo>& folders);

/// True for standard non-reading folders (Trash, Sent, Drafts, Outbox, Junk/Spam, etc.).
[[nodiscard]] bool is_system_mail_folder(const std::string& name);

/// Async wrapper around KF6 Akonadi for fetching KMail data.
/// All fetch operations run on a background thread with a Qt event loop.
class AkonadiFetcher {
public:
    AkonadiFetcher();
    ~AkonadiFetcher();

    AkonadiFetcher(const AkonadiFetcher&) = delete;
    AkonadiFetcher& operator=(const AkonadiFetcher&) = delete;

    /// Stop the background worker thread and cancel pending jobs cleanly.
    void stop();

    /// Check if the Akonadi server is running and reachable.
    [[nodiscard]] bool is_available() const;

    /// Fetch all mail folders (collections) asynchronously.
    [[nodiscard]] std::future<std::vector<FolderInfo>> fetch_folders();

    /// Fetch all email items in a collection asynchronously.
    [[nodiscard]] std::future<std::vector<EmailItem>>
    fetch_emails(int64_t collection_id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace endpaper
