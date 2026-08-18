// ChuckStation5 – Memory Manager implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/Logger/Logger.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace ChuckStation5::Memory {

// ── Platform helpers ──────────────────────────────────────────────────────

namespace {

#ifdef _WIN32
DWORD ProtToWin(Prot p)
{
    switch (p)
    {
        case Prot::None:  return PAGE_NOACCESS;
        case Prot::Read:  return PAGE_READONLY;
        case Prot::RW:    return PAGE_READWRITE;
        case Prot::RX:    return PAGE_EXECUTE_READ;
        case Prot::RWX:   return PAGE_EXECUTE_READWRITE;
        default:
            if ((p & Prot::Exec) != Prot::None && (p & Prot::Write) != Prot::None)
                return PAGE_EXECUTE_READWRITE;
            if ((p & Prot::Exec) != Prot::None)
                return PAGE_EXECUTE_READ;
            if ((p & Prot::Write) != Prot::None)
                return PAGE_READWRITE;
            return PAGE_READONLY;
    }
}
#else
int ProtToMmap(Prot p)
{
    int r = PROT_NONE;
    if ((p & Prot::Read)  != Prot::None) r |= PROT_READ;
    if ((p & Prot::Write) != Prot::None) r |= PROT_WRITE;
    if ((p & Prot::Exec)  != Prot::None) r |= PROT_EXEC;
    return r;
}
#endif

size_t AlignUp(size_t v, size_t align)
{
    return (v + align - 1) & ~(align - 1);
}

// ── Tracked allocation (heap) ─────────────────────────────────────────────
struct HeapAlloc
{
    void*     rawPtr = nullptr;
    size_t    size   = 0;
    AllocType type   = AllocType::Unknown;
};

// ── State ─────────────────────────────────────────────────────────────────
struct MemState
{
    std::vector<Region>                    regions;
    std::unordered_map<uintptr_t, HeapAlloc> heapAllocs;
    Stats                                  stats{};
    std::recursive_mutex                   mutex;

    static MemState& Get()
    {
        static MemState s;
        return s;
    }
};

void AddRegion(MemState& st, const Region& r)
{
    st.regions.push_back(r);
    st.stats.regionCount++;
    if (st.stats.regionCount > st.stats.peakRegionCount)
        st.stats.peakRegionCount = st.stats.regionCount;
    st.stats.totalReserved += r.size;
    if (r.committed) st.stats.totalCommitted += r.size;
}

void RemoveRegion(MemState& st, uintptr_t base)
{
    auto it = std::find_if(st.regions.begin(), st.regions.end(),
                           [base](const Region& r) { return r.base == base; });
    if (it == st.regions.end()) return;
    st.stats.totalReserved -= it->size;
    if (it->committed) st.stats.totalCommitted -= it->size;
    st.stats.regionCount--;
    st.regions.erase(it);
}

} // anonymous namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────

bool Init()
{
    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    st.regions.clear();
    st.heapAllocs.clear();
    st.stats = {};
    CHUCKSTATION5_INFO("[Memory] Manager initialised (page=0x%zx).", PAGE_SIZE);
    return true;
}

void Shutdown()
{
    ReportLeaks();

    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);

    // Release any remaining regions
    for (const auto& r : st.regions)
    {
        if (!r.committed) continue;
#ifdef _WIN32
        VirtualFree(reinterpret_cast<LPVOID>(r.base), 0, MEM_RELEASE);
#else
        munmap(reinterpret_cast<void*>(r.base), r.size);
#endif
    }
    st.regions.clear();

    // Free heap allocs
    for (auto& [ptr, ha] : st.heapAllocs)
    {
        CHUCKSTATION5_WARN("[Memory] Leak: heap alloc at 0x%zx size=%zu", ptr, ha.size);
#ifdef _WIN32
        _aligned_free(ha.rawPtr);
#else
        free(ha.rawPtr);
#endif
    }
    st.heapAllocs.clear();
    st.stats = {};
    CHUCKSTATION5_INFO("[Memory] Shutdown complete.");
}

void Reset()
{
    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);

    for (const auto& r : st.regions)
    {
        if (!r.committed) continue;
#ifdef _WIN32
        VirtualFree(reinterpret_cast<LPVOID>(r.base), 0, MEM_RELEASE);
#else
        munmap(reinterpret_cast<void*>(r.base), r.size);
#endif
    }
    st.regions.clear();
    st.heapAllocs.clear();
    st.stats = {};
    CHUCKSTATION5_INFO("[Memory] Reset.");
}

// ── Virtual memory ─────────────────────────────────────────────────────────

uintptr_t Reserve(uintptr_t hint, size_t size, std::string_view tag)
{
    size = AlignUp(size, PAGE_SIZE);

#ifdef _WIN32
    auto* ptr = VirtualAlloc(
        hint ? reinterpret_cast<LPVOID>(hint) : nullptr,
        size, MEM_RESERVE, PAGE_NOACCESS);
    if (!ptr)
    {
        // retry without hint
        ptr = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
    }
    if (!ptr)
    {
        CHUCKSTATION5_ERROR("[Memory] Reserve failed: size=%zu hint=0x%zx", size, hint);
        return 0;
    }
    auto base = reinterpret_cast<uintptr_t>(ptr);
#else
    int flags = MAP_PRIVATE | MAP_ANON;
    if (hint) flags |= MAP_FIXED_NOREPLACE;

    void* ptr = mmap(hint ? reinterpret_cast<void*>(hint) : nullptr,
                     size, PROT_NONE, flags, -1, 0);

    if (ptr == MAP_FAILED && hint)
    {
        // retry without hint
        ptr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    }
    if (ptr == MAP_FAILED)
    {
        CHUCKSTATION5_ERROR("[Memory] Reserve failed: size=%zu hint=0x%zx", size, hint);
        return 0;
    }
    auto base = reinterpret_cast<uintptr_t>(ptr);
#endif

    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);

    Region r;
    r.base      = base;
    r.size      = size;
    r.prot      = Prot::None;
    r.type      = AllocType::Unknown;
    r.tag       = std::string(tag);
    r.committed = false;
    AddRegion(st, r);

    CHUCKSTATION5_TRACE("[Memory] Reserved 0x%zx bytes @ 0x%zx [%s]", size, base,
               std::string(tag).c_str());
    return base;
}

bool Commit(uintptr_t base, size_t size, Prot prot)
{
    size = AlignUp(size, PAGE_SIZE);

#ifdef _WIN32
    auto* ptr = VirtualAlloc(reinterpret_cast<LPVOID>(base),
                             size, MEM_COMMIT, ProtToWin(prot));
    if (!ptr)
    {
        CHUCKSTATION5_ERROR("[Memory] Commit failed: base=0x%zx size=%zu", base, size);
        return false;
    }
#else
    if (mprotect(reinterpret_cast<void*>(base), size, ProtToMmap(prot)) != 0)
    {
        CHUCKSTATION5_ERROR("[Memory] Commit/mprotect failed: base=0x%zx size=%zu", base, size);
        return false;
    }
#endif

    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);

    auto it = std::find_if(st.regions.begin(), st.regions.end(),
                           [base](const Region& r) { return r.base == base; });
    if (it != st.regions.end())
    {
        it->committed = true;
        it->prot      = prot;
        if (!it->committed)
            st.stats.totalCommitted += size;
    }
    return true;
}

uintptr_t Map(uintptr_t hint, size_t size, Prot prot, AllocType type, std::string_view tag)
{
    size = AlignUp(size, PAGE_SIZE);

#ifdef _WIN32
    DWORD winProt = ProtToWin(prot);
    auto* ptr = VirtualAlloc(
        hint ? reinterpret_cast<LPVOID>(hint) : nullptr,
        size, MEM_RESERVE | MEM_COMMIT, winProt);
    if (!ptr && hint)
        ptr = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, winProt);
    if (!ptr)
    {
        CHUCKSTATION5_ERROR("[Memory] Map failed: size=%zu", size);
        return 0;
    }
    auto base = reinterpret_cast<uintptr_t>(ptr);
#else
    int flags = MAP_PRIVATE | MAP_ANON;
    void* ptr = mmap(hint ? reinterpret_cast<void*>(hint) : nullptr,
                     size, ProtToMmap(prot), flags, -1, 0);
    if (ptr == MAP_FAILED)
    {
        CHUCKSTATION5_ERROR("[Memory] Map failed: size=%zu", size);
        return 0;
    }
    auto base = reinterpret_cast<uintptr_t>(ptr);
#endif

    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);

    Region r;
    r.base      = base;
    r.size      = size;
    r.prot      = prot;
    r.type      = type;
    r.tag       = std::string(tag);
    r.committed = true;
    AddRegion(st, r);
    st.stats.totalCommitted += size;

    CHUCKSTATION5_TRACE("[Memory] Map 0x%zx bytes @ 0x%zx prot=%u [%s]",
               size, base, static_cast<uint32_t>(prot), std::string(tag).c_str());
    return base;
}

bool Protect(uintptr_t base, size_t size, Prot prot)
{
#ifdef _WIN32
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(base), size, ProtToWin(prot), &old))
    {
        CHUCKSTATION5_ERROR("[Memory] Protect failed: base=0x%zx", base);
        return false;
    }
#else
    if (mprotect(reinterpret_cast<void*>(base), size, ProtToMmap(prot)) != 0)
    {
        CHUCKSTATION5_ERROR("[Memory] Protect failed: base=0x%zx", base);
        return false;
    }
#endif

    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    auto it = std::find_if(st.regions.begin(), st.regions.end(),
                           [base](const Region& r) { return r.base == base; });
    if (it != st.regions.end()) it->prot = prot;
    return true;
}

bool Unmap(uintptr_t base, size_t size)
{
    (void)size;
#ifdef _WIN32
    if (!VirtualFree(reinterpret_cast<LPVOID>(base), 0, MEM_RELEASE))
    {
        CHUCKSTATION5_ERROR("[Memory] Unmap failed: base=0x%zx", base);
        return false;
    }
#else
    if (munmap(reinterpret_cast<void*>(base), size) != 0)
    {
        CHUCKSTATION5_ERROR("[Memory] Unmap failed: base=0x%zx", base);
        return false;
    }
#endif

    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    RemoveRegion(st, base);
    return true;
}

// ── Aligned heap ──────────────────────────────────────────────────────────

void* Alloc(size_t size, size_t align, AllocType type)
{
    if (align < sizeof(void*)) align = sizeof(void*);

#ifdef _WIN32
    void* ptr = _aligned_malloc(size, align);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, align, size) != 0) ptr = nullptr;
#endif
    if (!ptr)
    {
        CHUCKSTATION5_ERROR("[Memory] Alloc failed: size=%zu align=%zu", size, align);
        return nullptr;
    }

    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    auto base = reinterpret_cast<uintptr_t>(ptr);
    st.heapAllocs[base] = HeapAlloc{ptr, size, type};
    st.stats.totalAllocated += size;
    return ptr;
}

void Free(void* ptr)
{
    if (!ptr) return;
    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    auto base = reinterpret_cast<uintptr_t>(ptr);
    auto it   = st.heapAllocs.find(base);
    if (it == st.heapAllocs.end())
    {
        CHUCKSTATION5_WARN("[Memory] Free unknown ptr 0x%zx", base);
        return;
    }
    st.stats.totalAllocated -= it->second.size;
    st.heapAllocs.erase(it);
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// ── Queries ───────────────────────────────────────────────────────────────

std::optional<Region> FindRegion(uintptr_t addr)
{
    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    for (const auto& r : st.regions)
    {
        if (addr >= r.base && addr < r.base + r.size)
            return r;
    }
    return std::nullopt;
}

static bool HasProt(uintptr_t base, size_t size, Prot required)
{
    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    for (const auto& r : st.regions)
    {
        if (r.base <= base && base + size <= r.base + r.size)
            return r.committed && (r.prot & required) == required;
    }
    return false;
}

bool IsReadable(uintptr_t base, size_t size) {
    if (HasProt(base, size, Prot::Read)) return true;
#if !defined(_WIN32)
    int fd[2];
    if (pipe(fd) < 0) return false;
    ssize_t n = write(fd[1], reinterpret_cast<const void*>(base), size);
    close(fd[0]);
    close(fd[1]);
    return n == static_cast<ssize_t>(size);
#else
    return true; // Windows fallback
#endif
}

bool IsWritable(uintptr_t base, size_t size) {
    if (HasProt(base, size, Prot::Write)) return true;
#if !defined(_WIN32)
    if (!IsReadable(base, size)) return false;

    int fd[2];
    if (pipe(fd) < 0) return false;

    std::vector<uint8_t> backup(size);
    std::memcpy(backup.data(), reinterpret_cast<const void*>(base), size);

    std::vector<uint8_t> dummy(size, 0);
    ssize_t nw = write(fd[1], dummy.data(), size);
    (void)nw;

    ssize_t n = read(fd[0], reinterpret_cast<void*>(base), size);
    if (n == static_cast<ssize_t>(size)) {
        std::memcpy(reinterpret_cast<void*>(base), backup.data(), size);
    }

    close(fd[0]);
    close(fd[1]);

    return n == static_cast<ssize_t>(size);
#else
    return true; // Windows fallback
#endif
}
bool IsExecutable(uintptr_t base, size_t size) { return HasProt(base, size, Prot::Exec); }

Stats GetStats()
{
    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    return st.stats;
}

std::vector<Region> GetRegions()
{
    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    return st.regions;
}

void ReportLeaks()
{
    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    if (st.regions.empty() && st.heapAllocs.empty()) return;

    CHUCKSTATION5_WARN("[Memory] === Leak report ===");
    for (const auto& r : st.regions)
        CHUCKSTATION5_WARN("[Memory]  Region 0x%zx+0x%zx [%s]", r.base, r.size, r.tag.c_str());
    for (const auto& [ptr, ha] : st.heapAllocs)
        CHUCKSTATION5_WARN("[Memory]  Heap   0x%zx size=%zu", ptr, ha.size);
}

void ForEachRegion(RegionVisitorFn fn)
{
    auto& st = MemState::Get();
    std::lock_guard lock(st.mutex);
    for (const auto& r : st.regions)
        if (!fn(r)) break;
}

} // namespace ChuckStation5::Memory
