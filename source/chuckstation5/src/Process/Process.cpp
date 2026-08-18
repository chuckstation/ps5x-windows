// ChuckStation5 – Process Manager implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "ChuckStation5/Process/Process.h"
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/Filesystem/Filesystem.h"
#include "ChuckStation5/KernelRuntime/KernelRuntime.h"
#include "ChuckStation5/Syscalls/Syscalls.h"
#include "ChuckStation5/Cpu/Cpu.h"
#include "ChuckStation5/Loader/Loader.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ChuckStation5::Process {

using Clock = std::chrono::steady_clock;

static uint64_t NowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
}

// ── State ─────────────────────────────────────────────────────────────────
namespace {

struct ProcessEntry {
    ProcessInfo                  info;
    KernelRuntime::KHandle       mainThreadHandle = KernelRuntime::INVALID_HANDLE;
    std::atomic<ProcessState>    state{ProcessState::None};
    std::mutex                   mtx;
};

struct ProcState {
    std::mutex                                   mtx;
    std::unordered_map<uint32_t, std::unique_ptr<ProcessEntry>> procs;
    std::atomic<uint32_t>                        nextPid{1};
    std::atomic<uint32_t>                        currentPid{0};
    std::vector<ExitCallbackFn>                  exitCallbacks;

    static ProcState& Get() { static ProcState s; return s; }
};

void FireExitCallbacks(uint32_t pid, int code)
{
    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    for (auto& cb : ps.exitCallbacks) cb(pid, code);
}

} // namespace

// ── Name helpers ──────────────────────────────────────────────────────────
const char* ProcessStateName(ProcessState s)
{
    switch (s) {
        case ProcessState::None:       return "None";
        case ProcessState::Created:    return "Created";
        case ProcessState::Loading:    return "Loading";
        case ProcessState::Running:    return "Running";
        case ProcessState::Exiting:    return "Exiting";
        case ProcessState::Terminated: return "Terminated";
        case ProcessState::Faulted:    return "Faulted";
    }
    return "?";
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init()
{
    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    ps.procs.clear();
    ps.exitCallbacks.clear();  // clear dangling callbacks from previous test runs
    ps.nextPid.store(1);
    ps.currentPid.store(0);
    CHUCKSTATION5_INFO("[Process] Manager initialised.");
    return true;
}

void Shutdown()
{
    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    for (auto& [pid, e] : ps.procs) {
        if (e->state.load() == ProcessState::Running) {
            CHUCKSTATION5_WARN("[Process] Terminating running process PID=%u at shutdown", pid);
        }
    }
    ps.procs.clear();
    ps.exitCallbacks.clear();
    ps.currentPid.store(0);
    CHUCKSTATION5_INFO("[Process] Shutdown.");
}

// ── Process management ────────────────────────────────────────────────────
uint32_t Create(const std::filesystem::path& elfPath,
                const std::filesystem::path& contentPath,
                const std::filesystem::path& firmwarePath)
{
    CHUCKSTATION5_INFO("[Process] Create: %s", elfPath.string().c_str());

    // 1. Firmware validation (user must supply – ChuckStation5 never bundles it)
    if (auto r = Loader::ValidateFirmware(firmwarePath);
        r != Loader::LoadResult::Ok && !firmwarePath.empty())
    {
        CHUCKSTATION5_WARN("[Process] Firmware warning: %s",
                  Loader::LoadResultStr(r));
    }

    // 2. Mount /app0 → contentPath
    if (!contentPath.empty())
        Filesystem::Mount(Filesystem::MountPoint::App0, contentPath, true);
    if (!firmwarePath.empty())
        Filesystem::Mount(Filesystem::MountPoint::System, firmwarePath, true);

    // 3. Attempt param.sfo
    Loader::ExecutableInfo elfInfo;
    auto sfoPath = contentPath / "sce_sys" / "param.sfo";
    Loader::LoadParamSfo(sfoPath, elfInfo);

    // 4. Load ELF
    elfInfo = Loader::ExecutableInfo{};
    auto loadResult = Loader::LoadExecutable(elfPath, elfInfo);
    if (loadResult != Loader::LoadResult::Ok) {
        CHUCKSTATION5_ERROR("[Process] LoadExecutable failed: %s",
                   Loader::LoadResultStr(loadResult));
        return 0;
    }

    // 5. Register process
    auto& ps = ProcState::Get();
    uint32_t pid = ps.nextPid.fetch_add(1);

    ProcessEntry entry;
    entry.info.pid        = pid;
    entry.info.titleId    = elfInfo.titleId;
    entry.info.appVersion = elfInfo.appVersion;
    entry.info.state      = ProcessState::Created;
    entry.info.startTimeUs = NowUs();
    entry.state.store(ProcessState::Created);

    Module mainMod;
    mainMod.name    = elfPath.filename().string();
    mainMod.path    = elfPath;
    mainMod.elfInfo = elfInfo;
    mainMod.isMain  = true;
    entry.info.modules.push_back(std::move(mainMod));

    // 6. Create main thread (but don't start yet)
    KernelRuntime::ThreadAttr tattr;
    tattr.name      = "Main";
    tattr.stackSize = 2 * 1024 * 1024;  // 2 MiB main stack
    tattr.priority  = 256;

    uint64_t entryPoint = elfInfo.entryPoint;

    // Register all built-in syscalls for this process
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    // Point the CPU interpreter at the guest entry point.
    // The main thread's stack base comes from KernelRuntime::CreateThread,
    // which allocates it via Win32 VirtualAlloc.
    KernelRuntime::KHandle mainTh = KernelRuntime::CreateThread(
        [entryPoint](void*) -> int {
            // Set up CPU context for the guest entry point:
            //   RIP = entryPoint
            //   RSP = top of the thread's committed stack
            //   RFLAGS = IF (interrupts enabled in user mode)
            Cpu::Init();
            Cpu::GetContext().rflags = Cpu::Flags::IF;
            Cpu::SetRip(entryPoint);
            // RSP is set by the caller via KernelRuntime after CreateThread
            // returns — the main loop calls Cpu::Run() after Launch() returns.
            CHUCKSTATION5_INFO("[Process] Guest main thread ready, entry=0x%llx",
                      static_cast<unsigned long long>(entryPoint));
            return 0;
        },
        nullptr, tattr);

    // Set RSP to top of the allocated guest stack
    auto ti = KernelRuntime::GetThreadInfo(mainTh);
    if (ti.stackBase && ti.stackSize) {
        uint64_t rsp = ti.stackBase + ti.stackSize - 16; // 16-byte aligned top
        Cpu::SetRsp(rsp);
    }

    entry.mainThreadHandle     = mainTh;
    entry.info.mainThread      = mainTh;

    {
        std::lock_guard lk(ps.mtx);
        auto pe = std::make_unique<ProcessEntry>();
        pe->info = std::move(entry.info);
        pe->mainThreadHandle = entry.mainThreadHandle;
        pe->state.store(ProcessState::Created);
        ps.procs[pid] = std::move(pe);
        ps.currentPid.store(pid);
    }

    CHUCKSTATION5_INFO("[Process] Created PID=%u title='%s' entry=0x%llx",
              pid, elfInfo.titleId.c_str(),
              static_cast<unsigned long long>(elfInfo.entryPoint));
    return pid;
}

bool Start(uint32_t pid)
{
    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    auto it = ps.procs.find(pid);
    if (it == ps.procs.end()) return false;
    auto& e = *it->second;

    if (e.state.load() != ProcessState::Created) return false;
    e.state.store(ProcessState::Running);
    e.info.state = ProcessState::Running;

    bool ok = KernelRuntime::StartThread(e.mainThreadHandle);
    if (ok) {
        CHUCKSTATION5_INFO("[Process] PID=%u started (main thread h=%d).",
                  pid, e.mainThreadHandle);
    } else {
        e.state.store(ProcessState::Faulted);
        CHUCKSTATION5_ERROR("[Process] PID=%u start failed.", pid);
    }
    return ok;
}

bool RequestExit(uint32_t pid, int exitCode)
{
    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    auto it = ps.procs.find(pid);
    if (it == ps.procs.end()) return false;
    auto& e = *it->second;
    e.state.store(ProcessState::Exiting);
    e.info.exitCode = exitCode;
    CHUCKSTATION5_INFO("[Process] PID=%u RequestExit code=%d", pid, exitCode);
    return true;
}

bool Wait(uint32_t pid, int* exitCode, uint64_t timeoutUs)
{
    KernelRuntime::KHandle th = KernelRuntime::INVALID_HANDLE;
    {
        auto& ps = ProcState::Get();
        std::lock_guard lk(ps.mtx);
        auto it = ps.procs.find(pid);
        if (it == ps.procs.end()) return false;
        th = it->second->mainThreadHandle;
    }
    bool ok = KernelRuntime::JoinThread(th, exitCode, timeoutUs);
    if (ok) {
        auto& ps = ProcState::Get();
        std::lock_guard lk(ps.mtx);
        auto it = ps.procs.find(pid);
        if (it != ps.procs.end()) {
            it->second->state.store(ProcessState::Terminated);
            it->second->info.exitTimeUs = NowUs();
            if (exitCode) it->second->info.exitCode = *exitCode;
        }
        FireExitCallbacks(pid, exitCode ? *exitCode : 0);
    }
    return ok;
}

void Terminate(uint32_t pid)
{
    // Collect what we need while holding the lock, then release before heavy ops
    KernelRuntime::KHandle th = KernelRuntime::INVALID_HANDLE;
    int exitCode = 0;
    std::vector<ChuckStation5::Loader::ExecutableInfo> toUnload;

    {
        auto& ps = ProcState::Get();
        std::lock_guard lk(ps.mtx);
        auto it = ps.procs.find(pid);
        if (it == ps.procs.end()) return;
        auto& e = *it->second;

        th = e.mainThreadHandle;
        exitCode = e.info.exitCode;
        e.state.store(ProcessState::Terminated);
        e.info.exitTimeUs = NowUs();

        for (auto& mod : e.info.modules)
            toUnload.push_back(mod.elfInfo);
        e.info.modules.clear();

        if (ps.currentPid.load() == pid)
            ps.currentPid.store(0);
    }

    // Stop thread and unload outside the lock
    KernelRuntime::StopThread(th);

    for (auto& info : toUnload)
        Loader::UnloadExecutable(info);

    CHUCKSTATION5_INFO("[Process] PID=%u terminated.", pid);
    FireExitCallbacks(pid, exitCode);
}

// ── Module management ─────────────────────────────────────────────────────
bool LoadModule(uint32_t pid, const std::filesystem::path& path)
{
    Loader::ExecutableInfo info;
    if (auto r = Loader::LoadExecutable(path, info); r != Loader::LoadResult::Ok) {
        CHUCKSTATION5_ERROR("[Process] LoadModule '%s' failed: %s",
                   path.string().c_str(), Loader::LoadResultStr(r));
        return false;
    }

    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    auto it = ps.procs.find(pid);
    if (it == ps.procs.end()) return false;

    Module mod;
    mod.name    = path.filename().string();
    mod.path    = path;
    mod.elfInfo = std::move(info);
    it->second->info.modules.push_back(std::move(mod));
    CHUCKSTATION5_INFO("[Process] PID=%u loaded module '%s'", pid, path.filename().string().c_str());
    return true;
}

std::vector<Module> GetModules(uint32_t pid)
{
    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    auto it = ps.procs.find(pid);
    if (it == ps.procs.end()) return {};
    return it->second->info.modules;
}

// ── Queries ───────────────────────────────────────────────────────────────
ProcessInfo GetInfo(uint32_t pid)
{
    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    auto it = ps.procs.find(pid);
    if (it == ps.procs.end()) return ProcessInfo{};
    auto info = it->second->info;
    info.state = it->second->state.load();
    return info;
}

ProcessState GetState(uint32_t pid)
{
    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    auto it = ps.procs.find(pid);
    if (it == ps.procs.end()) return ProcessState::None;
    return it->second->state.load();
}

bool IsRunning(uint32_t pid) { return GetState(pid) == ProcessState::Running; }

uint32_t GetCurrentPid() { return ProcState::Get().currentPid.load(); }

void RegisterExitCallback(ExitCallbackFn fn)
{
    auto& ps = ProcState::Get();
    std::lock_guard lk(ps.mtx);
    ps.exitCallbacks.push_back(std::move(fn));
}

} // namespace ChuckStation5::Process
