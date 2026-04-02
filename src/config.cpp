#include "config.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

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
std::string Config::get(const std::string& section, const std::string& key,
                        const std::string& default_val) const {
    auto sit = values_.find(section);
    if (sit == values_.end()) return default_val;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return default_val;
    return kit->second;
}

int Config::get_int(const std::string& section, const std::string& key,
                    int default_val) const {
    std::string val = get(section, key);
    if (val.empty()) return default_val;
    try {
        return std::stoi(val);
    } catch (...) {
        return default_val;
    }
}

bool Config::get_bool(const std::string& section, const std::string& key,
                     bool default_val) const {
    std::string val = get(section, key);
    if (val.empty()) return default_val;
    std::string lower = val;
    for (char& c : lower) c = std::tolower(c);
    if (lower == "true" || lower == "yes" || lower == "1") return true;
    if (lower == "false" || lower == "no" || lower == "0") return false;
    return default_val;
}

void Config::set(const std::string& section, const std::string& key,
                 const std::string& value) {
    values_[section][key] = value;
}

// ── Save config to disk ──────────────────────────────────────────────────
void Config::save() const {
    std::ofstream out(config_file());
    if (!out) {
        throw std::runtime_error("Cannot write config: " +
                                 config_file().string());
    }
    for (const auto& [section, kvs] : values_) {
        out << "[" << section << "]\n";
        for (const auto& [k, v] : kvs) {
            out << k << " = " << v << "\n";
        }
        out << "\n";
    }
}

// ── Reload from disk ─────────────────────────────────────────────────────
void Config::reload() {
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

// ── INI parser ────────────────────────────────────────────────────────────
void Config::parse_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return;

    std::string current_section = "general";
    std::string line;

    while (std::getline(in, line)) {
        // Trim leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) line = line.substr(0, end + 1);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        // Section header
        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        // Key = Value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim key and value
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
}

} // namespace endpaper
