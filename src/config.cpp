#include "config.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace endpaper {

namespace fs = std::filesystem;

// ── Helper: resolve XDG env with fallback ─────────────────────────────────
static fs::path xdg_path(const char* env_var, const fs::path& fallback) {
    if (const char* val = std::getenv(env_var); val && val[0] != '\0') {
        return fs::path(val) / "endpaper";
    }
    const char* home = std::getenv("HOME");
    if (!home) {
        throw std::runtime_error("$HOME is not set");
    }
    return fs::path(home) / fallback / "endpaper";
}

// ── Constructor ───────────────────────────────────────────────────────────
Config::Config()
    : config_dir_(xdg_path("XDG_CONFIG_HOME", ".config"))
    , data_dir_(xdg_path("XDG_DATA_HOME", ".local/share"))
    , state_dir_(xdg_path("XDG_STATE_HOME", ".local/state"))
{
    ensure_directories();
    if (fs::exists(config_file())) {
        parse_file(config_file());
    }
}

// ── Path accessors ────────────────────────────────────────────────────────
fs::path Config::config_dir()  const { return config_dir_; }
fs::path Config::config_file() const { return config_dir_ / "endpaper.conf"; }
fs::path Config::data_dir()    const { return data_dir_; }
fs::path Config::db_path()     const { return data_dir_ / "library.db"; }
fs::path Config::state_dir()   const { return state_dir_; }
fs::path Config::log_path()    const { return state_dir_ / "error.log"; }

// ── Config value access ───────────────────────────────────────────────────
std::string Config::get_unlocked(const std::string& section,
                                 const std::string& key,
                                 const std::string& default_val) const {
    auto sit = values_.find(section);
    if (sit == values_.end()) return default_val;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return default_val;
    return kit->second;
}

bool Config::parse_bool(const std::string& val, bool default_val) {
    if (val.empty()) return default_val;
    std::string lower = val;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "true" || lower == "yes" || lower == "1") return true;
    if (lower == "false" || lower == "no" || lower == "0") return false;
    return default_val;
}

std::string Config::get(const std::string& section, const std::string& key,
                        const std::string& default_val) const {
    std::lock_guard<std::mutex> lock(mu_);
    return get_unlocked(section, key, default_val);
}

int Config::get_int(const std::string& section, const std::string& key,
                    int default_val) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::string val = get_unlocked(section, key, "");
    if (val.empty()) return default_val;
    try {
        return std::stoi(val);
    } catch (...) {
        return default_val;
    }
}

int64_t Config::get_int64(const std::string& section, const std::string& key,
                          int64_t default_val) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::string val = get_unlocked(section, key, "");
    if (val.empty()) return default_val;
    try {
        return std::stoll(val);
    } catch (...) {
        return default_val;
    }
}

bool Config::get_bool(const std::string& section, const std::string& key,
                     bool default_val) const {
    std::lock_guard<std::mutex> lock(mu_);
    return parse_bool(get_unlocked(section, key, ""), default_val);
}

void Config::set(const std::string& section, const std::string& key,
                 const std::string& value) {
    std::lock_guard<std::mutex> lock(mu_);
    values_[section][key] = value;
}

// ── Save config to disk ──────────────────────────────────────────────────
void Config::save() const {
    std::lock_guard<std::mutex> lock(mu_);
    fs::path target = config_file();
    fs::path tmp = target;
    tmp += ".tmp";

    std::ostringstream ss;
    for (const auto& [section, kvs] : values_) {
        ss << "[" << section << "]\n";
        for (const auto& [k, v] : kvs) {
            ss << k << " = " << v << "\n";
        }
        ss << "\n";
    }
    std::string data = ss.str();

    // Create temp file directly with mode 0600 so API tokens are never world-readable
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        throw std::runtime_error("Cannot write config: " + tmp.string() + ": " + std::strerror(errno));
    }

    size_t offset = 0;
    while (offset < data.size()) {
        ssize_t n = ::write(fd, data.data() + offset, data.size() - offset);
        if (n < 0) {
            if (errno == EINTR) continue;
            int err = errno;
            ::close(fd);
            std::error_code ec;
            fs::remove(tmp, ec);
            throw std::runtime_error("Failed writing config: " + tmp.string() + ": " + std::strerror(err));
        }
        offset += static_cast<size_t>(n);
    }

    ::fchmod(fd, 0600);
    if (::close(fd) != 0) {
        int err = errno;
        std::error_code ec;
        fs::remove(tmp, ec);
        throw std::runtime_error("Failed closing config: " + tmp.string() + ": " + std::strerror(err));
    }

    fs::rename(tmp, target);
}

// ── Reload from disk ─────────────────────────────────────────────────────
void Config::reload() {
    std::lock_guard<std::mutex> lock(mu_);
    values_.clear();
    if (fs::exists(config_file())) {
        parse_file(config_file());
    }
}

// ── Directory creation ────────────────────────────────────────────────────
void Config::ensure_directories() {
    fs::create_directories(config_dir_);
    fs::create_directories(data_dir_);
    fs::create_directories(state_dir_);
}

// ── INI parser & legacy key migration ────────────────────────────────────
void Config::migrate_legacy_keys_unlocked() {
    struct KeyAlias {
        const char* old_sec;
        const char* old_key;
        const char* new_sec;
        const char* new_key;
    };
    static const KeyAlias kAliases[] = {
        {"behavior", "auto_fetch_akonadi", "general", "auto_fetch_kmail"},
        {"behavior", "auto_fetch_miniflux", "general", "auto_fetch_miniflux"},
        {"behavior", "remember_config", "general", "remember_state"},
        {"state", "active_tab", "state", "last_page_index"},
        {"state", "last_miniflux_filter", "state", "last_miniflux_status_filter"},
    };

    for (const auto& a : kAliases) {
        auto sit = values_.find(a.old_sec);
        if (sit == values_.end()) continue;
        auto kit = sit->second.find(a.old_key);
        if (kit == sit->second.end()) continue;

        auto& dest = values_[a.new_sec];
        if (dest.find(a.new_key) == dest.end()) {
            dest[a.new_key] = kit->second;
        }
        sit->second.erase(kit);
        if (sit->second.empty()) {
            values_.erase(sit);
        }
    }
}

void Config::parse_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return;

    std::string current_section = "general";
    std::string line;

    while (std::getline(in, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) line = line.substr(0, end + 1);

        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t");
            size_t b = s.find_last_not_of(" \t");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(key);
        trim(val);

        if (!key.empty()) {
            values_[current_section][key] = val;
        }
    }

    migrate_legacy_keys_unlocked();
}

} // namespace endpaper
