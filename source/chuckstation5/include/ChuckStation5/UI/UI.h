// ChuckStation5 – UI module (Phase 8 polished)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors


//   Welcome screen toggle, recent homebrew list, firmware manager,
//   theme customisation + accent colour, dock layout persistence,
//   searchable log viewer with level filtering, performance dashboard.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ChuckStation5::UI {

// ── Colour palette ────────────────────────────────────────────────────────
namespace Palette {
    inline constexpr uint32_t BgDark        = 0x0C0F1FFF;
    inline constexpr uint32_t BgMid         = 0x111828FF;
    inline constexpr uint32_t BgLight       = 0x1A2340FF;
    inline constexpr uint32_t AccentBlue    = 0x0070D8FF;
    inline constexpr uint32_t AccentBlueDim = 0x004EA8FF;
    inline constexpr uint32_t TextPrimary   = 0xF0F4FFFF;
    inline constexpr uint32_t TextSecondary = 0x8899AAFF;
    inline constexpr uint32_t TextError     = 0xFF4444FF;
    inline constexpr uint32_t TextWarn      = 0xFFAA22FF;
    inline constexpr uint32_t Separator     = 0x263050FF;
}

// ── UI events ─────────────────────────────────────────────────────────────
enum class UIEvent : uint8_t {
    LoadElf, SetFirmwarePath, StartEmulation, StopEmulation,
    OpenSettings, OpenDebugger, Quit,
};
using UIEventFn = std::function<void(UIEvent, const std::string&)>;

// ── Log line ──────────────────────────────────────────────────────────────
struct LogLine {
    uint8_t     level   = 2;
    std::string tag;
    std::string message;
};


enum class Panel : uint8_t {
    ExecutionDashboard=0, RuntimeTimeline, MemoryInspector, GpuStatistics,
    EventViewer, ControllerMonitor, FilesystemBrowser, PerformanceGraphs,
    ThreadInspector, ModuleViewer, ShaderCacheViewer, COUNT,
};
const char* PanelName(Panel p);

struct PanelState {
    Panel panel = Panel::ExecutionDashboard;
    bool  visible=true, docked=false;
    float x=0,y=0,width=400,height=300;
};

struct ExecutionStatus {
    std::string state;
    uint64_t    frameIndex=0, syscalls=0, faultAddr=0;
    std::string faultDesc;
    double      uptimeMs=0;
};
struct GpuStatus {
    uint64_t submits=0,flips=0,barriers=0,fences=0;
    uint32_t activeQueues=0;
    double   gpuFrameMs=0;
};
struct MemoryStatus {
    size_t   allocatedBytes=0, peakBytes=0;
    uint64_t allocCount=0, freeCount=0;
};


enum class FirmwareState : uint8_t { NotProvided, PathSet, Validated, Invalid };
struct FirmwareStatus {
    FirmwareState state = FirmwareState::NotProvided;
    std::string   path;
};


struct RecentEntry {
    std::string path;
    std::string label;
    uint64_t    lastOpenedMs = 0;
};


struct Theme { std::string name = "Dark"; };
struct Color { uint8_t r=0, g=0, b=0; };


enum class DockSide : uint8_t { Left, Right, Top, Bottom, Float };
struct DockPanel {
    std::string id;
    bool        docked  = false;
    bool        visible = true;
    DockSide    side    = DockSide::Right;
    int         size    = 300;
};


enum class LogLevel : uint8_t { Debug=0, Info, Warn, Error };


struct CPUDashStats {
    uint64_t instructionsExecuted = 0;
    uint64_t syscallsDispatched   = 0;
    uint64_t faults               = 0;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(void* nativeWindowHandle = nullptr);
void Run();
void RequestExit();
void Shutdown();


void RegisterEventCallback(UIEventFn fn);
void PushLogLine(const LogLine& line);
void SetGameTitle(const std::string& title);
void SetEmulationActive(bool active);
void SetFps(float fps);
void SetBackendName(const std::string& name);
void ShowPanel(Panel p, bool visible = true);
void HidePanel(Panel p);
bool IsPanelVisible(Panel p);
PanelState  GetPanelState(Panel p);
void        SetPanelState(const PanelState& state);
void        UpdateExecutionStatus(const ExecutionStatus& s);
void        UpdateGpuStatus(const GpuStatus& s);
void        UpdateMemoryStatus(const MemoryStatus& s);
bool        SaveWorkspace(const std::string& path);
bool        LoadWorkspace(const std::string& path);


bool IsWelcomeScreenEnabled();
void SetWelcomeScreenEnabled(bool enabled);


std::vector<RecentEntry> GetRecentList();
void AddRecentEntry(const std::string& path, const std::string& label);
void ClearRecentList();


FirmwareStatus GetFirmwareStatus();
void           SetFirmwarePath(const std::string& path);
void           ClearFirmwarePath();


void  SetTheme(const std::string& name);
Theme GetCurrentTheme();
void  SetAccentColor(uint8_t r, uint8_t g, uint8_t b);
Color GetAccentColor();


void                    SetDockPanel(const DockPanel& panel);
std::optional<DockPanel> GetDockPanel(const std::string& id);
void                    SaveLayout();
void                    LoadLayout();


void                     ClearLogViewer();
void                     IngestLogLine(const std::string& line);
std::vector<std::string> SearchLogViewer(const std::string& query,
                                          bool caseSensitive = false);
std::vector<std::string> FilterLogByLevel(LogLevel level);
size_t                   MaxLogLines();


float         GetDashboardFPS();
float         GetDashboardCPUUsage();
CPUDashStats  GetDashboardCPUStats();
void          SetStatusOverlay(const std::string& text);
std::string   GetStatusOverlay();

} // namespace ChuckStation5::UI
