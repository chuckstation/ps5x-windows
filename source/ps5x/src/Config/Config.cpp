// PS5x – Config implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/Config/Config.h"
#include "PS5x/Logger/Logger.h"
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace PS5x::Config {

namespace {

struct ConfigState
{
    PS5xConfig    cfg;
    std::mutex    mtx;

    static ConfigState& Get()
    {
        static ConfigState s;
        return s;
    }
};

// Key-value store (must be before Load/Save which reference it)
std::mutex             kv_mtx;
std::map<std::string, std::string> kv_store;

} // anonymous namespace

void Reset()
{
    auto& st = ConfigState::Get();
    std::lock_guard lock(st.mtx);
    st.cfg = PS5xConfig{};
    {
        std::lock_guard lk(kv_mtx);
        kv_store.clear();
    }
}

const PS5xConfig& Get()
{
    return ConfigState::Get().cfg;
}

PS5xConfig& GetMutable()
{
    return ConfigState::Get().cfg;
}

bool Load(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        PS5X_WARN("Config file not found: %s – using defaults.", path.string().c_str());
        return false;
    }

    std::ifstream f(path);
    if (!f.is_open())
    {
        PS5X_ERROR("Cannot open config: %s", path.string().c_str());
        return false;
    }

    // INI parser: supports [section] headers and key=value pairs.
    // Lines beginning with # or ; are comments. Section names are
    // prepended to keys as "section.key" in the kv_store.
    std::lock_guard lk(kv_mtx);
    kv_store.clear(); // Reset before loading
    std::string line, section;
    while (std::getline(f, line)) {
        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            size_t end = line.find(']');
            section = (end != std::string::npos) ? line.substr(1, end-1) : "";
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        // Trim key and value
        auto trim = [](std::string& s) {
            size_t b = s.find_first_not_of(" \t");
            size_t e = s.find_last_not_of(" \t\r\n");
            s = (b == std::string::npos) ? "" : s.substr(b, e-b+1);
        };
        trim(key); trim(value);
        std::string full_key = section.empty() ? key : section + "." + key;
        kv_store[full_key] = value;
    }

    PS5X_INFO("[Config] Loaded %zu entries from %s",
              kv_store.size(), path.string().c_str());
    return true;
}

bool Save(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    if (!f.is_open())
    {
        PS5X_ERROR("Cannot write config: %s", path.string().c_str());
        return false;
    }

    // Serialise kv_store as INI. Keys of the form "section.key" are
    // grouped under [section] headers. Keys without a dot go under [general].
    std::lock_guard lk(kv_mtx);
    f << "# PS5x v1.0.0 configuration (auto-generated)\n";
    f << "# Repository: github.com/libaerto/ps5x-windows\n";

    // Group keys by section
    std::map<std::string, std::vector<std::pair<std::string,std::string>>> sections;
    for (auto& [fullKey, val] : kv_store) {
        size_t dot = fullKey.find('.');
        if (dot == std::string::npos) {
            sections["general"].push_back({fullKey, val});
        } else {
            std::string sec = fullKey.substr(0, dot);
            std::string key = fullKey.substr(dot + 1);
            sections[sec].push_back({key, val});
        }
    }
    for (auto& [sec, pairs] : sections) {
        f << "\n[" << sec << "]\n";
        for (auto& [k, v] : pairs) f << k << " = " << v << "\n";
    }

    PS5X_INFO("[Config] Saved %zu entries to %s",
              kv_store.size(), path.string().c_str());
    return true;
}

// ── Simple key-value string store ────────────────────────────────────────

void Set(const std::string& key, const std::string& value) {
    std::lock_guard lk(kv_mtx);
    kv_store[key] = value;
}

std::string Get(const std::string& key) {
    std::lock_guard lk(kv_mtx);
    auto it = kv_store.find(key);
    return (it != kv_store.end()) ? it->second : "";
}

bool ValidateFirmwarePath(const std::filesystem::path& path)
{
    // PS5x NEVER bundles, downloads, or provides firmware.
    // The user must supply their own legally-obtained firmware.
    if (path.empty())
    {
        PS5X_ERROR("Firmware path is not set.  "
                   "You must provide your own PS5 firmware.  "
                   "PS5x does not supply, bundle, or download firmware.");
        return false;
    }

    if (!std::filesystem::exists(path))
    {
        PS5X_ERROR("Firmware path does not exist: %s", path.string().c_str());
        return false;
    }

    PS5X_INFO("Firmware path accepted: %s (content not verified – user responsibility)",
              path.string().c_str());
    return true;
}

} // namespace PS5x::Config
