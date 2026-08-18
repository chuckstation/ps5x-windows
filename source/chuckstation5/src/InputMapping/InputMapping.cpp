// ChuckStation5 – Input Mapping System implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "ChuckStation5/InputMapping/InputMapping.h"
#include "ChuckStation5/Logger/Logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace ChuckStation5::InputMapping {

// ── Name tables ───────────────────────────────────────────────────────────
static const char* Ps5ButtonName(Ps5Button b)
{
    switch (b) {
        case Ps5Button::Triangle:  return "Triangle";
        case Ps5Button::Circle:    return "Circle";
        case Ps5Button::Cross:     return "Cross";
        case Ps5Button::Square:    return "Square";
        case Ps5Button::L1:        return "L1";
        case Ps5Button::R1:        return "R1";
        case Ps5Button::L2:        return "L2";
        case Ps5Button::R2:        return "R2";
        case Ps5Button::Create:    return "Create";
        case Ps5Button::Options:   return "Options";
        case Ps5Button::PS:        return "PS";
        case Ps5Button::TouchPad:  return "TouchPad";
        case Ps5Button::L3:        return "L3";
        case Ps5Button::R3:        return "R3";
        case Ps5Button::DPadUp:    return "DPadUp";
        case Ps5Button::DPadDown:  return "DPadDown";
        case Ps5Button::DPadLeft:  return "DPadLeft";
        case Ps5Button::DPadRight: return "DPadRight";
        case Ps5Button::GyroX:     return "GyroX";
        case Ps5Button::GyroY:     return "GyroY";
        case Ps5Button::GyroZ:     return "GyroZ";
        default:                   return "Unknown";
    }
}

static Ps5Button Ps5ButtonFromName(const std::string& name)
{
    // Linear scan is fine for a small enum
    for (uint16_t i = 0; i < static_cast<uint16_t>(Ps5Button::Count); ++i) {
        if (name == Ps5ButtonName(static_cast<Ps5Button>(i)))
            return static_cast<Ps5Button>(i);
    }
    return Ps5Button::Count; // sentinel
}

static const char* HostInputTypeName(HostInputType t)
{
    switch (t) {
        case HostInputType::Keyboard: return "Keyboard";
        case HostInputType::Mouse:    return "Mouse";
        case HostInputType::Gamepad:  return "Gamepad";
        case HostInputType::Touch:    return "Touch";
        default:                      return "Unknown";
    }
}

static HostInputType HostInputTypeFromName(const std::string& name)
{
    if (name == "Keyboard") return HostInputType::Keyboard;
    if (name == "Mouse")    return HostInputType::Mouse;
    if (name == "Gamepad")  return HostInputType::Gamepad;
    if (name == "Touch")    return HostInputType::Touch;
    return HostInputType::Keyboard; // default
}

// ── Module state ──────────────────────────────────────────────────────────
namespace {

struct InputMappingState {
    MappingProfile              activeProfile;
    std::filesystem::path       profileDir = "input";
    bool                        initialised = false;
    std::mutex                  mtx;

    static InputMappingState& Get() { static InputMappingState s; return s; }
};

} // anonymous namespace

// ── Built-in profiles ─────────────────────────────────────────────────────

MappingProfile DefaultKeyboardProfile()
{
    MappingProfile p;
    p.name = "Keyboard";

    // Virtual key codes (platform-agnostic, matching common VK/SDL conventions)
    // Face buttons: J=Triangle, K=Circle, L=Cross, I=Square
    // DPad: WASD
    // Shoulders: Q=L1, E=R1, 1=L2, 3=R2
    // Special: Enter=Options, Escape=Create, Space=Cross (alt), Tab=TouchPad
    // L3/R3: F/V
    p.entries = {
        // Face buttons
        { Ps5Button::Triangle,  HostInputType::Keyboard, 0x4A, 1.0f, 0.0f, false }, // J
        { Ps5Button::Circle,    HostInputType::Keyboard, 0x4B, 1.0f, 0.0f, false }, // K
        { Ps5Button::Cross,     HostInputType::Keyboard, 0x4C, 1.0f, 0.0f, false }, // L
        { Ps5Button::Square,    HostInputType::Keyboard, 0x49, 1.0f, 0.0f, false }, // I

        // DPad – WASD
        { Ps5Button::DPadUp,    HostInputType::Keyboard, 0x57, 1.0f, 0.0f, false }, // W
        { Ps5Button::DPadDown,  HostInputType::Keyboard, 0x53, 1.0f, 0.0f, false }, // S
        { Ps5Button::DPadLeft,  HostInputType::Keyboard, 0x41, 1.0f, 0.0f, false }, // A
        { Ps5Button::DPadRight, HostInputType::Keyboard, 0x44, 1.0f, 0.0f, false }, // D

        // DPad – Arrow keys (alternative)
        { Ps5Button::DPadUp,    HostInputType::Keyboard, 0x26, 1.0f, 0.0f, false }, // Up
        { Ps5Button::DPadDown,  HostInputType::Keyboard, 0x28, 1.0f, 0.0f, false }, // Down
        { Ps5Button::DPadLeft,  HostInputType::Keyboard, 0x25, 1.0f, 0.0f, false }, // Left
        { Ps5Button::DPadRight, HostInputType::Keyboard, 0x27, 1.0f, 0.0f, false }, // Right

        // Shoulder buttons
        { Ps5Button::L1,        HostInputType::Keyboard, 0x51, 1.0f, 0.0f, false }, // Q
        { Ps5Button::R1,        HostInputType::Keyboard, 0x45, 1.0f, 0.0f, false }, // E
        { Ps5Button::L2,        HostInputType::Keyboard, 0x31, 1.0f, 0.0f, false }, // 1
        { Ps5Button::R2,        HostInputType::Keyboard, 0x33, 1.0f, 0.0f, false }, // 3

        // Stick clicks
        { Ps5Button::L3,        HostInputType::Keyboard, 0x46, 1.0f, 0.0f, false }, // F
        { Ps5Button::R3,        HostInputType::Keyboard, 0x56, 1.0f, 0.0f, false }, // V

        // Special
        { Ps5Button::Options,   HostInputType::Keyboard, 0x0D, 1.0f, 0.0f, false }, // Enter
        { Ps5Button::Create,    HostInputType::Keyboard, 0x1B, 1.0f, 0.0f, false }, // Escape
        { Ps5Button::TouchPad,  HostInputType::Keyboard, 0x09, 1.0f, 0.0f, false }, // Tab
        { Ps5Button::PS,        HostInputType::Keyboard, 0x70, 1.0f, 0.0f, false }, // F1
    };

    return p;
}

MappingProfile DefaultXboxProfile()
{
    MappingProfile p;
    p.name = "Xbox";

    // Xbox controller button indices (XInput convention)
    // A=0, B=1, X=2, Y=3, LB=4, RB=5, Back=6, Start=7, L3=8, R3=9
    // DPad maps to Xbox DPad buttons or share axes
    p.entries = {
        // Face buttons (Xbox → DualSense)
        { Ps5Button::Cross,     HostInputType::Gamepad, 0, 1.0f, 0.0f, false },  // A → Cross
        { Ps5Button::Circle,    HostInputType::Gamepad, 1, 1.0f, 0.0f, false },  // B → Circle
        { Ps5Button::Square,    HostInputType::Gamepad, 2, 1.0f, 0.0f, false },  // X → Square
        { Ps5Button::Triangle,  HostInputType::Gamepad, 3, 1.0f, 0.0f, false },  // Y → Triangle

        // Shoulders
        { Ps5Button::L1,        HostInputType::Gamepad, 4, 1.0f, 0.0f, false },  // LB → L1
        { Ps5Button::R1,        HostInputType::Gamepad, 5, 1.0f, 0.0f, false },  // RB → R1

        // Triggers (axis indices 4=L, 5=R)
        { Ps5Button::L2,        HostInputType::Gamepad, 4, 1.0f, 0.05f, false }, // LT → L2
        { Ps5Button::R2,        HostInputType::Gamepad, 5, 1.0f, 0.05f, false }, // RT → R2

        // Special
        { Ps5Button::Create,    HostInputType::Gamepad, 6, 1.0f, 0.0f, false },  // Back → Create
        { Ps5Button::Options,   HostInputType::Gamepad, 7, 1.0f, 0.0f, false },  // Start → Options

        // Stick clicks
        { Ps5Button::L3,        HostInputType::Gamepad, 8, 1.0f, 0.0f, false },
        { Ps5Button::R3,        HostInputType::Gamepad, 9, 1.0f, 0.0f, false },

        // DPad (buttons 10-13 in XInput)
        { Ps5Button::DPadUp,    HostInputType::Gamepad, 10, 1.0f, 0.0f, false },
        { Ps5Button::DPadDown,  HostInputType::Gamepad, 11, 1.0f, 0.0f, false },
        { Ps5Button::DPadLeft,  HostInputType::Gamepad, 12, 1.0f, 0.0f, false },
        { Ps5Button::DPadRight, HostInputType::Gamepad, 13, 1.0f, 0.0f, false },

        // TouchPad mapped to Back (same as Create – user can rebind)
        { Ps5Button::TouchPad,  HostInputType::Gamepad, 6, 1.0f, 0.0f, false },
    };

    return p;
}

MappingProfile DefaultDualSenseProfile()
{
    MappingProfile p;
    p.name = "DualSense";

    // 1:1 identity mapping – DualSense button indices match Ps5Button enum
    for (uint16_t i = 0; i < static_cast<uint16_t>(Ps5Button::Count); ++i) {
        // Skip gyro axes (they use axis mapping, not buttons)
        if (i >= static_cast<uint16_t>(Ps5Button::GyroX) &&
            i <= static_cast<uint16_t>(Ps5Button::GyroZ))
            continue;

        MappingEntry e;
        e.target     = static_cast<Ps5Button>(i);
        e.sourceType = HostInputType::Gamepad;
        e.sourceId   = i;
        e.scale      = 1.0f;
        e.deadzone   = (i >= static_cast<uint16_t>(Ps5Button::L2) &&
                        i <= static_cast<uint16_t>(Ps5Button::R2)) ? 0.05f : 0.0f;
        e.inverted   = false;
        p.entries.push_back(e);
    }

    // Gyro axes (treated as gamepad axes 17/18/19)
    p.entries.push_back({ Ps5Button::GyroX, HostInputType::Gamepad, 17, 1.0f, 0.02f, false });
    p.entries.push_back({ Ps5Button::GyroY, HostInputType::Gamepad, 18, 1.0f, 0.02f, false });
    p.entries.push_back({ Ps5Button::GyroZ, HostInputType::Gamepad, 19, 1.0f, 0.02f, false });

    return p;
}

// ── INI serialisation ─────────────────────────────────────────────────────
// Simple INI format:
//   [profile]
//   name=Keyboard
//   [mapping.0]
//   target=Triangle
//   sourceType=Keyboard
//   sourceId=74
//   scale=1.0
//   deadzone=0.0
//   inverted=false

[[maybe_unused]] static std::string ProfilePath(const std::string& name)
{
    auto& st = InputMappingState::Get();
    return (st.profileDir / (name + ".ini")).string();
}

static bool WriteProfileIni(const MappingProfile& profile, const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    if (!f.is_open()) {
        CHUCKSTATION5_ERROR("[InputMapping] Cannot write profile to '%s'.", path.string().c_str());
        return false;
    }

    f << "[profile]\n";
    f << "name=" << profile.name << "\n\n";

    for (size_t i = 0; i < profile.entries.size(); ++i) {
        const auto& e = profile.entries[i];
        f << "[mapping." << i << "]\n";
        f << "target="      << Ps5ButtonName(e.target)      << "\n";
        f << "sourceType="  << HostInputTypeName(e.sourceType) << "\n";
        f << "sourceId="    << e.sourceId                   << "\n";
        f << "scale="       << e.scale                      << "\n";
        f << "deadzone="    << e.deadzone                   << "\n";
        f << "inverted="    << (e.inverted ? "true" : "false") << "\n";
        f << "\n";
    }

    f.close();
    CHUCKSTATION5_INFO("[InputMapping] Saved profile '%s' to %s (%zu mappings).",
              profile.name.c_str(), path.string().c_str(), profile.entries.size());
    return true;
}

// Minimal INI parser: reads key=value pairs grouped by [section]
struct IniSection {
    std::string name;
    std::vector<std::pair<std::string, std::string>> kv;
};

static std::vector<IniSection> ParseIni(const std::filesystem::path& path)
{
    std::vector<IniSection> sections;
    std::ifstream f(path);
    if (!f.is_open()) return sections;

    IniSection* cur = nullptr;
    std::string line;
    while (std::getline(f, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[') {
            // Section header
            auto close = line.find(']');
            if (close == std::string::npos) continue;
            sections.push_back({line.substr(1, close - 1), {}});
            cur = &sections.back();
        } else if (cur) {
            // Key = Value
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key   = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            // Trim
            auto ks = key.find_first_not_of(" \t");
            auto ke = key.find_last_not_of(" \t");
            auto vs = value.find_first_not_of(" \t");
            auto ve = value.find_last_not_of(" \t");
            if (ks != std::string::npos && ke != std::string::npos)
                key = key.substr(ks, ke - ks + 1);
            if (vs != std::string::npos && ve != std::string::npos)
                value = value.substr(vs, ve - vs + 1);
            cur->kv.emplace_back(key, value);
        }
    }
    return sections;
}

static std::string GetKv(const IniSection& s, const std::string& key,
                          const std::string& defaultVal = "")
{
    for (const auto& [k, v] : s.kv)
        if (k == key) return v;
    return defaultVal;
}

static bool ReadProfileIni(const std::filesystem::path& path, MappingProfile& outProfile)
{
    auto sections = ParseIni(path);
    if (sections.empty()) {
        CHUCKSTATION5_ERROR("[InputMapping] No sections in INI '%s'.", path.string().c_str());
        return false;
    }

    outProfile.entries.clear();

    for (const auto& sec : sections) {
        if (sec.name == "profile") {
            outProfile.name = GetKv(sec, "name", "Unknown");
        } else if (sec.name.rfind("mapping.", 0) == 0) {
            MappingEntry e{};
            e.target     = Ps5ButtonFromName(GetKv(sec, "target", ""));
            e.sourceType = HostInputTypeFromName(GetKv(sec, "sourceType", "Keyboard"));
            e.sourceId   = static_cast<uint32_t>(std::stoul(GetKv(sec, "sourceId", "0")));
            e.scale      = std::stof(GetKv(sec, "scale", "1.0"));
            e.deadzone   = std::stof(GetKv(sec, "deadzone", "0.0"));
            e.inverted   = (GetKv(sec, "inverted", "false") == "true");

            if (e.target != Ps5Button::Count)
                outProfile.entries.push_back(e);
        }
    }

    CHUCKSTATION5_INFO("[InputMapping] Loaded profile '%s' from %s (%zu mappings).",
              outProfile.name.c_str(), path.string().c_str(), outProfile.entries.size());
    return true;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init()
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);

    std::error_code ec;
    std::filesystem::create_directories(st.profileDir, ec);
    if (ec) {
        CHUCKSTATION5_ERROR("[InputMapping] Failed to create profile directory: %s", ec.message().c_str());
        st.initialised = false;
        return false;
    }

    // Set the default keyboard profile as active
    st.activeProfile = DefaultKeyboardProfile();
    st.initialised = true;

    CHUCKSTATION5_INFO("[InputMapping] Initialised. Profile dir: %s", st.profileDir.string().c_str());
    return true;
}

void Shutdown()
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);
    st.activeProfile.entries.clear();
    st.activeProfile.name.clear();
    st.initialised = false;
    CHUCKSTATION5_INFO("[InputMapping] Shutdown.");
}

// ── Profile management ────────────────────────────────────────────────────
bool LoadProfile(const std::string& name)
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);

    if (!st.initialised) {
        CHUCKSTATION5_ERROR("[InputMapping] Not initialised.");
        return false;
    }

    auto path = st.profileDir / (name + ".ini");
    if (!std::filesystem::exists(path)) {
        CHUCKSTATION5_ERROR("[InputMapping] Profile '%s' not found at %s.", name.c_str(), path.string().c_str());
        return false;
    }

    MappingProfile loaded;
    if (!ReadProfileIni(path, loaded)) return false;

    st.activeProfile = std::move(loaded);
    CHUCKSTATION5_INFO("[InputMapping] Active profile set to '%s'.", st.activeProfile.name.c_str());
    return true;
}

bool SaveProfile(const std::string& name)
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);

    if (!st.initialised) return false;

    auto path = st.profileDir / (name + ".ini");
    MappingProfile toSave = st.activeProfile;
    toSave.name = name;
    return WriteProfileIni(toSave, path);
}

bool DeleteProfile(const std::string& name)
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);

    if (!st.initialised) return false;

    auto path = st.profileDir / (name + ".ini");
    if (!std::filesystem::exists(path)) return false;

    std::error_code ec;
    bool ok = std::filesystem::remove(path, ec);
    if (ok) {
        CHUCKSTATION5_INFO("[InputMapping] Deleted profile '%s'.", name.c_str());
    } else {
        CHUCKSTATION5_ERROR("[InputMapping] Failed to delete profile '%s': %s",
                   name.c_str(), ec.message().c_str());
    }
    return ok;
}

// ── Active profile ────────────────────────────────────────────────────────
const MappingProfile& GetActiveProfile()
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);
    return st.activeProfile;
}

void SetActiveProfile(const MappingProfile& profile)
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);
    st.activeProfile = profile;
    CHUCKSTATION5_INFO("[InputMapping] Active profile changed to '%s' (%zu mappings).",
              profile.name.c_str(), profile.entries.size());
}

std::vector<std::string> ListProfiles()
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);

    std::vector<std::string> result;
    if (!st.initialised) return result;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(st.profileDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ini") {
            result.push_back(entry.path().stem().string());
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

// ── Input translation ─────────────────────────────────────────────────────
bool TranslateButton(HostInputType type, uint32_t sourceId, Ps5Button& outButton)
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);

    if (!st.initialised) return false;

    for (const auto& e : st.activeProfile.entries) {
        if (e.sourceType == type && e.sourceId == sourceId) {
            outButton = e.target;
            return true;
        }
    }
    return false;
}

float TranslateAxis(HostInputType type, uint32_t sourceId, float value)
{
    auto& st = InputMappingState::Get();
    std::lock_guard lk(st.mtx);

    if (!st.initialised) return value;

    for (const auto& e : st.activeProfile.entries) {
        if (e.sourceType == type && e.sourceId == sourceId) {
            // Apply deadzone
            if (std::abs(value) < e.deadzone)
                return 0.0f;

            // Apply inversion
            float result = e.inverted ? -value : value;

            // Apply scale
            result *= e.scale;

            // Re-apply deadzone after scaling (handles cases where scale < 1)
            if (std::abs(result) < e.deadzone)
                return 0.0f;

            // Clamp to [-1, 1]
            if (result > 1.0f)  result = 1.0f;
            if (result < -1.0f) result = -1.0f;

            return result;
        }
    }

    // No mapping found – pass through unmodified
    return value;
}

} // namespace ChuckStation5::InputMapping
