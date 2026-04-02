#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace endpaper {

/// XDG-compliant configuration manager.
/// Resolves paths, creates directories, parses INI-style .conf files.
class Config {
public:
    /// Load config from default XDG path. Creates dirs if needed.
    Config();

    /// XDG directory accessors
    [[nodiscard]] std::filesystem::path config_dir() const;
    [[nodiscard]] std::filesystem::path config_file() const;
    [[nodiscard]] std::filesystem::path data_dir() const;
    [[nodiscard]] std::filesystem::path db_path() const;
    [[nodiscard]] std::filesystem::path state_dir() const;
    [[nodiscard]] std::filesystem::path log_path() const;

    [[nodiscard]] std::string get(const std::string& section,
                                  const std::string& key,
                                  const std::string& default_val = "") const;

    /// Get a config value as an integer.
    [[nodiscard]] int get_int(const std::string& section,
                              const std::string& key,
                              int default_val = 0) const;

    /// Get a config value as a boolean.
    [[nodiscard]] bool get_bool(const std::string& section,
                                const std::string& key,
                                bool default_val = false) const;

    /// Set a config value (in memory only).
    void set(const std::string& section, const std::string& key,
             const std::string& value);

    /// Write current config to disk.
    void save() const;

    /// Reload config from disk.
    void reload();

private:
    void ensure_directories();
    void parse_file(const std::filesystem::path& path);

    std::filesystem::path config_dir_;
    std::filesystem::path data_dir_;
    std::filesystem::path state_dir_;

    // section -> (key -> value)
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>> values_;
};

} // namespace endpaper
