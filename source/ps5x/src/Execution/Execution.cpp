// PS5x – Execution Framework implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/Execution/Execution.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/Loader/Loader.h"
#include "PS5x/Process/Process.h"
#include "PS5x/KernelRuntime/KernelRuntime.h"
#include "PS5x/Filesystem/Filesystem.h"
#include "PS5x/Debugger/Debugger.h"
#include "PS5x/KytyAdapter/KytyAdapter.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace PS5x::Execution {

using Clock = std::chrono::steady_clock;

// ── State ─────────────────────────────────────────────────────────────────
namespace {

struct ExecContext
{
    std::atomic<ExecState>     state{ExecState::Idle};
    std::recursive_mutex       mtx;  // recursive: SetState may be called within callbacks
    uint32_t                   pid        = 0;
    std::string                lastError;
    Clock::time_point          startTime;
    Clock::time_point          loadStart;
    double                     loadTimeMs = 0.0;
    std::atomic<uint64_t>      frames{0};
    std::atomic<uint64_t>      syscalls{0};

    std::vector<StateChangeFn> onStateChange;
    std::vector<ExitFn>        onExit;
    std::vector<FaultFn>       onFault;

    static ExecContext& Get() { static ExecContext c; return c; }
};

void SetState(ExecState next)
{
    auto& ctx = ExecContext::Get();
    ExecState prev = ctx.state.exchange(next);
    if (prev == next) return;

    PS5X_INFO("[Exec] State %s → %s",
              ExecStateName(prev), ExecStateName(next));

    std::lock_guard lk(ctx.mtx);
    for (auto& fn : ctx.onStateChange) fn(prev, next);
}

void SetError(std::string msg)
{
    auto& ctx = ExecContext::Get();
    std::lock_guard lk(ctx.mtx);
    ctx.lastError = std::move(msg);
    PS5X_ERROR("[Exec] %s", ctx.lastError.c_str());
}

double ElapsedMs(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

} // namespace

// ── Name table ────────────────────────────────────────────────────────────
const char* ExecStateName(ExecState s)
{
    switch (s) {
        case ExecState::Idle:       return "Idle";
        case ExecState::Loading:    return "Loading";
        case ExecState::Ready:      return "Ready";
        case ExecState::Running:    return "Running";
        case ExecState::Paused:     return "Paused";
        case ExecState::Exiting:    return "Exiting";
        case ExecState::Terminated: return "Terminated";
        case ExecState::Faulted:    return "Faulted";
    }
    return "?";
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
static ExitInfo  g_exitInfo;
static std::mutex g_exitMtx;

bool Init()
{
    auto& ctx = ExecContext::Get();
    std::lock_guard lk(ctx.mtx);
    ctx.state.store(ExecState::Idle);
    ctx.pid       = 0;
    ctx.lastError.clear();
    ctx.frames.store(0);
    ctx.syscalls.store(0);
    ctx.onStateChange.clear();
    ctx.onExit.clear();
    ctx.onFault.clear();
    // Reset Phase 6 exit info
    { std::lock_guard el(g_exitMtx); g_exitInfo = ExitInfo{}; }
    PS5X_INFO("[Exec] Execution framework initialised.");
    return true;
}

void Shutdown()
{
    if (IsRunning()) ForceTerminate();
    auto& ctx = ExecContext::Get();
    std::lock_guard lk(ctx.mtx);
    ctx.onStateChange.clear();
    ctx.onExit.clear();
    ctx.onFault.clear();
    SetState(ExecState::Idle);
    PS5X_INFO("[Exec] Shutdown.");
}

// ── Program management ────────────────────────────────────────────────────
bool LoadProgram(const std::filesystem::path& elfPath, const LoadOptions& opts)
{
    auto& ctx = ExecContext::Get();

    if (ctx.state.load() != ExecState::Idle &&
        ctx.state.load() != ExecState::Terminated) {
        SetError("LoadProgram: execution already active");
        return false;
    }

    SetState(ExecState::Loading);
    ctx.loadStart = Clock::now();

    // Firmware notice (never bundled)
    if (!opts.firmwarePath.empty()) {
        auto r = Loader::ValidateFirmware(opts.firmwarePath);
        if (r != Loader::LoadResult::Ok) {
            PS5X_WARN("[Exec] Firmware validation: %s (continuing)",
                      Loader::LoadResultStr(r));
        }
    } else {
        PS5X_INFO("[Exec] No firmware path set. "
                  "PS5x does not supply firmware – provide your own.");
    }

    // Mount filesystem points
    if (!opts.contentPath.empty())
        Filesystem::Mount(Filesystem::MountPoint::App0,
                          opts.contentPath, true);
    if (!opts.saveDataPath.empty())
        Filesystem::Mount(Filesystem::MountPoint::SaveData,
                          opts.saveDataPath, false);
    if (!opts.firmwarePath.empty())
        Filesystem::Mount(Filesystem::MountPoint::System,
                          opts.firmwarePath, true);

    // Attach debugger if requested
    if (opts.debuggerAttach)
        Debugger::Init();

    // Create process
    uint32_t pid = Process::Create(elfPath, opts.contentPath, opts.firmwarePath);
    if (pid == 0) {
        SetError("LoadProgram: Process::Create failed for " + elfPath.string());
        SetState(ExecState::Faulted);
        return false;
    }

    {
        std::lock_guard lk(ctx.mtx);
        ctx.pid = pid;
        ctx.loadTimeMs = ElapsedMs(ctx.loadStart);
    }

    // Register exit callback
    Process::RegisterExitCallback([&ctx](uint32_t, int exitCode) {
        {
            std::lock_guard lk(ctx.mtx);
            for (auto& fn : ctx.onExit)
                fn(exitCode, "process exited normally");
        }
        SetState(ExecState::Terminated);
    });

    SetState(ExecState::Ready);

    PS5X_INFO("[Exec] Program loaded in %.2f ms: %s  PID=%u",
              ctx.loadTimeMs, elfPath.filename().string().c_str(), pid);
    return true;
}

bool Start()
{
    auto& ctx = ExecContext::Get();
    if (ctx.state.load() != ExecState::Ready) {
        SetError("Start: not in Ready state");
        return false;
    }

    ctx.startTime = Clock::now();
    ctx.frames.store(0);
    ctx.syscalls.store(0);

    if (!Process::Start(ctx.pid)) {
        SetError("Start: Process::Start failed");
        SetState(ExecState::Faulted);
        return false;
    }

    SetState(ExecState::Running);
    PS5X_INFO("[Exec] Execution started. PID=%u", ctx.pid);
    return true;
}

bool RequestExit(int exitCode)
{
    auto& ctx = ExecContext::Get();
    if (ctx.state.load() != ExecState::Running &&
        ctx.state.load() != ExecState::Paused)
        return false;

    SetState(ExecState::Exiting);
    return Process::RequestExit(ctx.pid, exitCode);
}

bool WaitForExit(uint64_t timeoutUs)
{
    auto& ctx = ExecContext::Get();
    int code = 0;
    bool ok = Process::Wait(ctx.pid, &code, timeoutUs);
    if (ok) {
        std::lock_guard lk(ctx.mtx);
        for (auto& fn : ctx.onExit)
            fn(code, "wait completed");
        SetState(ExecState::Terminated);
    }
    return ok;
}

void ForceTerminate()
{
    auto& ctx = ExecContext::Get();
    uint32_t pid = ctx.pid;
    if (pid) {
        Process::Terminate(pid);
        std::lock_guard lk(ctx.mtx);
        ctx.pid = 0;
    }
    SetState(ExecState::Terminated);
    PS5X_INFO("[Exec] Force terminated.");
}

bool Pause()
{
    auto& ctx = ExecContext::Get();
    if (ctx.state.load() != ExecState::Running) return false;
    Debugger::Pause();
    SetState(ExecState::Paused);
    return true;
}

bool Resume()
{
    auto& ctx = ExecContext::Get();
    if (ctx.state.load() != ExecState::Paused) return false;
    Debugger::Continue();
    SetState(ExecState::Running);
    return true;
}

// ── Queries ───────────────────────────────────────────────────────────────
ExecState GetState() { return ExecContext::Get().state.load(); }
bool      IsRunning() { return GetState() == ExecState::Running; }

ExecStats GetStats()
{
    auto& ctx = ExecContext::Get();
    ExecStats s;
    s.pid              = ctx.pid;
    s.state            = ctx.state.load();
    s.loadTimeMs       = ctx.loadTimeMs;
    s.framesRendered   = ctx.frames.load();
    s.syscallsEmulated = ctx.syscalls.load();
    s.exitCode         = 0;

    if (s.state == ExecState::Running) {
        std::lock_guard lk(ctx.mtx);
        s.uptimeMs = ElapsedMs(ctx.startTime);
    }

    // Pull thread/module/memory counts from subsystems
    auto kStats = KernelRuntime::GetStats();
    s.threadCount = kStats.runningThreads;

    auto mStats = Memory::GetStats();
    s.memoryUsedBytes = mStats.totalCommitted;

    if (ctx.pid) {
        auto mods = Process::GetModules(ctx.pid);
        s.moduleCount = static_cast<uint32_t>(mods.size());
    }

    std::lock_guard lk(ctx.mtx);
    s.lastError = ctx.lastError;
    return s;
}

std::string GetLastError()
{
    auto& ctx = ExecContext::Get();
    std::lock_guard lk(ctx.mtx);
    return ctx.lastError;
}

// ── Callbacks ─────────────────────────────────────────────────────────────
void OnStateChange(StateChangeFn fn)
{
    auto& ctx = ExecContext::Get();
    std::lock_guard lk(ctx.mtx);
    ctx.onStateChange.push_back(std::move(fn));
}

void OnExit(ExitFn fn)
{
    auto& ctx = ExecContext::Get();
    std::lock_guard lk(ctx.mtx);
    ctx.onExit.push_back(std::move(fn));
}

void OnFault(FaultFn fn)
{
    auto& ctx = ExecContext::Get();
    std::lock_guard lk(ctx.mtx);
    ctx.onFault.push_back(std::move(fn));
}

// ── Frame / syscall accounting ────────────────────────────────────────────
void NotifyFrameRendered() { ExecContext::Get().frames.fetch_add(1); }
void NotifySyscall(uint32_t /*sysno*/) { ExecContext::Get().syscalls.fetch_add(1); }

// ── Phase 6 – GuestLoop, ExitInfo, fault reporting ──────────────────────

namespace GuestLoop {

StepResult StepInstruction()
{
    auto& ctx = ExecContext::Get();
    if (ctx.state.load() != ExecState::Paused) {
        PS5X_WARN("[Exec] StepInstruction called outside Paused state.");
        return StepResult::Fault;
    }
    // Delegate to debugger for single-step
    Debugger::StepInto();
    PS5X_TRACE("[Exec] StepInstruction → Ok");
    return StepResult::Ok;
}

void DispatchException(uint8_t vector, uint64_t errorCode)
{
    PS5X_WARN("[Exec] Guest exception vector=%u ec=0x%llx", vector,
              static_cast<unsigned long long>(errorCode));
    RuntimeEvents::PublishFault(errorCode,
        "Guest exception vector=" + std::to_string(vector));
    SetState(ExecState::Faulted);
}

void InjectTrap(uint8_t trapNumber)
{
    PS5X_DEBUG("[Exec] InjectTrap INT%u", trapNumber);
    RuntimeEvents::PublishCustom("trap", "INT" + std::to_string(trapNumber));
}

} // namespace GuestLoop

const char* ExitReasonName(ExitReason r)
{
    switch (r) {
        case ExitReason::Normal:     return "Normal";
        case ExitReason::GuestPanic: return "GuestPanic";
        case ExitReason::Fault:      return "Fault";
        case ExitReason::Timeout:    return "Timeout";
        case ExitReason::Requested:  return "Requested";
        case ExitReason::Unknown:    return "Unknown";
    }
    return "?";
}

ExitInfo GetExitInfo()
{
    std::lock_guard lk(g_exitMtx);
    return g_exitInfo;
}

void ReportFault(uint64_t faultAddr, const std::string& description)
{
    {
        std::lock_guard lk(g_exitMtx);
        g_exitInfo.reason    = ExitReason::Fault;
        g_exitInfo.faultAddr = faultAddr;
        g_exitInfo.message   = description;
    }
    auto& ctx = ExecContext::Get();
    std::lock_guard lk(ctx.mtx);
    for (auto& fn : ctx.onFault) fn(faultAddr, description);
    SetState(ExecState::Faulted);
    RuntimeEvents::PublishFault(faultAddr, description);
    PS5X_ERROR("[Exec] Fault @ 0x%llx: %s",
               static_cast<unsigned long long>(faultAddr), description.c_str());
}

void ReportGuestPanic(const std::string& message)
{
    {
        std::lock_guard lk(g_exitMtx);
        g_exitInfo.reason  = ExitReason::GuestPanic;
        g_exitInfo.message = message;
    }
    SetState(ExecState::Faulted);
    RuntimeEvents::PublishCustom("guest_panic", message);
    PS5X_ERROR("[Exec] Guest panic: %s", message.c_str());
}

} // namespace PS5x::Execution
