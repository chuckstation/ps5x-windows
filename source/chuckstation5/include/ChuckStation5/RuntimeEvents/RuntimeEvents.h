// ChuckStation5 – Runtime Event Queue
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
// Provides a structured runtime event bus:
//   • Guest exception reporting
//   • Exit code propagation
//   • Execution timeline (microsecond-resolution)
//   • Guest watchdog (detects hangs)
//   • Runtime profiling events
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ChuckStation5::RuntimeEvents {

// ── Event types ───────────────────────────────────────────────────────────
enum class EventType : uint8_t
{
    // Lifecycle
    ProcessCreated   = 0,
    ProcessStarted   = 1,
    ProcessExited    = 2,
    ProcessFaulted   = 3,

    // Threads
    ThreadSpawned    = 10,
    ThreadExited     = 11,
    ThreadFaulted    = 12,

    // Modules
    ModuleLoaded     = 20,
    ModuleUnloaded   = 21,
    SymbolResolved   = 22,
    SymbolMissing    = 23,

    // Memory
    PageFault        = 30,
    MemoryProtViolation = 31,
    MemoryLeak       = 32,

    // Rendering
    FrameBegin       = 40,
    FrameEnd         = 41,
    SwapchainResize  = 42,
    ShaderCompiled   = 43,

    // Syscalls
    SyscallEnter     = 50,
    SyscallReturn    = 51,
    SyscallUnknown   = 52,

    // Watchdog
    WatchdogWarning  = 60,
    WatchdogTimeout  = 61,

    // Profiling
    ProfileMark      = 70,
    ProfileBegin     = 71,
    ProfileEnd       = 72,

    // Custom
    Custom           = 255,
};

const char* EventTypeName(EventType t);

// ── Event payload variants ────────────────────────────────────────────────
struct ProcessExitedPayload  { int exitCode; std::string reason; };
struct ProcessFaultedPayload { uint64_t faultAddr; std::string description; };
struct ThreadPayload         { uint32_t threadId; std::string name; int exitCode; };
struct ModulePayload         { uint32_t moduleId; std::string name; uint64_t base; };
struct SymbolPayload         { std::string name; uint64_t address; uint32_t moduleId; };
struct PageFaultPayload      { uint64_t address; bool isWrite; bool isExec; };
struct SyscallPayload        { uint32_t sysno; std::string name; int64_t result; };
struct FramePayload          { uint64_t frameIndex; double gpuMs; double cpuMs; };
struct ProfilePayload        { std::string label; uint64_t durationUs; };
struct CustomPayload         { std::string tag; std::string data; };

using EventPayload = std::variant<
    std::monostate,
    ProcessExitedPayload,
    ProcessFaultedPayload,
    ThreadPayload,
    ModulePayload,
    SymbolPayload,
    PageFaultPayload,
    SyscallPayload,
    FramePayload,
    ProfilePayload,
    CustomPayload
>;

// ── Runtime event ─────────────────────────────────────────────────────────
struct RuntimeEvent
{
    uint64_t     id          = 0;
    EventType    type        = EventType::Custom;
    uint64_t     timestampUs = 0;   ///< microseconds since epoch
    uint32_t     pid         = 0;
    uint32_t     tid         = 0;
    EventPayload payload;
};

// ── Callbacks ─────────────────────────────────────────────────────────────
using EventHandlerFn = std::function<void(const RuntimeEvent&)>;

// ── Watchdog config ───────────────────────────────────────────────────────
struct WatchdogConfig
{
    uint64_t warningUs  = 5'000'000;   ///< 5 seconds
    uint64_t timeoutUs  = 30'000'000;  ///< 30 seconds
    bool     enabled    = false;
};

// ── Timeline entry (for the execution timeline view) ─────────────────────
struct TimelineEntry
{
    uint64_t  timestampUs = 0;
    EventType type        = EventType::Custom;
    std::string label;
    double    durationUs  = 0.0;  ///< non-zero for ranged events
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(size_t ringCapacity = 65536);
void Shutdown();
void Reset();

// ── Publishing ────────────────────────────────────────────────────────────
void Publish(EventType type, EventPayload payload = {}, uint32_t pid = 0, uint32_t tid = 0);

// Convenience helpers
void PublishProcessExit(int exitCode, std::string reason = "");
void PublishFault(uint64_t faultAddr, std::string desc);
void PublishSyscall(uint32_t sysno, const std::string& name, int64_t result);
void PublishFrame(uint64_t frameIdx, double gpuMs, double cpuMs);
void PublishProfileMark(const std::string& label);
void PublishProfileBegin(const std::string& label);
void PublishProfileEnd(const std::string& label, uint64_t durationUs);
void PublishCustom(const std::string& tag, const std::string& data);

// ── Subscription ─────────────────────────────────────────────────────────
/// Register a handler for a specific event type (or all if type == COUNT).
/// Returns a handler ID that can be used to unregister.
uint32_t Subscribe(EventHandlerFn fn, std::optional<EventType> filter = std::nullopt);
void     Unsubscribe(uint32_t handlerId);

// ── Query ─────────────────────────────────────────────────────────────────
/// Return last N events from the ring buffer (newest last).
std::vector<RuntimeEvent>  GetRecent(size_t n = 64);

/// Return events of a specific type.
std::vector<RuntimeEvent>  GetByType(EventType type, size_t maxCount = 256);

/// Return the execution timeline (sorted by timestamp).
std::vector<TimelineEntry> GetTimeline();

uint64_t GetEventCount();  ///< total events published (including dropped)

// ── Watchdog ─────────────────────────────────────────────────────────────
void     ConfigureWatchdog(const WatchdogConfig& cfg);
void     KickWatchdog();   ///< Reset the watchdog timer (call from guest heartbeat)
bool     IsWatchdogArmed();

// ── Profiling ─────────────────────────────────────────────────────────────
/// RAII scope timer that publishes ProfileBegin/End.
struct ScopeTimer
{
    explicit ScopeTimer(std::string label);
    ~ScopeTimer();
private:
    std::string label_;
    std::chrono::steady_clock::time_point t0_;
};





// ── Event categories / filtering ─────────────────────────────────────────
enum class EventCategory : uint8_t
{
    Lifecycle  = 0,
    Thread     = 1,
    Module     = 2,
    Memory     = 3,
    GPU        = 4,
    Audio      = 5,
    Filesystem = 6,
    Scheduler  = 7,
    Syscall    = 8,
    Profile    = 9,
    Custom     = 10,
};
const char*   EventCategoryName(EventCategory c);
EventCategory GetEventCategory(EventType t);

// ── Per-category subscription filter ────────────────────────────────────
uint32_t SubscribeCategory(EventCategory cat, EventHandlerFn fn);

// ── New publish helpers ───────────────────────────────────────────────────
void PublishThread(EventType type, uint32_t tid, const std::string& name, int code = 0);
void PublishGpuEvent(EventType type, uint64_t frameIndex, double gpuMs, double cpuMs);
void PublishAudioEvent(const std::string& tag, const std::string& detail);
void PublishFilesystemEvent(const std::string& path, bool success);
void PublishSchedulerEvent(uint32_t tid, const std::string& detail);

// ── Timeline export ───────────────────────────────────────────────────────
/// Export timeline as JSON string.
std::string ExportTimelineJson(size_t maxEntries = 1024);

/// Export timeline as CSV string.
std::string ExportTimelineCsv(size_t maxEntries = 1024);

// ── Event count by category ───────────────────────────────────────────────
uint64_t GetCategoryCount(EventCategory cat);

} // namespace ChuckStation5::RuntimeEvents
