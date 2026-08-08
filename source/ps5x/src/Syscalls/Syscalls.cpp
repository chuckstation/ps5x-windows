// PS5x – Syscall Dispatcher (Windows-native, Phase 8)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
//
// Windows-only: memory syscalls (mmap/brk/munmap) are implemented via
// Win32 VirtualAlloc / VirtualFree instead of POSIX mmap.
// All Linux ABI syscall numbers are preserved for guest compatibility.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "PS5x/Syscalls/Syscalls.h"
#include "PS5x/Cpu/Cpu.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace PS5x::Syscalls {

using Clock = std::chrono::steady_clock;

namespace {

uint64_t NowUs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
}

struct SyscallState {
    std::unordered_map<uint64_t, SyscallDesc> table;
    std::mutex  tableMtx;
    SyscallStats stats;
    std::mutex   statsMtx;
    std::deque<SyscallRecord> recentLog;
    std::mutex                logMtx;
    static constexpr size_t   LOG_MAX = 4096;
    bool initialised = false;
    static SyscallState& Get() { static SyscallState s; return s; }
};

// ── Win32 virtual memory state for brk/mmap emulation ─────────────────────
namespace WinMem {
    // brk: single committed heap region, grown on demand
    static std::mutex brkMtx;
    static uintptr_t  brkBase    = 0;
    static size_t     brkSize    = 0;
    static uintptr_t  brkCurrent = 0;
    static constexpr size_t kBrkReserve = 64 * 1024 * 1024; // 64 MB

    static void EnsureBrk() {
        if (brkBase) return;
#if defined(_WIN32)
        void* p = ::VirtualAlloc(nullptr, kBrkReserve, MEM_RESERVE, PAGE_NOACCESS);
        if (p) {
            brkBase    = reinterpret_cast<uintptr_t>(p);
            brkCurrent = brkBase;
            brkSize    = 0;
        }
#else
        // Fallback for headless CI
        brkBase    = reinterpret_cast<uintptr_t>(std::malloc(kBrkReserve));
        brkCurrent = brkBase;
        brkSize    = kBrkReserve;
#endif
    }

    // Returns new brk value. If addr==0 returns current.
    static uintptr_t Brk(uintptr_t addr) {
        std::lock_guard lk(brkMtx);
        EnsureBrk();
        if (addr == 0) return brkCurrent;
        if (addr < brkBase) return brkCurrent;
        size_t newSize = addr - brkBase;
        if (newSize > kBrkReserve) {
            PS5X_WARN("[Syscall] brk: requested 0x%zx exceeds reserve.", newSize);
            return brkCurrent;
        }
#if defined(_WIN32)
        // Commit the newly requested pages
        size_t toCommit = newSize - brkSize;
        if (toCommit > 0) {
            void* commit = ::VirtualAlloc(
                reinterpret_cast<void*>(brkBase + brkSize),
                toCommit, MEM_COMMIT, PAGE_READWRITE);
            if (!commit) {
                PS5X_ERROR("[Syscall] brk: VirtualAlloc commit failed (err=%lu).",
                           ::GetLastError());
                return brkCurrent;
            }
            brkSize = newSize;
        }
#else
        brkSize = std::min(newSize, kBrkReserve);
#endif
        brkCurrent = brkBase + newSize;
        return brkCurrent;
    }

    // mmap emulation: VirtualAlloc a new region
    static void* Mmap(void* hint, size_t length, int prot, int flags) {
        (void)hint; (void)flags;
        if (length == 0) return reinterpret_cast<void*>(~0ULL); // MAP_FAILED
#if defined(_WIN32)
        DWORD winProt = PAGE_NOACCESS;
        bool  r = (prot & 1) != 0;
        bool  w = (prot & 2) != 0;
        bool  x = (prot & 4) != 0;
        if (x)       winProt = PAGE_EXECUTE_READWRITE;
        else if (w)  winProt = PAGE_READWRITE;
        else if (r)  winProt = PAGE_READONLY;
        void* p = ::VirtualAlloc(nullptr, length, MEM_RESERVE | MEM_COMMIT, winProt);
        if (!p) {
            PS5X_WARN("[Syscall] mmap: VirtualAlloc failed len=%zu err=%lu.",
                      length, ::GetLastError());
            return reinterpret_cast<void*>(~0ULL); // MAP_FAILED
        }
        return p;
#else
        void* p = std::aligned_alloc(4096, (length + 4095) & ~4095ULL);
        return p ? p : reinterpret_cast<void*>(~0ULL);
#endif
    }

    static int Munmap(void* addr, size_t length) {
        (void)length;
#if defined(_WIN32)
        if (!::VirtualFree(addr, 0, MEM_RELEASE)) {
            PS5X_WARN("[Syscall] munmap: VirtualFree failed err=%lu.", ::GetLastError());
            return -1;
        }
        return 0;
#else
        std::free(addr);
        return 0;
#endif
    }
} // namespace WinMem

// ── Built-in syscall handlers ─────────────────────────────────────────────

static int64_t Builtin_write(const SyscallArgs& a)
{
    uint64_t fd    = a.arg0;
    uint64_t buf   = a.arg1;
    uint64_t count = a.arg2;

    if (fd == 1 || fd == 2) {
        if (buf && count > 0) {
            PS5X_INFO("[Guest %s] %.*s",
                      fd == 1 ? "stdout" : "stderr",
                      static_cast<int>(count),
                      reinterpret_cast<const char*>(buf));
        }
        return static_cast<int64_t>(count);
    }
    // Other fds — pass through to Win32 WriteFile
#if defined(_WIN32)
    HANDLE hFile = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(fd));
    DWORD  written = 0;
    if (buf && count > 0 &&
        ::WriteFile(hFile, reinterpret_cast<const void*>(buf),
                    static_cast<DWORD>(count), &written, nullptr))
        return static_cast<int64_t>(written);
#endif
    PS5X_WARN("[Syscall] write: unsupported fd=%llu", static_cast<unsigned long long>(fd));
    return -9; // EBADF
}

static int64_t Builtin_read(const SyscallArgs& a)
{
    uint64_t fd    = a.arg0;
    uint64_t buf   = a.arg1;
    uint64_t count = a.arg2;
#if defined(_WIN32)
    HANDLE hFile = (fd == 0) ? ::GetStdHandle(STD_INPUT_HANDLE)
                              : reinterpret_cast<HANDLE>(static_cast<uintptr_t>(fd));
    DWORD nRead = 0;
    if (buf && count > 0 &&
        ::ReadFile(hFile, reinterpret_cast<void*>(buf),
                   static_cast<DWORD>(count), &nRead, nullptr))
        return static_cast<int64_t>(nRead);
#endif
    (void)fd; (void)buf; (void)count;
    return -9; // EBADF
}

static int64_t Builtin_exit(const SyscallArgs& a)
{
    int code = static_cast<int>(a.arg0);
    PS5X_INFO("[Syscall] exit(%d)", code);
    Cpu::Stop();
    return 0;
}

static int64_t Builtin_getpid(const SyscallArgs&)
{
#if defined(_WIN32)
    return static_cast<int64_t>(::GetCurrentProcessId());
#else
    return 1;
#endif
}

static int64_t Builtin_gettid(const SyscallArgs&)
{
#if defined(_WIN32)
    return static_cast<int64_t>(::GetCurrentThreadId());
#else
    return 1;
#endif
}

static int64_t Builtin_yield(const SyscallArgs&)
{
#if defined(_WIN32)
    ::SwitchToThread();
#else
    std::this_thread::yield();
#endif
    return 0;
}

static int64_t Builtin_clock_gettime(const SyscallArgs& a)
{
    uint64_t tsAddr = a.arg1;
    if (tsAddr) {
#if defined(_WIN32)
        LARGE_INTEGER freq{}, counter{};
        ::QueryPerformanceFrequency(&freq);
        ::QueryPerformanceCounter(&counter);
        uint64_t totalNs = static_cast<uint64_t>(
            counter.QuadPart * 1'000'000'000LL / freq.QuadPart);
        uint64_t sec  = totalNs / 1'000'000'000ULL;
        uint64_t nsec = totalNs % 1'000'000'000ULL;
#else
        uint64_t now  = NowUs();
        uint64_t sec  = now / 1'000'000;
        uint64_t nsec = (now % 1'000'000) * 1000;
#endif
        std::memcpy(reinterpret_cast<void*>(tsAddr),     &sec,  8);
        std::memcpy(reinterpret_cast<void*>(tsAddr + 8), &nsec, 8);
    }
    return 0;
}

static int64_t Builtin_nanosleep(const SyscallArgs& a)
{
    uint64_t tsAddr = a.arg0;
    if (tsAddr) {
        uint64_t sec{}, nsec{};
        std::memcpy(&sec,  reinterpret_cast<const void*>(tsAddr),     8);
        std::memcpy(&nsec, reinterpret_cast<const void*>(tsAddr + 8), 8);
        uint64_t totalMs = sec * 1000 + nsec / 1'000'000;
        if (totalMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(totalMs));
    }
    return 0;
}

static int64_t Builtin_brk(const SyscallArgs& a)
{
    uintptr_t addr = static_cast<uintptr_t>(a.arg0);
    uintptr_t result = WinMem::Brk(addr);
    PS5X_DEBUG("[Syscall] brk(0x%zx) → 0x%zx", static_cast<size_t>(addr),
               static_cast<size_t>(result));
    return static_cast<int64_t>(result);
}

static int64_t Builtin_mmap(const SyscallArgs& a)
{
    void*  hint   = reinterpret_cast<void*>(a.arg0);
    size_t length = static_cast<size_t>(a.arg1);
    int    prot   = static_cast<int>(a.arg2);
    int    flags  = static_cast<int>(a.arg3);
    // fd (a.arg4) and offset (a.arg5) ignored — anonymous mmap only
    void* p = WinMem::Mmap(hint, length, prot, flags);
    if (p == reinterpret_cast<void*>(~0ULL)) return -12; // ENOMEM
    PS5X_DEBUG("[Syscall] mmap(len=%zu, prot=%d) → 0x%p", length, prot, p);
    return static_cast<int64_t>(reinterpret_cast<uintptr_t>(p));
}

static int64_t Builtin_munmap(const SyscallArgs& a)
{
    void*  addr   = reinterpret_cast<void*>(a.arg0);
    size_t length = static_cast<size_t>(a.arg1);
    int    r      = WinMem::Munmap(addr, length);
    PS5X_DEBUG("[Syscall] munmap(0x%p, %zu) → %d", addr, length, r);
    return r;
}

static int64_t Builtin_mprotect(const SyscallArgs& a)
{
    void*  addr   = reinterpret_cast<void*>(a.arg0);
    size_t length = static_cast<size_t>(a.arg1);
    int    prot   = static_cast<int>(a.arg2);
#if defined(_WIN32)
    DWORD winProt = PAGE_NOACCESS;
    if (prot & 4)       winProt = PAGE_EXECUTE_READWRITE;
    else if (prot & 2)  winProt = PAGE_READWRITE;
    else if (prot & 1)  winProt = PAGE_READONLY;
    DWORD old = 0;
    if (!::VirtualProtect(addr, length, winProt, &old)) {
        PS5X_WARN("[Syscall] mprotect: VirtualProtect failed err=%lu.", ::GetLastError());
        return -13; // EACCES
    }
#else
    (void)addr; (void)length; (void)prot;
#endif
    return 0;
}

static int64_t Builtin_futex(const SyscallArgs& a)
{
    // futex(addr, FUTEX_WAIT/FUTEX_WAKE, val, ...)
    // Implement WAKE (op=1) by SwitchToThread; WAIT by spin.
    uint64_t op = a.arg1 & 0xF;
    (void)a;
    if (op == 1 /*FUTEX_WAKE*/) {
#if defined(_WIN32)
        ::SwitchToThread();
#else
        std::this_thread::yield();
#endif
        return 1;
    }
    // FUTEX_WAIT — yield once; full blocking not implemented here
    std::this_thread::yield();
    return 0;
}

static int64_t Builtin_getrusage(const SyscallArgs& a)
{
    // struct rusage at a.arg1 — zero-fill it
    uint64_t structAddr = a.arg1;
    if (structAddr) std::memset(reinterpret_cast<void*>(structAddr), 0, 144);
    return 0;
}

static int64_t Builtin_ioctl(const SyscallArgs& a)
{
    uint64_t fd      = a.arg0;
    uint64_t request = a.arg1;
    uint64_t argp    = a.arg2;

    // TIOCGWINSZ (0x5413) — get terminal window size
    // Return a synthetic 80x24 terminal for stdout/stderr
    constexpr uint64_t TIOCGWINSZ = 0x5413;
    if (request == TIOCGWINSZ && (fd == 1 || fd == 2)) {
        if (argp) {
            // struct winsize: ws_row, ws_col, ws_xpixel, ws_ypixel (each uint16_t)
            uint16_t ws[4] = {24, 80, 0, 0};
            std::memcpy(reinterpret_cast<void*>(argp), ws, sizeof(ws));
        }
        return 0;
    }

    // FIONBIO (0x5421) — set non-blocking mode; accept silently
    constexpr uint64_t FIONBIO = 0x5421;
    if (request == FIONBIO) return 0;

    // FIONREAD (0x541B) — bytes available to read; return 0
    constexpr uint64_t FIONREAD = 0x541B;
    if (request == FIONREAD) {
        if (argp) { int n = 0; std::memcpy(reinterpret_cast<void*>(argp), &n, 4); }
        return 0;
    }

    // Win32 device ioctl forwarding: only for real Win32 handles (fd >= 3)
#if defined(_WIN32)
    if (fd >= 3) {
        HANDLE h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(fd));
        DWORD  bytes = 0;
        // DeviceIoControl with the Linux ioctl request code as ioControlCode
        // This is a best-effort mapping; most PS5 homebrew will not reach here.
        void* inBuf  = argp ? reinterpret_cast<void*>(argp) : nullptr;
        DWORD  inSz  = argp ? 256 : 0;
        BOOL   ok    = ::DeviceIoControl(h, static_cast<DWORD>(request),
                                         inBuf, inSz,
                                         inBuf, inSz,
                                         &bytes, nullptr);
        if (ok) {
            PS5X_DEBUG("[Syscall] ioctl fd=%llu req=0x%llx → ok (%lu bytes)",
                       static_cast<unsigned long long>(fd),
                       static_cast<unsigned long long>(request), bytes);
            return 0;
        }
    }
#endif

    PS5X_WARN("[Syscall] ioctl fd=%llu req=0x%llx argp=0x%llx — ENOTTY",
              static_cast<unsigned long long>(fd),
              static_cast<unsigned long long>(request),
              static_cast<unsigned long long>(argp));
    return -25; // ENOTTY
}

static int64_t Unknown_handler(const SyscallArgs& a)
{
    PS5X_WARN("[Syscall] UNKNOWN nr=%llu — ENOSYS",
              static_cast<unsigned long long>(a.number));
    return -38; // ENOSYS
}

} // namespace (anonymous)

// ── Public API ────────────────────────────────────────────────────────────

SyscallArgs ExtractArgs(const Cpu::CpuContext& ctx)
{
    SyscallArgs a;
    a.number = ctx.gpr_get(Cpu::Reg::RAX);
    a.arg0   = ctx.gpr_get(Cpu::Reg::RDI);
    a.arg1   = ctx.gpr_get(Cpu::Reg::RSI);
    a.arg2   = ctx.gpr_get(Cpu::Reg::RDX);
    a.arg3   = ctx.gpr_get(Cpu::Reg::R10);
    a.arg4   = ctx.gpr_get(Cpu::Reg::R8);
    a.arg5   = ctx.gpr_get(Cpu::Reg::R9);
    return a;
}

bool Init()
{
    auto& st = SyscallState::Get();
    std::lock_guard lk(st.tableMtx);
    st.table.clear();
    st.stats       = SyscallStats{};
    st.initialised = true;
    PS5X_INFO("[Syscall] Dispatcher initialised (Windows-native).");
    return true;
}

void Shutdown()
{
    auto& st = SyscallState::Get();
    std::lock_guard lk(st.tableMtx);
    st.table.clear();
    st.initialised = false;
    PS5X_INFO("[Syscall] Dispatcher shut down.");
}

void RegisterBuiltins()
{
    RegisterSyscall(Nr::Write,        "write",          Builtin_write,        3);
    RegisterSyscall(Nr::Read,         "read",           Builtin_read,         3);
    RegisterSyscall(Nr::Exit,         "exit",           Builtin_exit,         1);
    RegisterSyscall(Nr::ExitGrp,      "exit_group",     Builtin_exit,         1);
    RegisterSyscall(Nr::GetPid,       "getpid",         Builtin_getpid,       0);
    RegisterSyscall(Nr::GetTid,       "gettid",         Builtin_gettid,       0);
    RegisterSyscall(Nr::Sched_yield,  "sched_yield",    Builtin_yield,        0);
    RegisterSyscall(Nr::ClockGettime, "clock_gettime",  Builtin_clock_gettime,2);
    RegisterSyscall(Nr::Brk,          "brk",            Builtin_brk,          1);
    RegisterSyscall(Nr::Mmap,         "mmap",           Builtin_mmap,         6);
    RegisterSyscall(Nr::Munmap,       "munmap",         Builtin_munmap,       2);
    RegisterSyscall(Nr::Mprotect,     "mprotect",       Builtin_mprotect,     3);
    RegisterSyscall(Nr::Futex,        "futex",          Builtin_futex,        6);
    RegisterSyscall(Nr::Nanosleep,    "nanosleep",      Builtin_nanosleep,    2);
    RegisterSyscall(Nr::Getrusage,    "getrusage",      Builtin_getrusage,    2);
    RegisterSyscall(Nr::Ioctl,        "ioctl",          Builtin_ioctl,        3);
    PS5X_INFO("[Syscall] Registered %zu built-in handlers.", std::size_t{16});
}

void RegisterSyscall(const SyscallDesc& desc)
{
    auto& st = SyscallState::Get();
    std::lock_guard lk(st.tableMtx);
    st.table[desc.number] = desc;
    PS5X_DEBUG("[Syscall] Registered %llu (%s)",
               static_cast<unsigned long long>(desc.number), desc.name.c_str());
}

void RegisterSyscall(uint64_t number, std::string name,
                     HandlerFn handler, uint8_t argCount)
{
    RegisterSyscall(SyscallDesc{number, std::move(name), std::move(handler), argCount});
}

bool Dispatch(Cpu::CpuContext& ctx)
{
    auto& st      = SyscallState::Get();
    SyscallArgs args = ExtractArgs(ctx);

    HandlerFn   handler;
    std::string sname;
    {
        std::lock_guard lk(st.tableMtx);
        auto it = st.table.find(args.number);
        if (it != st.table.end()) {
            handler = it->second.handler;
            sname   = it->second.name;
        }
    }

    {
        std::lock_guard lk(st.statsMtx);
        ++st.stats.total;
        if (handler) ++st.stats.known;
        else         ++st.stats.unknown;
    }

    int64_t result = handler ? handler(args) : Unknown_handler(args);

    if (result < 0) { std::lock_guard lk(st.statsMtx); ++st.stats.errors; }

    ctx.gpr_set(Cpu::Reg::RAX, static_cast<uint64_t>(result));

    PS5X_TRACE("[Syscall] %llu (%s) → %lld",
               static_cast<unsigned long long>(args.number),
               sname.empty() ? "unknown" : sname.c_str(),
               static_cast<long long>(result));

    RuntimeEvents::PublishCustom("syscall",
        (sname.empty() ? std::to_string(args.number) : sname)
        + " -> " + std::to_string(result));

    {
        std::lock_guard lk(st.logMtx);
        if (st.recentLog.size() >= SyscallState::LOG_MAX)
            st.recentLog.pop_front();
        st.recentLog.push_back({
            args.number,
            sname.empty() ? ("nr:" + std::to_string(args.number)) : sname,
            args, result, NowUs()
        });
    }
    return true;
}

bool ValidateArguments(const SyscallArgs& args)
{
    auto& st = SyscallState::Get();
    std::lock_guard lk(st.tableMtx);
    auto it = st.table.find(args.number);
    if (it == st.table.end()) return true;
    (void)args;
    return true;
}

std::optional<SyscallDesc> Lookup(uint64_t number)
{
    auto& st = SyscallState::Get();
    std::lock_guard lk(st.tableMtx);
    auto it = st.table.find(number);
    if (it == st.table.end()) return std::nullopt;
    return it->second;
}

const char* SyscallName(uint64_t number)
{
    auto& st = SyscallState::Get();
    std::lock_guard lk(st.tableMtx);
    auto it = st.table.find(number);
    return (it != st.table.end()) ? it->second.name.c_str() : "unknown";
}

SyscallStats GetStats()
{
    auto& st = SyscallState::Get();
    std::lock_guard lk(st.statsMtx);
    return st.stats;
}

void ResetStats()
{
    auto& st = SyscallState::Get();
    std::lock_guard lk(st.statsMtx);
    st.stats = SyscallStats{};
}

std::vector<SyscallRecord> GetRecentLog(size_t maxEntries)
{
    auto& st = SyscallState::Get();
    std::lock_guard lk(st.logMtx);
    size_t n = std::min(maxEntries, st.recentLog.size());
    return std::vector<SyscallRecord>(
        st.recentLog.end() - static_cast<ptrdiff_t>(n), st.recentLog.end());
}

} // namespace PS5x::Syscalls
