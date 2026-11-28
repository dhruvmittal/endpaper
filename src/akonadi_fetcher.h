#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

/// True if collection_id or any ancestor in by_id is in hidden.
[[nodiscard]] bool is_folder_hidden(int64_t collection_id,
                                    const std::unordered_map<int64_t, const FolderInfo*>& by_id,
                                    const std::unordered_set<int64_t>& hidden);

/// Collect root_id and all of its descendants from a flat folder list.
[[nodiscard]] std::vector<int64_t> collect_folder_subtree_ids(int64_t root_id,
                                                              const std::vector<FolderInfo>& folders);

/// Return the top-level account name for collection_id, or empty string if it is already a root.
[[nodiscard]] std::string folder_account_name(int64_t collection_id,
                                              const std::unordered_map<int64_t, const FolderInfo*>& by_id);

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
