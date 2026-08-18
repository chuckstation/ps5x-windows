// ChuckStation5 – Runtime Event Queue implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "ChuckStation5/RuntimeEvents/RuntimeEvents.h"
#include "ChuckStation5/Logger/Logger.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace ChuckStation5::RuntimeEvents {

using Clock = std::chrono::steady_clock;

static uint64_t NowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
}

// ── State ─────────────────────────────────────────────────────────────────
namespace {

struct HandlerEntry
{
    uint32_t              id;
    EventHandlerFn        fn;
    std::optional<EventType> filter;
};

struct WatchdogState
{
    WatchdogConfig  cfg;
    std::atomic<uint64_t> lastKickUs{0};
    std::thread     thread;
    std::atomic<bool> running{false};
};

struct EventState
{
    std::deque<RuntimeEvent>   ring;
    size_t                     capacity  = 65536;
    std::atomic<uint64_t>      totalCount{0};
    std::atomic<uint64_t>      nextId{1};
    std::mutex                 ringMtx;

    std::vector<HandlerEntry>  handlers;
    std::atomic<uint32_t>      nextHandlerId{1};
    std::mutex                 handlerMtx;

    std::vector<TimelineEntry> timeline;
    std::mutex                 timelineMtx;

    WatchdogState              watchdog;
    std::unordered_map<std::string, uint64_t> profileBeginTimes;
    std::mutex                 profileMtx;

    static EventState& Get() { static EventState s; return s; }
};

void DispatchToHandlers(const RuntimeEvent& ev)
{
    auto& es = EventState::Get();
    std::lock_guard lk(es.handlerMtx);
    for (const auto& h : es.handlers) {
        if (!h.filter || *h.filter == ev.type)
            h.fn(ev);
    }
}

void AppendTimeline(const RuntimeEvent& ev, const std::string& label, double durUs = 0.0)
{
    auto& es = EventState::Get();
    std::lock_guard lk(es.timelineMtx);
    es.timeline.push_back({ev.timestampUs, ev.type, label, durUs});
}

RuntimeEvent MakeEvent(EventType type, EventPayload payload, uint32_t pid, uint32_t tid)
{
    auto& es = EventState::Get();
    RuntimeEvent ev;
    ev.id          = es.nextId.fetch_add(1, std::memory_order_relaxed);
    ev.type        = type;
    ev.timestampUs = NowUs();
    ev.pid         = pid;
    ev.tid         = tid;
    ev.payload     = std::move(payload);
    return ev;
}

} // namespace

// ── Name table ────────────────────────────────────────────────────────────
const char* EventTypeName(EventType t)
{
    switch (t) {
        case EventType::ProcessCreated:      return "ProcessCreated";
        case EventType::ProcessStarted:      return "ProcessStarted";
        case EventType::ProcessExited:       return "ProcessExited";
        case EventType::ProcessFaulted:      return "ProcessFaulted";
        case EventType::ThreadSpawned:       return "ThreadSpawned";
        case EventType::ThreadExited:        return "ThreadExited";
        case EventType::ThreadFaulted:       return "ThreadFaulted";
        case EventType::ModuleLoaded:        return "ModuleLoaded";
        case EventType::ModuleUnloaded:      return "ModuleUnloaded";
        case EventType::SymbolResolved:      return "SymbolResolved";
        case EventType::SymbolMissing:       return "SymbolMissing";
        case EventType::PageFault:           return "PageFault";
        case EventType::MemoryProtViolation: return "MemoryProtViolation";
        case EventType::MemoryLeak:          return "MemoryLeak";
        case EventType::FrameBegin:          return "FrameBegin";
        case EventType::FrameEnd:            return "FrameEnd";
        case EventType::SwapchainResize:     return "SwapchainResize";
        case EventType::ShaderCompiled:      return "ShaderCompiled";
        case EventType::SyscallEnter:        return "SyscallEnter";
        case EventType::SyscallReturn:       return "SyscallReturn";
        case EventType::SyscallUnknown:      return "SyscallUnknown";
        case EventType::WatchdogWarning:     return "WatchdogWarning";
        case EventType::WatchdogTimeout:     return "WatchdogTimeout";
        case EventType::ProfileMark:         return "ProfileMark";
        case EventType::ProfileBegin:        return "ProfileBegin";
        case EventType::ProfileEnd:          return "ProfileEnd";
        case EventType::Custom:              return "Custom";
    }
    return "Unknown";
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(size_t ringCapacity)
{
    auto& es = EventState::Get();
    {
        std::lock_guard lk(es.ringMtx);
        es.ring.clear();
        es.capacity = ringCapacity;
        es.totalCount.store(0);
        es.nextId.store(1);
    }
    {
        std::lock_guard lk(es.handlerMtx);
        es.handlers.clear();
    }
    {
        std::lock_guard lk(es.timelineMtx);
        es.timeline.clear();
    }
    CHUCKSTATION5_INFO("[Events] Initialised. Ring capacity=%zu", ringCapacity);
    return true;
}

void Shutdown()
{
    auto& es = EventState::Get();
    // Stop watchdog
    if (es.watchdog.running.load()) {
        es.watchdog.running.store(false);
        if (es.watchdog.thread.joinable())
            es.watchdog.thread.join();
    }
    {
        std::lock_guard lk(es.handlerMtx);
        es.handlers.clear();
    }
    CHUCKSTATION5_INFO("[Events] Shutdown. Total events published: %llu",
              static_cast<unsigned long long>(es.totalCount.load()));
}

void Reset()
{
    auto& es = EventState::Get();
    std::lock_guard lr(es.ringMtx);
    std::lock_guard lh(es.handlerMtx);
    std::lock_guard lt(es.timelineMtx);
    es.ring.clear();
    es.timeline.clear();
    es.totalCount.store(0);
    es.nextId.store(1);
    // Keep handlers – they may be long-lived
}

// ── Publishing ────────────────────────────────────────────────────────────
void Publish(EventType type, EventPayload payload, uint32_t pid, uint32_t tid)
{
    auto& es = EventState::Get();
    auto ev = MakeEvent(type, std::move(payload), pid, tid);
    es.totalCount.fetch_add(1, std::memory_order_relaxed);

    {
        std::lock_guard lk(es.ringMtx);
        if (es.ring.size() >= es.capacity)
            es.ring.pop_front();
        es.ring.push_back(ev);
    }

    // Timeline append for certain categories
    switch (type) {
        case EventType::FrameBegin: case EventType::FrameEnd:
        case EventType::ModuleLoaded: case EventType::ProcessStarted:
        case EventType::ProcessExited: case EventType::ShaderCompiled:
        case EventType::WatchdogWarning: case EventType::WatchdogTimeout:
        case EventType::ProfileMark: case EventType::ProfileEnd:
            AppendTimeline(ev, EventTypeName(type));
            break;
        default: break;
    }

    DispatchToHandlers(ev);
}

void PublishProcessExit(int exitCode, std::string reason)
{
    CHUCKSTATION5_INFO("[Events] Process exit: code=%d %s", exitCode, reason.c_str());
    Publish(EventType::ProcessExited,
            ProcessExitedPayload{exitCode, std::move(reason)});
}

void PublishFault(uint64_t faultAddr, std::string desc)
{
    CHUCKSTATION5_ERROR("[Events] Fault @ 0x%llx: %s",
               static_cast<unsigned long long>(faultAddr), desc.c_str());
    Publish(EventType::ProcessFaulted,
            ProcessFaultedPayload{faultAddr, std::move(desc)});
}

void PublishSyscall(uint32_t sysno, const std::string& name, int64_t result)
{
    Publish(EventType::SyscallReturn,
            SyscallPayload{sysno, name, result});
}

void PublishFrame(uint64_t frameIdx, double gpuMs, double cpuMs)
{
    Publish(EventType::FrameEnd,
            FramePayload{frameIdx, gpuMs, cpuMs});
}

void PublishProfileMark(const std::string& label)
{
    Publish(EventType::ProfileMark, ProfilePayload{label, 0});
    AppendTimeline(MakeEvent(EventType::ProfileMark, {}, 0, 0), label);
}

void PublishProfileBegin(const std::string& label)
{
    auto& es = EventState::Get();
    std::lock_guard lk(es.profileMtx);
    es.profileBeginTimes[label] = NowUs();
    Publish(EventType::ProfileBegin, ProfilePayload{label, 0});
}

void PublishProfileEnd(const std::string& label, uint64_t durationUs)
{
    auto& es = EventState::Get();
    uint64_t dur = durationUs;
    {
        std::lock_guard lk(es.profileMtx);
        auto it = es.profileBeginTimes.find(label);
        if (it != es.profileBeginTimes.end() && durationUs == 0) {
            dur = NowUs() - it->second;
            es.profileBeginTimes.erase(it);
        }
    }
    Publish(EventType::ProfileEnd, ProfilePayload{label, dur});
}

void PublishCustom(const std::string& tag, const std::string& data)
{
    Publish(EventType::Custom, CustomPayload{tag, data});
}

// ── Subscription ─────────────────────────────────────────────────────────
uint32_t Subscribe(EventHandlerFn fn, std::optional<EventType> filter)
{
    auto& es = EventState::Get();
    std::lock_guard lk(es.handlerMtx);
    uint32_t id = es.nextHandlerId.fetch_add(1);
    es.handlers.push_back({id, std::move(fn), filter});
    return id;
}

void Unsubscribe(uint32_t handlerId)
{
    auto& es = EventState::Get();
    std::lock_guard lk(es.handlerMtx);
    es.handlers.erase(
        std::remove_if(es.handlers.begin(), es.handlers.end(),
            [handlerId](const HandlerEntry& h){ return h.id == handlerId; }),
        es.handlers.end());
}

// ── Query ─────────────────────────────────────────────────────────────────
std::vector<RuntimeEvent> GetRecent(size_t n)
{
    auto& es = EventState::Get();
    std::lock_guard lk(es.ringMtx);
    std::vector<RuntimeEvent> out;
    size_t start = es.ring.size() > n ? es.ring.size() - n : 0;
    out.assign(es.ring.begin() + static_cast<ptrdiff_t>(start), es.ring.end());
    return out;
}

std::vector<RuntimeEvent> GetByType(EventType type, size_t maxCount)
{
    auto& es = EventState::Get();
    std::lock_guard lk(es.ringMtx);
    std::vector<RuntimeEvent> out;
    for (auto it = es.ring.rbegin(); it != es.ring.rend() && out.size() < maxCount; ++it)
        if (it->type == type) out.push_back(*it);
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<TimelineEntry> GetTimeline()
{
    auto& es = EventState::Get();
    std::lock_guard lk(es.timelineMtx);
    return es.timeline;
}

uint64_t GetEventCount()
{
    return EventState::Get().totalCount.load();
}

// ── Watchdog ─────────────────────────────────────────────────────────────
void ConfigureWatchdog(const WatchdogConfig& cfg)
{
    auto& es = EventState::Get();

    // Stop existing watchdog
    if (es.watchdog.running.load()) {
        es.watchdog.running.store(false);
        if (es.watchdog.thread.joinable())
            es.watchdog.thread.join();
    }

    es.watchdog.cfg = cfg;
    if (!cfg.enabled) return;

    es.watchdog.lastKickUs.store(NowUs());
    es.watchdog.running.store(true);

    es.watchdog.thread = std::thread([&wd = es.watchdog]() {
        while (wd.running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!wd.running.load()) break;

            uint64_t elapsed = NowUs() - wd.lastKickUs.load();
            if (elapsed >= wd.cfg.timeoutUs) {
                Publish(EventType::WatchdogTimeout,
                        CustomPayload{"watchdog", "guest timeout"});
                CHUCKSTATION5_ERROR("[Events] Watchdog TIMEOUT after %llu ms",
                           static_cast<unsigned long long>(elapsed / 1000));
                break;
            } else if (elapsed >= wd.cfg.warningUs) {
                Publish(EventType::WatchdogWarning,
                        CustomPayload{"watchdog", "guest unresponsive"});
                CHUCKSTATION5_WARN("[Events] Watchdog warning: guest unresponsive for %llu ms",
                          static_cast<unsigned long long>(elapsed / 1000));
            }
        }
    });
    CHUCKSTATION5_INFO("[Events] Watchdog armed: warning=%llums timeout=%llums",
              static_cast<unsigned long long>(cfg.warningUs / 1000),
              static_cast<unsigned long long>(cfg.timeoutUs / 1000));
}

void KickWatchdog()
{
    EventState::Get().watchdog.lastKickUs.store(NowUs());
}

bool IsWatchdogArmed()
{
    return EventState::Get().watchdog.running.load();
}

// ── RAII scope timer ──────────────────────────────────────────────────────
ScopeTimer::ScopeTimer(std::string label)
    : label_(std::move(label)), t0_(Clock::now())
{
    PublishProfileBegin(label_);
}

ScopeTimer::~ScopeTimer()
{
    uint64_t dur = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - t0_).count());
    PublishProfileEnd(label_, dur);
}




namespace {

// Category counters
std::array<std::atomic<uint64_t>, 11> g_catCounts{};

// Helper: categorise an event type
EventCategory Categorise(EventType t)
{
    using ET = EventType;
    using EC = EventCategory;
    switch (t) {
        case ET::ProcessCreated: case ET::ProcessStarted:
        case ET::ProcessExited:  case ET::ProcessFaulted:
            return EC::Lifecycle;
        case ET::ThreadSpawned:  case ET::ThreadExited: case ET::ThreadFaulted:
            return EC::Thread;
        case ET::ModuleLoaded:   case ET::ModuleUnloaded:
        case ET::SymbolResolved: case ET::SymbolMissing:
            return EC::Module;
        case ET::PageFault: case ET::MemoryProtViolation: case ET::MemoryLeak:
            return EC::Memory;
        case ET::FrameBegin: case ET::FrameEnd:
        case ET::SwapchainResize: case ET::ShaderCompiled:
            return EC::GPU;
        case ET::SyscallEnter: case ET::SyscallReturn: case ET::SyscallUnknown:
            return EC::Syscall;
        case ET::WatchdogWarning: case ET::WatchdogTimeout:
            return EC::Lifecycle;
        case ET::ProfileMark: case ET::ProfileBegin: case ET::ProfileEnd:
            return EC::Profile;
        case ET::Custom:
            return EC::Custom;
    }
    return EC::Custom;
}

} // namespace (Phase 6)

const char* EventCategoryName(EventCategory c)
{
    switch (c) {
        case EventCategory::Lifecycle:  return "Lifecycle";
        case EventCategory::Thread:     return "Thread";
        case EventCategory::Module:     return "Module";
        case EventCategory::Memory:     return "Memory";
        case EventCategory::GPU:        return "GPU";
        case EventCategory::Audio:      return "Audio";
        case EventCategory::Filesystem: return "Filesystem";
        case EventCategory::Scheduler:  return "Scheduler";
        case EventCategory::Syscall:    return "Syscall";
        case EventCategory::Profile:    return "Profile";
        case EventCategory::Custom:     return "Custom";
    }
    return "?";
}

EventCategory GetEventCategory(EventType t) { return Categorise(t); }

uint32_t SubscribeCategory(EventCategory cat, EventHandlerFn fn)
{
    return Subscribe([cat, fn = std::move(fn)](const RuntimeEvent& ev){
        if (Categorise(ev.type) == cat) fn(ev);
    });
}

void PublishThread(EventType type, uint32_t tid, const std::string& name, int code)
{
    ThreadPayload p{tid, name, code};
    Publish(type, std::move(p), 0, tid);
    g_catCounts[static_cast<size_t>(EventCategory::Thread)].fetch_add(1);
}

void PublishGpuEvent(EventType type, uint64_t frameIndex, double gpuMs, double cpuMs)
{
    FramePayload p{frameIndex, gpuMs, cpuMs};
    Publish(type, std::move(p));
    g_catCounts[static_cast<size_t>(EventCategory::GPU)].fetch_add(1);
}

void PublishAudioEvent(const std::string& tag, const std::string& detail)
{
    CustomPayload p{"audio:" + tag, detail};
    Publish(EventType::Custom, std::move(p));
    g_catCounts[static_cast<size_t>(EventCategory::Audio)].fetch_add(1);
}

void PublishFilesystemEvent(const std::string& path, bool success)
{
    CustomPayload p{"fs", path + (success ? ":ok" : ":fail")};
    Publish(EventType::Custom, std::move(p));
    g_catCounts[static_cast<size_t>(EventCategory::Filesystem)].fetch_add(1);
}

void PublishSchedulerEvent(uint32_t tid, const std::string& detail)
{
    ThreadPayload p{tid, detail, 0};
    Publish(EventType::ThreadSpawned, std::move(p), 0, tid);
    g_catCounts[static_cast<size_t>(EventCategory::Scheduler)].fetch_add(1);
}

uint64_t GetCategoryCount(EventCategory cat)
{
    auto idx = static_cast<size_t>(cat);
    if (idx >= g_catCounts.size()) return 0;
    return g_catCounts[idx].load();
}

std::string ExportTimelineJson(size_t maxEntries)
{
    auto entries = GetTimeline();
    if (entries.size() > maxEntries)
        entries.erase(entries.begin(), entries.end() - static_cast<ptrdiff_t>(maxEntries));

    std::string out = "[\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& e = entries[i];
        out += "  {\"ts\":" + std::to_string(e.timestampUs)
             + ",\"type\":\"" + EventTypeName(e.type) + "\""
             + ",\"label\":\"" + e.label + "\""
             + ",\"dur\":" + std::to_string(e.durationUs) + "}";
        if (i + 1 < entries.size()) out += ',';
        out += '\n';
    }
    out += "]\n";
    return out;
}

std::string ExportTimelineCsv(size_t maxEntries)
{
    auto entries = GetTimeline();
    if (entries.size() > maxEntries)
        entries.erase(entries.begin(), entries.end() - static_cast<ptrdiff_t>(maxEntries));

    std::string out = "timestamp_us,type,label,duration_us\n";
    for (auto& e : entries) {
        out += std::to_string(e.timestampUs) + ','
             + EventTypeName(e.type) + ','
             + '"' + e.label + '"' + ','
             + std::to_string(e.durationUs) + '\n';
    }
    return out;
}

} // namespace ChuckStation5::RuntimeEvents
