// PS5x – Memory Diagnostics implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/MemoryDiag/MemoryDiag.h"
#include "PS5x/Logger/Logger.h"

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace PS5x::MemoryDiag {

using Clock = std::chrono::steady_clock;

static uint64_t NowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
}

// ── State ─────────────────────────────────────────────────────────────────
namespace {

struct DiagState
{
    std::vector<Snapshot>   snapshots;
    std::vector<AllocEvent> history;
    std::atomic<bool>       historyEnabled{false};
    std::mutex              mtx;
    static constexpr size_t MAX_HISTORY = 65536;

    static DiagState& Get() { static DiagState s; return s; }
};

} // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init()
{
    auto& d = DiagState::Get();
    std::lock_guard lk(d.mtx);
    d.snapshots.clear();
    d.history.clear();
    d.historyEnabled.store(false);
    PS5X_INFO("[MemDiag] Initialised.");
    return true;
}

void Shutdown()
{
    auto& d = DiagState::Get();
    std::lock_guard lk(d.mtx);
    d.snapshots.clear();
    d.history.clear();
    PS5X_INFO("[MemDiag] Shutdown.");
}

// ── Snapshots ─────────────────────────────────────────────────────────────
void TakeSnapshot(std::string_view label)
{
    Snapshot s;
    s.timestampUs = NowUs();
    s.label       = std::string(label);
    s.stats       = Memory::GetStats();
    s.regions     = Memory::GetRegions();

    auto& d = DiagState::Get();
    std::lock_guard lk(d.mtx);
    d.snapshots.push_back(std::move(s));
    PS5X_DEBUG("[MemDiag] Snapshot '%.*s': regions=%u committed=%zu",
               static_cast<int>(label.size()), label.data(),
               s.stats.regionCount, s.stats.totalCommitted);
}

std::vector<Snapshot> GetSnapshots()
{
    auto& d = DiagState::Get();
    std::lock_guard lk(d.mtx);
    return d.snapshots;
}

void ClearSnapshots()
{
    auto& d = DiagState::Get();
    std::lock_guard lk(d.mtx);
    d.snapshots.clear();
}

std::string DiffSnapshots(const Snapshot& a, const Snapshot& b)
{
    std::ostringstream oss;
    oss << "Diff: '" << a.label << "' → '" << b.label << "'\n";

    int64_t dc = static_cast<int64_t>(b.stats.totalCommitted) -
                 static_cast<int64_t>(a.stats.totalCommitted);
    int64_t dr = static_cast<int64_t>(b.stats.regionCount) -
                 static_cast<int64_t>(a.stats.regionCount);

    oss << "  committed: " << a.stats.totalCommitted
        << " → " << b.stats.totalCommitted
        << "  (" << (dc >= 0 ? "+" : "") << dc << " bytes)\n";
    oss << "  regions:   " << a.stats.regionCount
        << " → " << b.stats.regionCount
        << "  (" << (dr >= 0 ? "+" : "") << dr << ")\n";

    // New regions in B not in A
    for (const auto& rb : b.regions) {
        bool found = false;
        for (const auto& ra : a.regions)
            if (ra.base == rb.base && ra.size == rb.size) { found = true; break; }
        if (!found)
            oss << "  + 0x" << std::hex << rb.base
                << "+" << rb.size << " [" << rb.tag << "]\n";
    }

    // Regions removed
    for (const auto& ra : a.regions) {
        bool found = false;
        for (const auto& rb : b.regions)
            if (ra.base == rb.base && ra.size == rb.size) { found = true; break; }
        if (!found)
            oss << "  - 0x" << std::hex << ra.base
                << "+" << ra.size << " [" << ra.tag << "]\n";
    }

    return oss.str();
}

// ── Allocation history ─────────────────────────────────────────────────────
void EnableHistory(bool enable)
{
    DiagState::Get().historyEnabled.store(enable);
    PS5X_INFO("[MemDiag] Allocation history %s.", enable ? "enabled" : "disabled");
}

bool IsHistoryEnabled()
{
    return DiagState::Get().historyEnabled.load();
}

std::vector<AllocEvent> GetHistory()
{
    auto& d = DiagState::Get();
    std::lock_guard lk(d.mtx);
    return d.history;
}

void ClearHistory()
{
    auto& d = DiagState::Get();
    std::lock_guard lk(d.mtx);
    d.history.clear();
}

void RecordEvent(const AllocEvent& ev)
{
    auto& d = DiagState::Get();
    if (!d.historyEnabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lk(d.mtx);
    if (d.history.size() >= DiagState::MAX_HISTORY)
        d.history.erase(d.history.begin()); // ring: drop oldest
    d.history.push_back(ev);
}

// ── Fragmentation ─────────────────────────────────────────────────────────
FragReport ComputeFragmentation()
{
    auto stats   = Memory::GetStats();
    auto regions = Memory::GetRegions();

    FragReport rep;
    rep.regionCount          = stats.regionCount;
    rep.totalReservedBytes   = stats.totalReserved;
    rep.totalCommittedBytes  = stats.totalCommitted;

    if (stats.totalReserved > 0)
        rep.fragmentationPct = 100.0 * static_cast<double>(stats.totalCommitted)
                               / static_cast<double>(stats.totalReserved);

    // Sort regions by base address to compute gaps
    std::sort(regions.begin(), regions.end(),
        [](const Memory::Region& a, const Memory::Region& b){ return a.base < b.base; });

    size_t largestGap = 0;
    for (size_t i = 1; i < regions.size(); ++i) {
        uintptr_t prev_end = regions[i-1].base + regions[i-1].size;
        if (regions[i].base > prev_end) {
            size_t gap = static_cast<size_t>(regions[i].base - prev_end);
            if (gap > largestGap) largestGap = gap;
            rep.gapCount++;
        }
    }
    rep.largestFreeGapBytes = largestGap;
    return rep;
}

// ── Statistics overlay ─────────────────────────────────────────────────────
MemOverlay GetOverlay()
{
    auto s = Memory::GetStats();
    MemOverlay ov;
    ov.committed    = s.totalCommitted;
    ov.reserved     = s.totalReserved;
    ov.heapAllocd   = s.totalAllocated;
    ov.regionCount  = s.regionCount;
    ov.peakRegions  = s.peakRegionCount;
    ov.usagePct     = (s.totalReserved > 0)
                    ? 100.0 * static_cast<double>(s.totalCommitted)
                      / static_cast<double>(s.totalReserved)
                    : 0.0;
    return ov;
}

std::string FormatOverlay()
{
    auto ov = GetOverlay();
    std::ostringstream oss;
    auto mb = [](size_t b){ return static_cast<double>(b) / (1024.0*1024.0); };
    oss << std::fixed << std::setprecision(1)
        << "Mem: " << mb(ov.committed) << "MB / " << mb(ov.reserved)
        << "MB  (" << ov.usagePct << "%)  regions=" << ov.regionCount;
    return oss.str();
}

// ── Diagnostics ───────────────────────────────────────────────────────────
void DumpMemoryMap()
{
    auto regions = Memory::GetRegions();
    PS5X_INFO("[MemDiag] === Memory Map (%zu regions) ===", regions.size());
    for (const auto& r : regions) {
        PS5X_INFO("[MemDiag]  0x%016zx + 0x%08zx  prot=%u  type=%u  [%s]",
                  r.base, r.size,
                  static_cast<uint32_t>(r.prot),
                  static_cast<uint32_t>(r.type),
                  r.tag.c_str());
    }
    auto ov = GetOverlay();
    PS5X_INFO("[MemDiag]  Total committed: %zu bytes  reserved: %zu bytes",
              ov.committed, ov.reserved);
}

void DumpFragmentation()
{
    auto rep = ComputeFragmentation();
    PS5X_INFO("[MemDiag] === Fragmentation Report ===");
    PS5X_INFO("[MemDiag]  Reserved:    %zu bytes", rep.totalReservedBytes);
    PS5X_INFO("[MemDiag]  Committed:   %zu bytes (%.1f%%)", rep.totalCommittedBytes,
              rep.fragmentationPct);
    PS5X_INFO("[MemDiag]  Regions:     %u", rep.regionCount);
    PS5X_INFO("[MemDiag]  Gaps:        %u  (largest: %zu bytes)",
              rep.gapCount, rep.largestFreeGapBytes);
}

std::vector<uintptr_t> SearchPattern(const uint8_t* pattern, size_t patLen)
{
    std::vector<uintptr_t> results;
    if (!pattern || patLen == 0) return results;

    Memory::ForEachRegion([&](const Memory::Region& r) {
        if (!r.committed || !Memory::IsReadable(r.base, r.size)) return true;
        const auto* mem = reinterpret_cast<const uint8_t*>(r.base);
        for (size_t i = 0; i + patLen <= r.size; ++i) {
            if (std::memcmp(mem + i, pattern, patLen) == 0)
                results.push_back(r.base + i);
        }
        return true;
    });

    PS5X_INFO("[MemDiag] Pattern search: %zu match(es)", results.size());
    return results;
}

// ── Phase 8: Simple leak tracker ─────────────────────────────────────────
namespace {
    struct TrackedAlloc { size_t bytes; std::string label; };
    std::mutex                            leak_mtx;
    std::unordered_map<uintptr_t, TrackedAlloc> leak_map;
}

void RecordAlloc(uintptr_t tag, size_t bytes, const std::string& label) {
    std::lock_guard lk(leak_mtx);
    leak_map[tag] = {bytes, label};
}

void RecordFree(uintptr_t tag) {
    std::lock_guard lk(leak_mtx);
    leak_map.erase(tag);
}

DiagReport GetReport() {
    std::lock_guard lk(leak_mtx);
    DiagReport r;
    r.leakCount = leak_map.size();
    for (auto& [_, a] : leak_map) r.trackedBytes += a.bytes;
    return r;
}

} // namespace PS5x::MemoryDiag
