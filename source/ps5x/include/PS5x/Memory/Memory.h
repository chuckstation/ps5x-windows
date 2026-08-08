// PS5x – Memory Manager
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// Replaces the Kernel stubs with a full tracked allocator.
// Provides:
//   • Virtual address space reservations (PS5 layout)
//   • Page-granularity commit / decommit
//   • Memory protection changes
//   • Allocation tracking with leak detection
//   • Debug statistics
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PS5x::Memory {

// ── Constants ─────────────────────────────────────────────────────────────
static constexpr size_t   PAGE_SIZE         = 0x4000;          // 16 KiB (PS5 page)
static constexpr size_t   LARGE_PAGE_SIZE   = 0x200000;        // 2 MiB
static constexpr uint64_t USER_BASE         = 0x00400000ULL;
static constexpr uint64_t USER_END          = 0x00000000'FFFFFFFFULL;
static constexpr uint64_t FLEX_HEAP_BASE    = 0x100000000ULL;  // 4 GiB
static constexpr uint64_t FLEX_HEAP_SIZE    = 0x40000000ULL;   // 1 GiB

// ── Protection flags ──────────────────────────────────────────────────────
enum class Prot : uint32_t
{
    None    = 0,
    Read    = 1 << 0,
    Write   = 1 << 1,
    Exec    = 1 << 2,
    RW      = Read | Write,
    RX      = Read | Exec,
    RWX     = Read | Write | Exec,
};
inline Prot operator|(Prot a, Prot b)
{ return static_cast<Prot>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
inline Prot operator&(Prot a, Prot b)
{ return static_cast<Prot>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); }

// ── Allocation type ───────────────────────────────────────────────────────
enum class AllocType : uint8_t
{
    Unknown  = 0,
    Code     = 1,   ///< executable segment
    Data     = 2,   ///< data segment
    Stack    = 3,
    Heap     = 4,
    FlexHeap = 5,
    GPU      = 6,
    System   = 7,
};

// ── Region descriptor ─────────────────────────────────────────────────────
struct Region
{
    uintptr_t    base    = 0;
    size_t       size    = 0;
    Prot         prot    = Prot::None;
    AllocType    type    = AllocType::Unknown;
    std::string  tag;          ///< debug label, e.g. "/app0/eboot.bin:LOAD0"
    bool         committed = false;
};

// ── Statistics ────────────────────────────────────────────────────────────
struct Stats
{
    size_t   totalReserved   = 0;
    size_t   totalCommitted  = 0;
    size_t   totalAllocated  = 0;
    uint32_t regionCount     = 0;
    uint32_t peakRegionCount = 0;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();
void Reset();    ///< Release all tracked regions (e.g. between program loads)

// ── Virtual address space ─────────────────────────────────────────────────

/// Reserve a VA range (does not commit physical pages).
/// @param hint    Preferred base address.  nullptr = let OS choose.
/// @param size    Must be PAGE_SIZE-aligned.
/// @param tag     Debug label.
/// @returns Actual base address, or 0 on failure.
uintptr_t Reserve(uintptr_t hint, size_t size, std::string_view tag = "");

/// Commit physical pages for a previously-reserved range.
bool Commit(uintptr_t base, size_t size, Prot prot);

/// Reserve + commit in one call (most common use case).
uintptr_t Map(uintptr_t hint, size_t size, Prot prot,
              AllocType type = AllocType::Unknown,
              std::string_view tag = "");

/// Change protection on a committed range.
bool Protect(uintptr_t base, size_t size, Prot prot);

/// Decommit and release a range.
bool Unmap(uintptr_t base, size_t size);

// ── Aligned heap ─────────────────────────────────────────────────────────
void*    Alloc(size_t size, size_t align = 16, AllocType type = AllocType::Heap);
void     Free(void* ptr);

// ── Address translation helpers ───────────────────────────────────────────
/// Find the region containing addr, or nullopt.
std::optional<Region> FindRegion(uintptr_t addr);

/// True if [base, base+size) is fully within committed, readable memory.
bool IsReadable(uintptr_t base, size_t size);
bool IsWritable(uintptr_t base, size_t size);
bool IsExecutable(uintptr_t base, size_t size);

// ── Diagnostics ───────────────────────────────────────────────────────────
Stats              GetStats();
std::vector<Region> GetRegions();

/// Walk all regions and log any that appear leaked (still committed at shutdown).
void ReportLeaks();

/// Visitor callback: return false to stop enumeration.
using RegionVisitorFn = std::function<bool(const Region&)>;
void ForEachRegion(RegionVisitorFn fn);

// ── Phase 8: Simple host allocation aliases ───────────────────────────────
/// Convenience wrapper: allocate host memory with alignment=16.
inline void* AllocHost(size_t size, AllocType type = AllocType::Heap) {
    return Alloc(size, 16, type);
}
/// Convenience wrapper: free host memory.
inline void FreeHost(void* ptr) { Free(ptr); }

/// IsReadable: check if a host address range can be safely read.
bool IsReadable(uintptr_t base, size_t size);

} // namespace PS5x::Memory
