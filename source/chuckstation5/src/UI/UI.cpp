// ChuckStation5 – UI implementation (Windows-native, Phase 8)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Windows-only. Run() drives a Win32 message pump.
// When CHUCKSTATION5_ENABLE_UI_WINDOW is defined, Dear ImGui (Win32 + DX12 backend)
// is layered on top. Without it the pump runs headless for tests and CI.
// SaveWorkspace/LoadWorkspace use a simple INI format.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ChuckStation5/UI/UI.h"
#include "ChuckStation5/Config/Config.h"
#include "ChuckStation5/Cpu/Cpu.h"
#include "ChuckStation5/PerfTools/PerfTools.h"
#include "ChuckStation5/Logger/Logger.h"
#include <fstream>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ChuckStation5::UI {

// ── Internal state ────────────────────────────────────────────────────────
namespace {

static constexpr size_t kMaxLogLines  = 8192;
static constexpr size_t kMaxRecent    = 20;

struct UIState {
    UIState() {
        for (size_t i = 0; i < panels.size(); ++i) {
            panels[i].panel = static_cast<Panel>(i);
        }
    }

    std::mutex mtx;


    std::atomic<bool>      running{false};
    std::atomic<bool>      emulationActive{false};
    std::atomic<float>     fps{0.f};
    std::string            gameTitle;
    std::string            backendName;
    std::deque<LogLine>    logLines;
    std::vector<UIEventFn> eventCallbacks;
    std::array<PanelState, static_cast<size_t>(Panel::COUNT)> panels;


    bool welcomeEnabled = true;


    std::vector<RecentEntry> recentList;


    FirmwareStatus firmware;


    Theme  theme;
    Color  accentColor{0x00, 0x70, 0xD8};


    std::map<std::string, DockPanel> dockPanels;


    std::deque<std::string> logViewer;


    std::string statusOverlay;

    static UIState& Get() { static UIState s; return s; }
};

} // namespace


const char* PanelName(Panel p) {
    switch (p) {
        case Panel::ExecutionDashboard: return "Execution Dashboard";
        case Panel::RuntimeTimeline:    return "Runtime Timeline";
        case Panel::MemoryInspector:    return "Memory Inspector";
        case Panel::GpuStatistics:      return "GPU Statistics";
        case Panel::EventViewer:        return "Event Viewer";
        case Panel::ControllerMonitor:  return "Controller Monitor";
        case Panel::FilesystemBrowser:  return "Filesystem Browser";
        case Panel::PerformanceGraphs:  return "Performance Graphs";
        case Panel::ThreadInspector:    return "Thread Inspector";
        case Panel::ModuleViewer:       return "Module Viewer";
        case Panel::ShaderCacheViewer:  return "Shader Cache Viewer";
        default: return "Unknown";
    }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(void*) {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    st.running.store(false);
    st.welcomeEnabled = true;
    st.recentList.clear();
    st.firmware = {};
    st.theme = Theme{};
    st.accentColor = {0x00, 0x70, 0xD8};
    st.dockPanels.clear();
    st.logViewer.clear();
    st.statusOverlay.clear();
    CHUCKSTATION5_INFO("[UI] Initialised (Phase 8).");
    return true;
}

void Run() {
    auto& st = UIState::Get();
    st.running.store(true);
    CHUCKSTATION5_INFO("[UI] Run() started.");

#if defined(_WIN32)
    // Win32 message pump.  The ImGui render loop is layered on top of this
    // via the ImGui Win32 + DX12 backend (linked when CHUCKSTATION5_ENABLE_UI_WINDOW
    // is defined at build time).  Without that define the pump runs headless
    // (no visible window) which is the correct mode for tests and CI.
    MSG msg{};
    while (st.running.load()) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { st.running.store(false); break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!st.running.load()) break;
        // Yield between frames to avoid 100% CPU burn in headless mode
        Sleep(1);
    }
    CHUCKSTATION5_INFO("[UI] Win32 message pump exited.");
#else
    // Non-Windows: yield-based headless loop (CI)
    while (st.running.load()) { std::this_thread::yield(); }
#endif

    st.running.store(false);
    CHUCKSTATION5_INFO("[UI] Run() returned.");
}

void RequestExit() { UIState::Get().running.store(false); }

void Shutdown() {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    st.running.store(false);
    CHUCKSTATION5_INFO("[UI] Shut down.");
}


void RegisterEventCallback(UIEventFn fn) {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    st.eventCallbacks.push_back(std::move(fn));
}

void PushLogLine(const LogLine& line) {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    if (st.logLines.size() >= kMaxLogLines) st.logLines.pop_front();
    st.logLines.push_back(line);
}

void SetGameTitle(const std::string& t) { std::lock_guard lk(UIState::Get().mtx); UIState::Get().gameTitle = t; }
void SetEmulationActive(bool a) { UIState::Get().emulationActive.store(a); }
void SetFps(float f) { UIState::Get().fps.store(f); }
void SetBackendName(const std::string& n) { std::lock_guard lk(UIState::Get().mtx); UIState::Get().backendName = n; }

void ShowPanel(Panel p, bool v) {
    auto& st = UIState::Get(); std::lock_guard lk(st.mtx);
    st.panels[static_cast<size_t>(p)].visible = v;
}
void HidePanel(Panel p) { ShowPanel(p, false); }
bool IsPanelVisible(Panel p) {
    auto& st = UIState::Get(); std::lock_guard lk(st.mtx);
    return st.panels[static_cast<size_t>(p)].visible;
}
PanelState GetPanelState(Panel p) {
    auto& st = UIState::Get(); std::lock_guard lk(st.mtx);
    return st.panels[static_cast<size_t>(p)];
}
void SetPanelState(const PanelState& ps) {
    auto& st = UIState::Get(); std::lock_guard lk(st.mtx);
    st.panels[static_cast<size_t>(ps.panel)] = ps;
}
void UpdateExecutionStatus(const ExecutionStatus&) {}
void UpdateGpuStatus(const GpuStatus&) {}
void UpdateMemoryStatus(const MemoryStatus&) {}
bool SaveWorkspace(const std::string& path) {
    if (path.empty()) return false;
    std::ofstream f(path);
    if (!f.is_open()) {
        CHUCKSTATION5_ERROR("[UI] SaveWorkspace: cannot write '%s'", path.c_str());
        return false;
    }
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    f << "# ChuckStation5 UI workspace\n";
    f << "[panels]\n";
    for (size_t i = 0; i < st.panels.size(); ++i) {
        auto& p = st.panels[i];
        f << "panel_" << i << "_visible=" << (p.visible ? "1" : "0") << "\n";
        f << "panel_" << i << "_docked="  << (p.docked  ? "1" : "0") << "\n";
        f << "panel_" << i << "_x="       << p.x << "\n";
        f << "panel_" << i << "_y="       << p.y << "\n";
        f << "panel_" << i << "_w="       << p.width  << "\n";
        f << "panel_" << i << "_h="       << p.height << "\n";
    }
    CHUCKSTATION5_INFO("[UI] Workspace saved to '%s'.", path.c_str());
    return true;
}

bool LoadWorkspace(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream f(path);
    if (!f.is_open()) {
        CHUCKSTATION5_WARN("[UI] LoadWorkspace: '%s' not found — using defaults.", path.c_str());
        return false;
    }
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        // Parse panel_N_field=value
        if (key.substr(0, 6) == "panel_") {
            size_t sep1 = key.find('_', 6);
            if (sep1 == std::string::npos) continue;
            int idx = std::stoi(key.substr(6, sep1 - 6));
            if (idx < 0 || idx >= static_cast<int>(st.panels.size())) continue;
            auto& p = st.panels[idx];
            std::string field = key.substr(sep1 + 1);
            if (field == "visible") p.visible = (value != "0");
            else if (field == "docked") p.docked = (value != "0");
            else if (field == "x") p.x = std::stof(value);
            else if (field == "y") p.y = std::stof(value);
            else if (field == "w") p.width  = std::stof(value);
            else if (field == "h") p.height = std::stof(value);
        }
    }
    CHUCKSTATION5_INFO("[UI] Workspace loaded from '%s'.", path.c_str());
    return true;
}


bool IsWelcomeScreenEnabled() {
    std::lock_guard lk(UIState::Get().mtx);
    return UIState::Get().welcomeEnabled;
}
void SetWelcomeScreenEnabled(bool e) {
    std::lock_guard lk(UIState::Get().mtx);
    UIState::Get().welcomeEnabled = e;
}


std::vector<RecentEntry> GetRecentList() {
    std::lock_guard lk(UIState::Get().mtx);
    return UIState::Get().recentList;
}

void AddRecentEntry(const std::string& path, const std::string& label) {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    // Remove duplicate
    st.recentList.erase(
        std::remove_if(st.recentList.begin(), st.recentList.end(),
            [&](const RecentEntry& e){ return e.path == path; }),
        st.recentList.end());
    // Prepend (most recent first)
    RecentEntry e; e.path = path; e.label = label;
    st.recentList.insert(st.recentList.begin(), e);
    // Cap
    if (st.recentList.size() > kMaxRecent)
        st.recentList.resize(kMaxRecent);
}

void ClearRecentList() {
    std::lock_guard lk(UIState::Get().mtx);
    UIState::Get().recentList.clear();
}


FirmwareStatus GetFirmwareStatus() {
    std::lock_guard lk(UIState::Get().mtx);
    return UIState::Get().firmware;
}
void SetFirmwarePath(const std::string& path) {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    st.firmware.path  = path;
    st.firmware.state = FirmwareState::PathSet;
}
void ClearFirmwarePath() {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    st.firmware = {};
}


void SetTheme(const std::string& name) {
    std::lock_guard lk(UIState::Get().mtx);
    UIState::Get().theme.name = name;
    CHUCKSTATION5_DEBUG("[UI] Theme set to '%s'.", name.c_str());
}
Theme GetCurrentTheme() {
    std::lock_guard lk(UIState::Get().mtx);
    return UIState::Get().theme;
}
void SetAccentColor(uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard lk(UIState::Get().mtx);
    UIState::Get().accentColor = {r, g, b};
}
Color GetAccentColor() {
    std::lock_guard lk(UIState::Get().mtx);
    return UIState::Get().accentColor;
}


void SetDockPanel(const DockPanel& panel) {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    st.dockPanels[panel.id] = panel;
}

std::optional<DockPanel> GetDockPanel(const std::string& id) {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.dockPanels.find(id);
    if (it == st.dockPanels.end()) return std::nullopt;
    return it->second;
}

void SaveLayout() {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    Config::Set("ui.welcome_enabled", st.welcomeEnabled ? "1" : "0");
    Config::Set("ui.theme", st.theme.name);
    Config::Set("ui.accent_r", std::to_string(st.accentColor.r));
    Config::Set("ui.accent_g", std::to_string(st.accentColor.g));
    Config::Set("ui.accent_b", std::to_string(st.accentColor.b));
    // Persist dock panels
    int i = 0;
    for (auto& [id, p] : st.dockPanels) {
        std::string prefix = "ui.dock." + std::to_string(i++) + ".";
        Config::Set(prefix + "id",      p.id);
        Config::Set(prefix + "docked",  p.docked  ? "1" : "0");
        Config::Set(prefix + "visible", p.visible ? "1" : "0");
        Config::Set(prefix + "side",    std::to_string(static_cast<int>(p.side)));
        Config::Set(prefix + "size",    std::to_string(p.size));
    }
    Config::Set("ui.dock.count", std::to_string(i));
    CHUCKSTATION5_DEBUG("[UI] Layout saved.");
}

void LoadLayout() {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    auto welcome = Config::Get("ui.welcome_enabled");
    if (!welcome.empty()) st.welcomeEnabled = (welcome != "0");
    auto theme = Config::Get("ui.theme");
    if (!theme.empty()) st.theme.name = theme;
    auto r = Config::Get("ui.accent_r"); if (!r.empty()) st.accentColor.r = static_cast<uint8_t>(std::stoi(r));
    auto g = Config::Get("ui.accent_g"); if (!g.empty()) st.accentColor.g = static_cast<uint8_t>(std::stoi(g));
    auto b = Config::Get("ui.accent_b"); if (!b.empty()) st.accentColor.b = static_cast<uint8_t>(std::stoi(b));
    auto countStr = Config::Get("ui.dock.count");
    if (!countStr.empty()) {
        int count = std::stoi(countStr);
        for (int i = 0; i < count; ++i) {
            std::string prefix = "ui.dock." + std::to_string(i) + ".";
            DockPanel p;
            p.id      = Config::Get(prefix + "id");
            p.docked  = Config::Get(prefix + "docked")  != "0";
            p.visible = Config::Get(prefix + "visible") != "0";
            auto side = Config::Get(prefix + "side");
            if (!side.empty()) p.side = static_cast<DockSide>(std::stoi(side));
            auto size = Config::Get(prefix + "size");
            if (!size.empty()) p.size = std::stoi(size);
            if (!p.id.empty()) st.dockPanels[p.id] = p;
        }
    }
    CHUCKSTATION5_DEBUG("[UI] Layout loaded.");
}


void ClearLogViewer() {
    std::lock_guard lk(UIState::Get().mtx);
    UIState::Get().logViewer.clear();
}

void IngestLogLine(const std::string& line) {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    if (st.logViewer.size() >= kMaxLogLines) st.logViewer.pop_front();
    st.logViewer.push_back(line);
}

std::vector<std::string> SearchLogViewer(const std::string& query, bool caseSensitive) {
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    if (query.empty()) return std::vector<std::string>(st.logViewer.begin(), st.logViewer.end());
    std::vector<std::string> out;
    std::string qLower = query;
    if (!caseSensitive) std::transform(qLower.begin(), qLower.end(), qLower.begin(), ::tolower);
    for (auto& line : st.logViewer) {
        std::string haystack = line;
        if (!caseSensitive) std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
        if (haystack.find(qLower) != std::string::npos) out.push_back(line);
    }
    return out;
}

std::vector<std::string> FilterLogByLevel(LogLevel level) {
    const char* prefix = nullptr;
    switch (level) {
        case LogLevel::Debug: prefix = "[DEBUG]"; break;
        case LogLevel::Info:  prefix = "[INFO]";  break;
        case LogLevel::Warn:  prefix = "[WARN]";  break;
        case LogLevel::Error: prefix = "[ERROR]"; break;
    }
    auto& st = UIState::Get();
    std::lock_guard lk(st.mtx);
    std::vector<std::string> out;
    for (auto& line : st.logViewer) {
        if (prefix && line.find(prefix) != std::string::npos) out.push_back(line);
    }
    return out;
}

size_t MaxLogLines() { return kMaxLogLines; }


float GetDashboardFPS() {
    auto stats = PerfTools::GetFrameStats();
    if (stats.samples == 0 || stats.avgMs <= 0.0) return 0.0f;
    return static_cast<float>(1000.0 / stats.avgMs);
}

float GetDashboardCPUUsage() {
    // Approximation: ratio of CPU section time to frame time
    auto fs = PerfTools::GetFrameStats();
    if (fs.samples == 0 || fs.avgMs <= 0.0) return 0.0f;
    auto sections = PerfTools::GetSectionStats();
    double cpuMs = 0.0;
    for (auto& s : sections) {
        if (s.name == "CPU" || s.name.find("cpu") != std::string::npos)
            cpuMs += s.totalMs / std::max<uint64_t>(s.callCount, 1);
    }
    float pct = static_cast<float>(cpuMs / fs.avgMs * 100.0);
    return std::min(pct, 100.0f);
}

CPUDashStats GetDashboardCPUStats() {
    auto s = Cpu::GetStats();
    CPUDashStats out;
    out.instructionsExecuted = s.instructionsExecuted;
    out.syscallsDispatched   = s.syscallsDispatched;
    out.faults               = s.faults;
    return out;
}

void SetStatusOverlay(const std::string& text) {
    std::lock_guard lk(UIState::Get().mtx);
    UIState::Get().statusOverlay = text;
}

std::string GetStatusOverlay() {
    std::lock_guard lk(UIState::Get().mtx);
    return UIState::Get().statusOverlay;
}

} // namespace ChuckStation5::UI
