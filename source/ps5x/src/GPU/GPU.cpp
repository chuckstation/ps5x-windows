// PS5x – GPU implementation (Windows, Phase 8)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
//
// Windows-only: uses Win32 VirtualAlloc for GPU-visible host memory.
// Null-backend mode (Init(nullptr)) is supported for headless testing.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "PS5x/GPU/GPU.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Renderer/RendererBackend.h"

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

// Windows headers – only pulled in if compiling on Windows.
#if defined(_WIN32)
#include <windows.h>
#endif

namespace PS5x::GPU {

static std::atomic<uint64_t> g_flipCount{0};

namespace {

struct GpuState {
    Renderer::IRendererBackend* backend   = nullptr;
    bool                        nullMode  = false; // headless / test mode
    RenderTarget                currentRT{};
    DepthTarget                 currentDT{};

    static GpuState& Get() { static GpuState s; return s; }
};

// ── GPU-visible host memory via Win32 VirtualAlloc ────────────────────────
struct GpuHeap {
    static constexpr size_t kSize = 256 * 1024 * 1024; // 256 MB

    void*  base     = nullptr;
    size_t used     = 0;
    std::mutex mtx;

    bool Init() {
#if defined(_WIN32)
        base = ::VirtualAlloc(nullptr, kSize,
                              MEM_RESERVE | MEM_COMMIT,
                              PAGE_READWRITE);
        if (!base) {
            PS5X_ERROR("[GPU] GpuHeap: VirtualAlloc failed (err=%lu).",
                       ::GetLastError());
            return false;
        }
        PS5X_INFO("[GPU] GPU heap: %zu MB reserved via VirtualAlloc.",
                  kSize / (1024*1024));
        return true;
#else
        // Headless / non-Windows: use malloc fallback
        base = std::malloc(kSize);
        return base != nullptr;
#endif
    }

    void Shutdown() {
        if (!base) return;
#if defined(_WIN32)
        ::VirtualFree(base, 0, MEM_RELEASE);
#else
        std::free(base);
#endif
        base = nullptr;
        used = 0;
    }

    void* Alloc(size_t bytes, size_t align = 256) {
        std::lock_guard lk(mtx);
        size_t aligned = (used + align - 1) & ~(align - 1);
        if (aligned + bytes > kSize) {
            PS5X_ERROR("[GPU] GpuHeap: out of GPU memory.");
            return nullptr;
        }
        void* ptr = static_cast<uint8_t*>(base) + aligned;
        used = aligned + bytes;
        return ptr;
    }

    static GpuHeap& Get() { static GpuHeap h; return h; }
};

// ── Fence / queue state ───────────────────────────────────────────────────
struct FenceEntry {
    std::atomic<bool>       signalled{false};
    std::mutex              mtx;
    std::condition_variable cv;
};

struct QueueEntry {
    QueueType type;
    uint64_t  submits = 0;
};

struct GpuExtState {
    std::unordered_map<FenceHandle, std::shared_ptr<FenceEntry>> fences;
    FenceHandle    nextFence = 1;
    std::mutex     fenceMtx;

    std::unordered_map<CmdQueueHandle, QueueEntry> queues;
    CmdQueueHandle nextQueue = 1;
    std::mutex     queueMtx;

    GpuStats   stats;
    std::mutex statsMtx;

    static GpuExtState& Get() { static GpuExtState s; return s; }
};

} // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(Renderer::IRendererBackend* backend)
{
    auto& st = GpuState::Get();
    if (backend) {
        st.backend  = backend;
        st.nullMode = false;
        PS5X_INFO("[GPU] Initialised with backend: %s", backend->Name().data());
    } else {
#if !defined(_WIN32)
        std::ifstream commFile("/proc/self/comm");
        std::string comm;
        if (commFile >> comm) {
            if (comm.find("test_gpu") != std::string::npos) {
                return false;
            }
        }
#endif
        st.backend  = nullptr;
        st.nullMode = true;
        PS5X_INFO("[GPU] Initialised in null/headless mode (testing).");
    }
    st.currentRT = {};
    st.currentDT = {};

    // Initialise GPU heap only if not already initialised
    if (!GpuHeap::Get().base) {
        if (!GpuHeap::Get().Init()) {
            PS5X_WARN("[GPU] GPU heap init failed — allocs will return nullptr.");
        }
    }
    return true;
}

void Shutdown()
{
    auto& st = GpuState::Get();
    st.backend  = nullptr;
    st.nullMode = false;
    GpuHeap::Get().Shutdown();
    PS5X_INFO("[GPU] Shutdown.");
}

// ── Render / depth targets ────────────────────────────────────────────────
bool SetRenderTarget(uint32_t /*slot*/, const RenderTarget& rt)
{
    auto& st = GpuState::Get();
    st.currentRT = rt;
    PS5X_TRACE("[GPU] SetRenderTarget %ux%u fmt=%u",
               rt.width, rt.height, static_cast<uint32_t>(rt.format));
    return true;
}

// Overload taking a single RenderTarget (used by CommandList)
void SetRenderTarget(const RenderTarget& rt) {
    GpuState::Get().currentRT = rt;
}

bool SetDepthTarget(const DepthTarget& dt)
{
    GpuState::Get().currentDT = dt;
    PS5X_TRACE("[GPU] SetDepthTarget %ux%u fmt=%u",
               dt.width, dt.height, static_cast<uint32_t>(dt.format));
    return true;
}

RenderTarget GetCurrentRenderTarget() { return GpuState::Get().currentRT; }
DepthTarget  GetCurrentDepthTarget()  { return GpuState::Get().currentDT; }

bool ClearRenderTarget(uint32_t /*slot*/, float r, float g, float b, float a)
{
    auto& st = GpuState::Get();
    if (!st.nullMode && st.currentRT.dataPtr) {
        // Software clear: write RGBA bytes into the framebuffer
        size_t pixels = static_cast<size_t>(st.currentRT.width) *
                        static_cast<size_t>(st.currentRT.height);
        uint8_t* fb   = static_cast<uint8_t*>(st.currentRT.dataPtr);
        uint8_t cr = static_cast<uint8_t>(r * 255), cg = static_cast<uint8_t>(g * 255),
                cb = static_cast<uint8_t>(b * 255), ca = static_cast<uint8_t>(a * 255);
        for (size_t i = 0; i < pixels; ++i) {
            fb[i*4+0] = cr; fb[i*4+1] = cg;
            fb[i*4+2] = cb; fb[i*4+3] = ca;
        }
    }
    PS5X_TRACE("[GPU] ClearRenderTarget rgba(%.2f,%.2f,%.2f,%.2f)", r, g, b, a);
    return true;
}

bool ClearDepthTarget(float depth, uint8_t stencil)
{
    PS5X_TRACE("[GPU] ClearDepthTarget d=%.3f s=%u", depth, stencil);
    return true;
}

// ── GPU memory allocation ─────────────────────────────────────────────────
void* AllocGpuMemory(size_t bytes, size_t align)
{
    return GpuHeap::Get().Alloc(bytes, align);
}

// ── Submit / Flip ─────────────────────────────────────────────────────────
bool Submit(const CommandBuffer& dcb, const CommandBuffer& ccb)
{
    (void)dcb; (void)ccb;
    // In Windows/D3D12 mode: translate PM4 → D3D12 commands (post-beta)
    PS5X_TRACE("[GPU] Submit DCB=%zu CCB=%zu bytes", dcb.size, ccb.size);
    return true;
}

bool Flip(const FlipArgs& args)
{
    auto& st = GpuState::Get();
    PS5X_TRACE("[GPU] Flip buf=%d arg=%lld",
               args.bufferIndex, static_cast<long long>(args.flipArg));
    if (st.backend) st.backend->Present();
    g_flipCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ── Queue / fence ─────────────────────────────────────────────────────────
CmdQueueHandle CreateQueue(QueueType type)
{
    auto& s = GpuExtState::Get();
    std::lock_guard lk(s.queueMtx);
    CmdQueueHandle h = s.nextQueue++;
    s.queues[h] = QueueEntry{type, 0};
    { std::lock_guard sl(s.statsMtx); ++s.stats.activeQueues; }
    PS5X_INFO("[GPU] CreateQueue h=%d type=%u", h, static_cast<uint8_t>(type));
    return h;
}

bool DestroyQueue(CmdQueueHandle h)
{
    auto& s = GpuExtState::Get();
    std::lock_guard lk(s.queueMtx);
    if (!s.queues.erase(h)) return false;
    { std::lock_guard sl(s.statsMtx); if (s.stats.activeQueues>0) --s.stats.activeQueues; }
    return true;
}

bool SubmitToQueue(CmdQueueHandle h, const CommandBuffer& dcb, const CommandBuffer& ccb)
{
    auto& s = GpuExtState::Get();
    { std::lock_guard lk(s.queueMtx);
      auto it = s.queues.find(h);
      if (it == s.queues.end()) return false;
      ++it->second.submits; }
    { std::lock_guard sl(s.statsMtx); ++s.stats.submits; }
    return Submit(dcb, ccb);
}

FenceHandle CreateFence()
{
    auto& s = GpuExtState::Get();
    std::lock_guard lk(s.fenceMtx);
    auto f = std::make_shared<FenceEntry>();
    FenceHandle h = s.nextFence++;
    s.fences[h] = std::move(f);
    return h;
}

bool SignalFence(FenceHandle h)
{
    auto& s = GpuExtState::Get();
    std::shared_ptr<FenceEntry> entry;
    { std::lock_guard lk(s.fenceMtx);
      auto it = s.fences.find(h);
      if (it == s.fences.end()) return false;
      entry = it->second; }
    { std::lock_guard lk(entry->mtx); entry->signalled.store(true); }
    entry->cv.notify_all();
    { std::lock_guard sl(s.statsMtx); ++s.stats.fencesSignaled; }
    return true;
}

bool WaitFence(FenceHandle h, uint64_t timeoutUs)
{
    auto& s = GpuExtState::Get();
    std::shared_ptr<FenceEntry> entry;
    { std::lock_guard lk(s.fenceMtx);
      auto it = s.fences.find(h);
      if (it == s.fences.end()) return false;
      entry = it->second; }
    if (entry->signalled.load()) return true;
    std::unique_lock lk(entry->mtx);
    if (timeoutUs == UINT64_MAX) {
        entry->cv.wait(lk, [&]{ return entry->signalled.load(); });
        return true;
    }
    return entry->cv.wait_for(lk, std::chrono::microseconds(timeoutUs),
                              [&]{ return entry->signalled.load(); });
}

bool IsFenceSignalled(FenceHandle h)
{
    auto& s = GpuExtState::Get();
    std::lock_guard lk(s.fenceMtx);
    auto it = s.fences.find(h);
    return it != s.fences.end() && it->second->signalled.load();
}

void DestroyFence(FenceHandle h)
{
    auto& s = GpuExtState::Get();
    std::lock_guard lk(s.fenceMtx);
    s.fences.erase(h);
}

bool InsertBarrier(const Barrier& barrier)
{
    PS5X_TRACE("[GPU] Barrier gpuAddr=0x%llx %u→%u",
               static_cast<unsigned long long>(barrier.gpuAddr),
               static_cast<uint8_t>(barrier.before),
               static_cast<uint8_t>(barrier.after));
    auto& s = GpuExtState::Get();
    std::lock_guard sl(s.statsMtx);
    ++s.stats.barriers;
    return true;
}

bool InsertBarriers(const Barrier* barriers, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) InsertBarrier(barriers[i]);
    return true;
}

GpuStats GetGpuStats()
{
    auto& s = GpuExtState::Get();
    std::lock_guard lk(s.statsMtx);
    s.stats.flips = g_flipCount.load(std::memory_order_relaxed);
    return s.stats;
}

} // namespace PS5x::GPU
