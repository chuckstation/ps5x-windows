// PS5x – Kernel Runtime implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/KernelRuntime/KernelRuntime.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"

#include <atomic>
#include <unordered_map>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace PS5x::KernelRuntime {

// ── Internal kernel objects ───────────────────────────────────────────────
namespace {

using Clock = std::chrono::steady_clock;

// ── OS mutex wrapper ──────────────────────────────────────────────────────
struct KMutex {
    std::string          name;
    MutexAttr            attr;
    std::recursive_mutex rmtx;
    std::mutex           mtx;
    KHandle              owner = INVALID_HANDLE;

    void lock(bool recursive) {
        if (recursive) rmtx.lock(); else mtx.lock();
    }
    bool try_lock(bool recursive) {
        if (recursive) return rmtx.try_lock();
        return mtx.try_lock();
    }
    void unlock(bool recursive) {
        if (recursive) rmtx.unlock(); else mtx.unlock();
    }
};

// ── Semaphore ──────────────────────────────────────────────────────────
struct KSemaphore {
    std::string         name;
    SemaphoreAttr       attr;
    std::mutex          mtx;
    std::condition_variable cv;
    int32_t             value;
    explicit KSemaphore(const SemaphoreAttr& a) : attr(a), value(a.initialValue) {}
};

// ── Event ─────────────────────────────────────────────────────────────
struct KEvent {
    std::string         name;
    EventAttr           attr;
    std::mutex          mtx;
    std::condition_variable cv;
    bool                set = false;
};

// ── Timer ─────────────────────────────────────────────────────────────
struct KTimer {
    std::string         name;
    TimerAttr           attr;
    TimerCallbackFn     callback;
    void*               arg = nullptr;
    KHandle             handle = INVALID_HANDLE;
    std::atomic<bool>   running{false};
    std::thread         thread;
};

// ── Thread ─────────────────────────────────────────────────────────────
struct KThread {
    ThreadAttr          attr;
    ThreadInfo          info;
    ThreadEntryFn       fn;
    void*               arg = nullptr;
    std::thread         osThread;
    std::atomic<ThreadState> state{ThreadState::Created};
    std::mutex          joinMtx;
    std::condition_variable joinCv;
    std::map<uint32_t, void*> tlsData;
    std::mutex          tlsMtx;
};

// ── Handle table entry ────────────────────────────────────────────────
struct HandleEntry {
    KObjectType type = KObjectType::Unknown;
    std::shared_ptr<void> obj;
};

// ── TLS key registry ──────────────────────────────────────────────────
struct TlsKeyEntry {
    TlsDestructorFn dtor;
    bool            used = false;
};

// ── Global state ──────────────────────────────────────────────────────
struct KernelState {
    std::mutex                              mtx;
    std::unordered_map<int32_t, HandleEntry> handles;
    std::atomic<int32_t>                    nextHandle{1};

    std::map<uint32_t, TlsKeyEntry>         tlsKeys;
    std::atomic<uint32_t>                   nextTlsKey{0};

    // current-thread lookup
    thread_local static KHandle             currentHandle;

    static KernelState& Get() { static KernelState s; return s; }

    KHandle Alloc(KObjectType type, std::shared_ptr<void> obj) {
        KHandle h = nextHandle.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard lk(mtx);
        handles[h] = HandleEntry{type, std::move(obj)};
        return h;
    }

    template<typename T>
    std::shared_ptr<T> Get(KHandle h, KObjectType expectedType) {
        std::lock_guard lk(mtx);
        auto it = handles.find(h);
        if (it == handles.end() || it->second.type != expectedType)
            return nullptr;
        return std::static_pointer_cast<T>(it->second.obj);
    }

    bool Free(KHandle h) {
        std::lock_guard lk(mtx);
        return handles.erase(h) > 0;
    }

    uint32_t CountType(KObjectType t) {
        std::lock_guard lk(mtx);
        uint32_t n = 0;
        for (auto& [k,e] : handles) if (e.type == t) ++n;
        return n;
    }
};

thread_local KHandle KernelState::currentHandle = INVALID_HANDLE;

} // anonymous namespace

// ── Name table ────────────────────────────────────────────────────────────
const char* KObjectTypeName(KObjectType t)
{
    switch (t) {
        case KObjectType::Unknown:   return "Unknown";
        case KObjectType::Thread:    return "Thread";
        case KObjectType::Mutex:     return "Mutex";
        case KObjectType::Semaphore: return "Semaphore";
        case KObjectType::Event:     return "Event";
        case KObjectType::Timer:     return "Timer";
        case KObjectType::File:      return "File";
        case KObjectType::Socket:    return "Socket";
        case KObjectType::Module:    return "Module";
    }
    return "?";
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init()
{
    auto& st = KernelState::Get();
    std::lock_guard lk(st.mtx);
    st.handles.clear();
    st.tlsKeys.clear();
    st.nextHandle.store(1);
    st.nextTlsKey.store(0);
    PS5X_INFO("[KernelRuntime] Initialised.");
    return true;
}

void Reset()
{
    auto& st = KernelState::Get();
    std::lock_guard lk(st.mtx);
    // Stop timers first
    for (auto& [h, e] : st.handles) {
        if (e.type == KObjectType::Timer) {
            auto kt = std::static_pointer_cast<KTimer>(e.obj);
            kt->running.store(false);
            if (kt->thread.joinable()) kt->thread.detach();
        }
    }
    st.handles.clear();
    st.tlsKeys.clear();
    PS5X_INFO("[KernelRuntime] Reset.");
}

void Shutdown()
{
    Reset();
    PS5X_INFO("[KernelRuntime] Shutdown.");
}

// ── Handle table ─────────────────────────────────────────────────────────
KObjectType GetHandleType(KHandle h)
{
    auto& st = KernelState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.handles.find(h);
    return (it == st.handles.end()) ? KObjectType::Unknown : it->second.type;
}

bool CloseHandle(KHandle h)
{
    auto& st = KernelState::Get();
    return st.Free(h);
}

// ── Threads ───────────────────────────────────────────────────────────────
KHandle CreateThread(ThreadEntryFn fn, void* arg, const ThreadAttr& attr)
{
    auto kt = std::make_shared<KThread>();
    kt->fn   = std::move(fn);
    kt->arg  = arg;
    kt->attr = attr;
    kt->state.store(ThreadState::Created);

    size_t stackSz = attr.stackSize ? attr.stackSize : (256 * 1024); // 256 KB default

    // Allocate a host-side guard + stack region for accounting and
    // as backing for guest stack pointer initialisation.
    uintptr_t stackBase = 0;
#if defined(_WIN32)
    void* mem = ::VirtualAlloc(nullptr,
                               stackSz + 4096, // +1 guard page
                               MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE);
    if (mem) {
        // Protect the bottom 4 KB as a guard page
        DWORD old = 0;
        ::VirtualProtect(mem, 4096, PAGE_GUARD | PAGE_READWRITE, &old);
        stackBase = reinterpret_cast<uintptr_t>(mem) + 4096; // stack grows down from top
    }
#else
    // Headless fallback: use malloc
    void* mem = std::malloc(stackSz);
    stackBase = mem ? reinterpret_cast<uintptr_t>(mem) : 0;
#endif

    kt->info.name       = attr.name;
    kt->info.priority   = attr.priority;
    kt->info.stackBase  = stackBase;
    kt->info.stackSize  = stackSz;
    kt->info.state      = ThreadState::Created;

    auto& st = KernelState::Get();
    KHandle h = st.Alloc(KObjectType::Thread, kt);
    kt->info.handle = h;

    PS5X_INFO("[KernelRuntime] CreateThread '%s' h=%d stack=0x%llx+%zu",
              attr.name.c_str(), h,
              static_cast<unsigned long long>(stackBase), stackSz);
    return h;
}

bool StartThread(KHandle h)
{
    auto& st = KernelState::Get();
    auto kt = st.Get<KThread>(h, KObjectType::Thread);
    if (!kt) return false;
    if (kt->state.load() != ThreadState::Created) return false;

    kt->state.store(ThreadState::Running);
    kt->info.startTime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());

    kt->osThread = std::thread([kt, h]() {
        KernelState::currentHandle = h;
        int code = 0;
        if (kt->fn) code = kt->fn(kt->arg);
        kt->info.exitCode = code;
        kt->state.store(ThreadState::Dead);
        PS5X_DEBUG("[KernelRuntime] Thread '%s' exited code=%d", kt->info.name.c_str(), code);
    });

    PS5X_INFO("[KernelRuntime] Started thread '%s' h=%d", kt->info.name.c_str(), h);
    return true;
}

bool StopThread(KHandle h)
{
    auto& st = KernelState::Get();
    auto kt = st.Get<KThread>(h, KObjectType::Thread);
    if (!kt) return false;
    kt->state.store(ThreadState::Stopped);
    if (kt->osThread.joinable()) kt->osThread.detach();
    return true;
}

bool JoinThread(KHandle h, int* exitCode, uint64_t timeoutUs)
{
    auto& st = KernelState::Get();
    auto kt = st.Get<KThread>(h, KObjectType::Thread);
    if (!kt) return false;

    if (kt->osThread.joinable()) {
        if (timeoutUs == UINT64_MAX) {
            kt->osThread.join();
        } else {
            // Poll with sleep for timeout case (portable)
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::microseconds(timeoutUs);
            while (kt->state.load() != ThreadState::Dead &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            if (kt->state.load() == ThreadState::Dead && kt->osThread.joinable())
                kt->osThread.join();
        }
    }

    if (exitCode) *exitCode = kt->info.exitCode;
    return kt->state.load() == ThreadState::Dead;
}

bool SetThreadPriority(KHandle h, int priority)
{
    auto& st = KernelState::Get();
    auto kt = st.Get<KThread>(h, KObjectType::Thread);
    if (!kt) return false;
    kt->info.priority = priority;
    // Host thread priority: platform-specific, skipped for portability
    return true;
}

bool SetThreadAffinity(KHandle h, uint64_t mask)
{
    auto& st = KernelState::Get();
    auto kt = st.Get<KThread>(h, KObjectType::Thread);
    if (!kt) return false;
    kt->info.affinityMask = mask;
#if defined(_WIN32)
    if (kt->osThread.native_handle()) {
        HANDLE hOsThread = static_cast<HANDLE>(kt->osThread.native_handle());
        DWORD_PTR winMask = static_cast<DWORD_PTR>(mask);
        if (!::SetThreadAffinityMask(hOsThread, winMask)) {
            PS5X_WARN("[KernelRuntime] SetThreadAffinityMask failed (err=%lu).",
                      ::GetLastError());
            return false;
        }
        PS5X_DEBUG("[KernelRuntime] Thread h=%d affinity set to 0x%llx.", h,
                   static_cast<unsigned long long>(mask));
    }
#endif
    return true;
}

ThreadInfo GetThreadInfo(KHandle h)
{
    auto& st = KernelState::Get();
    auto kt = st.Get<KThread>(h, KObjectType::Thread);
    if (!kt) return ThreadInfo{};
    auto info = kt->info;
    info.state = kt->state.load();
    return info;
}

std::vector<ThreadInfo> GetAllThreads()
{
    auto& st = KernelState::Get();
    std::lock_guard lk(st.mtx);
    std::vector<ThreadInfo> out;
    for (auto& [h, e] : st.handles) {
        if (e.type != KObjectType::Thread) continue;
        auto kt = std::static_pointer_cast<KThread>(e.obj);
        auto info = kt->info;
        info.state = kt->state.load();
        out.push_back(info);
    }
    return out;
}

KHandle GetCurrentThreadHandle() { return KernelState::currentHandle; }

// ── Mutex ─────────────────────────────────────────────────────────────────
KHandle CreateMutex(const MutexAttr& attr, std::string_view name)
{
    auto km = std::make_shared<KMutex>();
    km->name = std::string(name);
    km->attr = attr;
    auto& st = KernelState::Get();
    KHandle h = st.Alloc(KObjectType::Mutex, km);
    PS5X_TRACE("[KernelRuntime] CreateMutex '%.*s' h=%d",
               static_cast<int>(name.size()), name.data(), h);
    return h;
}

KHandle Lock(KHandle h) { LockMutex(h); return h; }

bool LockMutex(KHandle h)
{
    auto& st = KernelState::Get();
    auto km = st.Get<KMutex>(h, KObjectType::Mutex);
    if (!km) return false;
    km->lock(km->attr.recursive);
    return true;
}

bool TryLockMutex(KHandle h)
{
    auto& st = KernelState::Get();
    auto km = st.Get<KMutex>(h, KObjectType::Mutex);
    if (!km) return false;
    return km->try_lock(km->attr.recursive);
}

bool UnlockMutex(KHandle h)
{
    auto& st = KernelState::Get();
    auto km = st.Get<KMutex>(h, KObjectType::Mutex);
    if (!km) return false;
    km->unlock(km->attr.recursive);
    return true;
}

// ── Semaphore ─────────────────────────────────────────────────────────────
KHandle CreateSemaphore(const SemaphoreAttr& attr, std::string_view name)
{
    auto ks = std::make_shared<KSemaphore>(attr);
    ks->name = std::string(name);
    auto& st = KernelState::Get();
    KHandle h = st.Alloc(KObjectType::Semaphore, ks);
    PS5X_TRACE("[KernelRuntime] CreateSemaphore '%.*s' init=%d h=%d",
               static_cast<int>(name.size()), name.data(), attr.initialValue, h);
    return h;
}

bool WaitSemaphore(KHandle h, uint64_t timeoutUs)
{
    auto& st = KernelState::Get();
    auto ks = st.Get<KSemaphore>(h, KObjectType::Semaphore);
    if (!ks) return false;
    std::unique_lock lk(ks->mtx);
    auto pred = [&]{ return ks->value > 0; };
    if (timeoutUs == UINT64_MAX) {
        ks->cv.wait(lk, pred);
    } else {
        if (!ks->cv.wait_for(lk, std::chrono::microseconds(timeoutUs), pred))
            return false;
    }
    --ks->value;
    return true;
}

bool SignalSemaphore(KHandle h, int32_t count)
{
    auto& st = KernelState::Get();
    auto ks = st.Get<KSemaphore>(h, KObjectType::Semaphore);
    if (!ks) return false;
    {
        std::lock_guard lk(ks->mtx);
        ks->value = std::min(ks->value + count, ks->attr.maxValue);
    }
    ks->cv.notify_all();
    return true;
}

int32_t GetSemaphoreValue(KHandle h)
{
    auto& st = KernelState::Get();
    auto ks = st.Get<KSemaphore>(h, KObjectType::Semaphore);
    if (!ks) return -1;
    std::lock_guard lk(ks->mtx);
    return ks->value;
}

// ── Event ─────────────────────────────────────────────────────────────────
KHandle CreateEvent(const EventAttr& attr, std::string_view name)
{
    auto ke = std::make_shared<KEvent>();
    ke->name = std::string(name);
    ke->attr = attr;
    ke->set  = attr.initialSet;
    auto& st = KernelState::Get();
    KHandle h = st.Alloc(KObjectType::Event, ke);
    PS5X_TRACE("[KernelRuntime] CreateEvent '%.*s' h=%d",
               static_cast<int>(name.size()), name.data(), h);
    return h;
}

bool SetEvent(KHandle h)
{
    auto& st = KernelState::Get();
    auto ke = st.Get<KEvent>(h, KObjectType::Event);
    if (!ke) return false;
    { std::lock_guard lk(ke->mtx); ke->set = true; }
    ke->cv.notify_all();
    return true;
}

bool ClearEvent(KHandle h)
{
    auto& st = KernelState::Get();
    auto ke = st.Get<KEvent>(h, KObjectType::Event);
    if (!ke) return false;
    std::lock_guard lk(ke->mtx);
    ke->set = false;
    return true;
}

bool WaitEvent(KHandle h, uint64_t timeoutUs)
{
    auto& st = KernelState::Get();
    auto ke = st.Get<KEvent>(h, KObjectType::Event);
    if (!ke) return false;
    std::unique_lock lk(ke->mtx);
    auto pred = [&]{ return ke->set; };
    bool ok;
    if (timeoutUs == UINT64_MAX) { ke->cv.wait(lk, pred); ok = true; }
    else ok = ke->cv.wait_for(lk, std::chrono::microseconds(timeoutUs), pred);
    if (ok && ke->attr.autoReset) ke->set = false;
    return ok;
}

// ── Timer ─────────────────────────────────────────────────────────────────
KHandle CreateTimer(const TimerAttr& attr, TimerCallbackFn cb, void* arg, std::string_view name)
{
    auto kt = std::make_shared<KTimer>();
    kt->attr     = attr;
    kt->callback = std::move(cb);
    kt->arg      = arg;
    kt->name     = std::string(name);
    auto& st = KernelState::Get();
    KHandle h = st.Alloc(KObjectType::Timer, kt);
    kt->handle = h;
    if (attr.autoStart) StartTimer(h);
    return h;
}

bool StartTimer(KHandle h)
{
    auto& st = KernelState::Get();
    auto kt = st.Get<KTimer>(h, KObjectType::Timer);
    if (!kt) return false;
    if (kt->running.load()) return true;
    kt->running.store(true);
    kt->thread = std::thread([kt]() {
        auto period = std::chrono::microseconds(kt->attr.periodUs ? kt->attr.periodUs : 0);
        do {
            if (kt->attr.periodUs > 0)
                std::this_thread::sleep_for(period);
            if (!kt->running.load()) break;
            if (kt->callback) kt->callback(kt->handle, kt->arg);
        } while (kt->attr.periodUs > 0 && kt->running.load());
        kt->running.store(false);
    });
    return true;
}

bool StopTimer(KHandle h)
{
    auto& st = KernelState::Get();
    auto kt = st.Get<KTimer>(h, KObjectType::Timer);
    if (!kt) return false;
    kt->running.store(false);
    if (kt->thread.joinable()) kt->thread.join();
    return true;
}

bool CancelTimer(KHandle h) { return StopTimer(h); }

// ── TLS – keyed by (thread_id, tls_key) ──────────────────────────────────
// Uses std::this_thread::get_id() to avoid currentHandle dependency.
namespace {
    struct TlsStore {
        using ThreadId = std::thread::id;
        using Key      = std::pair<ThreadId, TlsKey>;
        struct KeyHash {
            size_t operator()(const Key& k) const {
                return std::hash<ThreadId>{}(k.first) ^
                       (std::hash<uint32_t>{}(k.second) << 16);
            }
        };
        std::unordered_map<Key, void*, KeyHash> data;
        std::mutex mtx;
        static TlsStore& Get() { static TlsStore s; return s; }
    };
} // namespace

TlsKey TlsAlloc(TlsDestructorFn dtor)
{
    auto& st = KernelState::Get();
    std::lock_guard lk(st.mtx);
    TlsKey k = st.nextTlsKey.fetch_add(1);
    st.tlsKeys[k] = TlsKeyEntry{std::move(dtor), true};
    return k;
}

bool TlsFree(TlsKey key)
{
    auto& st = KernelState::Get();
    std::lock_guard lk(st.mtx);
    return st.tlsKeys.erase(key) > 0;
}

bool TlsSet(TlsKey key, void* value)
{
    auto& ts = TlsStore::Get();
    std::lock_guard lk(ts.mtx);
    ts.data[{std::this_thread::get_id(), key}] = value;
    return true;
}

void* TlsGet(TlsKey key)
{
    auto& ts = TlsStore::Get();
    std::lock_guard lk(ts.mtx);
    auto it = ts.data.find({std::this_thread::get_id(), key});
    return (it == ts.data.end()) ? nullptr : it->second;
}

// ── Statistics ────────────────────────────────────────────────────────────
KernelStats GetStats()
{
    auto& st = KernelState::Get();
    KernelStats s;
    s.totalHandles    = static_cast<uint32_t>(st.handles.size());
    s.totalThreads    = st.CountType(KObjectType::Thread);
    s.totalMutexes    = st.CountType(KObjectType::Mutex);
    s.totalSemaphores = st.CountType(KObjectType::Semaphore);
    s.totalEvents     = st.CountType(KObjectType::Event);
    s.totalTimers     = st.CountType(KObjectType::Timer);
    // Count running threads
    std::lock_guard lk(st.mtx);
    for (auto& [h,e] : st.handles) {
        if (e.type != KObjectType::Thread) continue;
        auto kt = std::static_pointer_cast<KThread>(e.obj);
        if (kt->state.load() == ThreadState::Running) ++s.runningThreads;
    }
    return s;
}

// ── Phase 6 implementations ───────────────────────────────────────────────

namespace {

struct NameRegistry
{
    // ns -> (name -> handle)
    std::unordered_map<uint32_t, std::unordered_map<std::string, KHandle>> table;
    std::mutex mtx;
    static NameRegistry& Get() { static NameRegistry r; return r; }
};

struct WaitQueueEntry
{
    std::string name;
    std::mutex  mtx;
    std::condition_variable cv;
    uint32_t    waiters = 0;
};

struct IpcEntry
{
    std::string                name;
    std::deque<std::vector<uint8_t>> queue;
    std::mutex                 mtx;
    std::condition_variable    cv;
    bool                       server = false;
};

struct P6State
{
    // Wait queues
    std::unordered_map<WqHandle, std::shared_ptr<WaitQueueEntry>> wqs;
    WqHandle  nextWq = 1;
    std::mutex wqMtx;

    // IPC ports
    std::unordered_map<std::string, std::shared_ptr<IpcEntry>> ipcByName;
    std::unordered_map<IpcPortHandle, std::shared_ptr<IpcEntry>> ipcByHandle;
    IpcPortHandle nextIpc = 1;
    std::mutex    ipcMtx;

    // Resource limits
    ResourceLimits limits;
    std::mutex     limitsMtx;

    static P6State& Get() { static P6State s; return s; }
};

} // namespace

// ── Object namespaces ─────────────────────────────────────────────────────

bool RegisterName(std::string_view name, KHandle handle, NamespaceId ns)
{
    auto& r = NameRegistry::Get();
    std::lock_guard lk(r.mtx);
    r.table[ns.value][std::string(name)] = handle;
    PS5X_DEBUG("[KR] RegisterName '%s' ns=%u h=%d",
               std::string(name).c_str(), ns.value, handle);
    return true;
}

KHandle LookupName(std::string_view name, NamespaceId ns)
{
    auto& r = NameRegistry::Get();
    std::lock_guard lk(r.mtx);
    auto nsIt = r.table.find(ns.value);
    if (nsIt == r.table.end()) return INVALID_HANDLE;
    auto it = nsIt->second.find(std::string(name));
    return (it == nsIt->second.end()) ? INVALID_HANDLE : it->second;
}

bool UnregisterName(std::string_view name, NamespaceId ns)
{
    auto& r = NameRegistry::Get();
    std::lock_guard lk(r.mtx);
    auto nsIt = r.table.find(ns.value);
    if (nsIt == r.table.end()) return false;
    return nsIt->second.erase(std::string(name)) > 0;
}

// ── Wait queues ───────────────────────────────────────────────────────────

WqHandle CreateWaitQueue(std::string_view name)
{
    auto& st = P6State::Get();
    std::lock_guard lk(st.wqMtx);
    auto wq = std::make_shared<WaitQueueEntry>();
    wq->name = name;
    WqHandle h = st.nextWq++;
    st.wqs[h] = std::move(wq);
    return h;
}

bool WaitOnQueue(WqHandle wq, uint64_t timeoutUs)
{
    std::shared_ptr<WaitQueueEntry> entry;
    {
        auto& st = P6State::Get();
        std::lock_guard lk(st.wqMtx);
        auto it = st.wqs.find(wq);
        if (it == st.wqs.end()) return false;
        entry = it->second;
    }
    std::unique_lock lk(entry->mtx);
    ++entry->waiters;
    if (timeoutUs == UINT64_MAX) {
        entry->cv.wait(lk);
        --entry->waiters;
        return true;
    }
    auto ok = entry->cv.wait_for(lk, std::chrono::microseconds(timeoutUs));
    --entry->waiters;
    return ok == std::cv_status::no_timeout;
}

bool WakeOne(WqHandle wq)
{
    auto& st = P6State::Get();
    std::lock_guard lk(st.wqMtx);
    auto it = st.wqs.find(wq);
    if (it == st.wqs.end()) return false;
    it->second->cv.notify_one();
    return true;
}

uint32_t WakeAll(WqHandle wq)
{
    auto& st = P6State::Get();
    std::lock_guard lk(st.wqMtx);
    auto it = st.wqs.find(wq);
    if (it == st.wqs.end()) return 0;
    uint32_t n = it->second->waiters;
    it->second->cv.notify_all();
    return n;
}

bool DestroyWaitQueue(WqHandle wq)
{
    auto& st = P6State::Get();
    std::lock_guard lk(st.wqMtx);
    auto it = st.wqs.find(wq);
    if (it == st.wqs.end()) return false;
    it->second->cv.notify_all();
    st.wqs.erase(it);
    return true;
}

// ── Handle duplication ────────────────────────────────────────────────────

KHandle DuplicateHandle(KHandle src)
{
    // For Phase 6: duplicate shares the same underlying object.
    // The KernelState stores shared_ptr, so just registering the same
    // pointer under a new handle effectively duplicates it.
    auto& st = KernelState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.handles.find(src);
    if (it == st.handles.end()) {
        PS5X_WARN("[KR] DuplicateHandle: unknown handle %d", src);
        return INVALID_HANDLE;
    }
    KHandle newH = st.nextHandle.fetch_add(1, std::memory_order_relaxed);
    st.handles[newH] = it->second;  // shared ownership
    PS5X_DEBUG("[KR] DuplicateHandle %d → %d", src, newH);
    return newH;
}

// ── Resource limits ───────────────────────────────────────────────────────

void SetResourceLimits(const ResourceLimits& limits)
{
    auto& st = P6State::Get();
    std::lock_guard lk(st.limitsMtx);
    st.limits = limits;
    PS5X_INFO("[KR] ResourceLimits: maxMem=%zu maxThreads=%u maxHandles=%u",
              limits.maxMemoryBytes, limits.maxThreads, limits.maxHandles);
}

ResourceLimits GetResourceLimits()
{
    auto& st = P6State::Get();
    std::lock_guard lk(st.limitsMtx);
    return st.limits;
}

// ── IPC foundations ───────────────────────────────────────────────────────

IpcPortHandle CreateIpcPort(std::string_view name)
{
    auto& st = P6State::Get();
    std::lock_guard lk(st.ipcMtx);
    auto entry = std::make_shared<IpcEntry>();
    entry->name   = name;
    entry->server = true;
    IpcPortHandle h = st.nextIpc++;
    st.ipcByName[std::string(name)] = entry;
    st.ipcByHandle[h] = entry;
    PS5X_INFO("[IPC] CreatePort '%s' h=%d", entry->name.c_str(), h);
    return h;
}

IpcPortHandle ConnectIpcPort(std::string_view name)
{
    auto& st = P6State::Get();
    std::lock_guard lk(st.ipcMtx);
    auto it = st.ipcByName.find(std::string(name));
    if (it == st.ipcByName.end()) {
        PS5X_WARN("[IPC] ConnectPort '%s' not found", std::string(name).c_str());
        return INVALID_IPC_PORT;
    }
    IpcPortHandle h = st.nextIpc++;
    st.ipcByHandle[h] = it->second;  // shared queue
    PS5X_DEBUG("[IPC] ConnectPort '%s' h=%d", std::string(name).c_str(), h);
    return h;
}

bool CloseIpcPort(IpcPortHandle h)
{
    auto& st = P6State::Get();
    std::lock_guard lk(st.ipcMtx);
    auto it = st.ipcByHandle.find(h);
    if (it == st.ipcByHandle.end()) return false;
    // Remove name registration if this is the server
    if (it->second->server) {
        st.ipcByName.erase(it->second->name);
    }
    st.ipcByHandle.erase(it);
    return true;
}

bool IpcSend(IpcPortHandle h, const void* data, size_t size)
{
    auto& st = P6State::Get();
    std::shared_ptr<IpcEntry> entry;
    {
        std::lock_guard lk(st.ipcMtx);
        auto it = st.ipcByHandle.find(h);
        if (it == st.ipcByHandle.end()) return false;
        entry = it->second;
    }
    const auto* p = static_cast<const uint8_t*>(data);
    {
        std::lock_guard lk(entry->mtx);
        entry->queue.emplace_back(p, p + size);
    }
    entry->cv.notify_one();
    return true;
}

int64_t IpcRecv(IpcPortHandle h, void* buf, size_t bufSize, uint64_t timeoutUs)
{
    auto& st = P6State::Get();
    std::shared_ptr<IpcEntry> entry;
    {
        std::lock_guard lk(st.ipcMtx);
        auto it = st.ipcByHandle.find(h);
        if (it == st.ipcByHandle.end()) return -1;
        entry = it->second;
    }
    std::unique_lock lk(entry->mtx);
    auto pred = [&]{ return !entry->queue.empty(); };
    if (timeoutUs == UINT64_MAX) {
        entry->cv.wait(lk, pred);
    } else if (!entry->cv.wait_for(lk, std::chrono::microseconds(timeoutUs), pred)) {
        return -1;  // timeout
    }
    auto& msg = entry->queue.front();
    size_t toCopy = std::min(msg.size(), bufSize);
    std::memcpy(buf, msg.data(), toCopy);
    entry->queue.pop_front();
    return static_cast<int64_t>(toCopy);
}


// ── Phase 8: Generic handle allocator ────────────────────────────────────
namespace {
    std::mutex                             alloc_mtx;
    std::atomic<KHandle>                   next_handle{1000};
    std::unordered_map<KHandle, KObjectType> alloc_table;
}

KHandle AllocHandle(KObjectType type) {
    KHandle h = next_handle.fetch_add(1);
    std::lock_guard lk(alloc_mtx);
    alloc_table[h] = type;
    return h;
}

void FreeHandle(KHandle h) {
    std::lock_guard lk(alloc_mtx);
    alloc_table.erase(h);
}

} // namespace PS5x::KernelRuntime
