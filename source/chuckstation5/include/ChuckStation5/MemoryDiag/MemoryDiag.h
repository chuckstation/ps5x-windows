// ChuckStation5 – Memory Diagnostics
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
// Layered on top of the Memory Manager.
// Provides snapshots, allocation history, fragmentation reporting,
// region visualisation, and a statistics overlay for the UI.
#pragma once

#include "ChuckStation5/Memory/Memory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ChuckStation5::MemoryDiag {

// ── Snapshot ──────────────────────────────────────────────────────────────
struct Snapshot
{
    uint64_t             timestampUs   = 0;
    std::string          label;
    Memory::Stats        stats;
    std::vector<Memory::Region> regions;
};

// ── Allocation history entry ──────────────────────────────────────────────
enum class AllocEventType : uint8_t { Map=0, Unmap=1, Protect=2, Alloc=3, Free=4 };

struct AllocEvent
{
    AllocEventType type        = AllocEventType::Map;
    uintptr_t      address     = 0;
    size_t         size        = 0;
    Memory::Prot   prot        = Memory::Prot::None;
    Memory::AllocType allocType= Memory::AllocType::Unknown;
    std::string    tag;
    uint64_t       timestampUs = 0;
};

// ── Fragmentation report ──────────────────────────────────────────────────
struct FragReport
{
    size_t   totalReservedBytes  = 0;
    size_t   totalCommittedBytes = 0;
    size_t   largestFreeGapBytes = 0;
    uint32_t regionCount         = 0;
    uint32_t gapCount            = 0;
    double   fragmentationPct    = 0.0;  ///< committed / reserved * 100
};

// ── Statistics overlay ────────────────────────────────────────────────────
struct MemOverlay
{
    size_t   committed    = 0;
    size_t   reserved     = 0;
    size_t   heapAllocd   = 0;
    uint32_t regionCount  = 0;
    uint32_t peakRegions  = 0;
    double   usagePct     = 0.0;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();

// ── Snapshots ─────────────────────────────────────────────────────────────
/// Capture a named snapshot of the current memory state.
void     TakeSnapshot(std::string_view label = "");

/// Return all captured snapshots.
std::vector<Snapshot> GetSnapshots();

/// Clear the snapshot history.
void     ClearSnapshots();

/// Compare two snapshots. Returns a diff as a log-friendly string.
std::string DiffSnapshots(const Snapshot& a, const Snapshot& b);

// ── Allocation history ─────────────────────────────────────────────────────
/// Enable recording every Map/Unmap/Alloc/Free call.
void     EnableHistory(bool enable);
bool     IsHistoryEnabled();

/// Return the recorded allocation events.
std::vector<AllocEvent> GetHistory();

/// Clear the history buffer.
void     ClearHistory();

/// Record an event (called internally by the Memory instrumentation hooks).
void     RecordEvent(const AllocEvent& ev);

// ── Fragmentation ─────────────────────────────────────────────────────────
FragReport ComputeFragmentation();

// ── Statistics overlay ─────────────────────────────────────────────────────
MemOverlay GetOverlay();

/// Format the overlay as a compact one-line string for the UI status bar.
std::string FormatOverlay();


struct DiagReport {
    size_t   leakCount    = 0;   ///< number of unfreed tracked allocations
    size_t   trackedBytes = 0;   ///< total bytes currently tracked
};
void       RecordAlloc(uintptr_t tag, size_t bytes, const std::string& label);
void       RecordFree(uintptr_t tag);
DiagReport GetReport();

// ── Diagnostics ───────────────────────────────────────────────────────────
/// Log a full memory map to the ChuckStation5 Logger.
void DumpMemoryMap();

/// Log a fragmentation report.
void DumpFragmentation();

/// Search for a byte pattern in all readable committed regions.
/// Returns list of host addresses where the pattern was found.
std::vector<uintptr_t> SearchPattern(const uint8_t* pattern, size_t patLen);

} // namespace ChuckStation5::MemoryDiag
