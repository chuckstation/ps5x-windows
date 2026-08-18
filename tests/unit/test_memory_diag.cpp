// PS5x – Memory Diagnostics tests (Phase 4)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/MemoryDiag/MemoryDiag.h"

#include <cstring>

using namespace PS5x::MemoryDiag;

static void Setup()
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Memory::Init();
    Init();
}
static void Teardown()
{
    Shutdown();
    PS5x::Memory::Shutdown();
    PS5x::Logger::Shutdown();
}

// ── Snapshot ──────────────────────────────────────────────────────────────

TEST_CASE("MemDiag – TakeSnapshot captures state", "[memdiag]")
{
    Setup();
    TakeSnapshot("initial");
    auto snaps = GetSnapshots();
    REQUIRE(snaps.size() == 1);
    REQUIRE(snaps[0].label == "initial");
    REQUIRE(snaps[0].timestampUs > 0);
    Teardown();
}

TEST_CASE("MemDiag – Multiple snapshots accumulate", "[memdiag]")
{
    Setup();
    TakeSnapshot("A");
    uintptr_t b = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                     PS5x::Memory::Prot::RW,
                                     PS5x::Memory::AllocType::Data, "diag-test");
    TakeSnapshot("B");
    auto snaps = GetSnapshots();
    REQUIRE(snaps.size() == 2);
    REQUIRE(snaps[1].stats.regionCount > snaps[0].stats.regionCount);
    PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("MemDiag – ClearSnapshots empties list", "[memdiag]")
{
    Setup();
    TakeSnapshot("x"); TakeSnapshot("y");
    REQUIRE(GetSnapshots().size() == 2);
    ClearSnapshots();
    REQUIRE(GetSnapshots().empty());
    Teardown();
}

TEST_CASE("MemDiag – DiffSnapshots detects new region", "[memdiag]")
{
    Setup();
    TakeSnapshot("before");
    uintptr_t b = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                     PS5x::Memory::Prot::RW,
                                     PS5x::Memory::AllocType::Data, "diff-region");
    TakeSnapshot("after");
    auto snaps = GetSnapshots();
    REQUIRE(snaps.size() == 2);
    std::string diff = DiffSnapshots(snaps[0], snaps[1]);
    REQUIRE(!diff.empty());
    REQUIRE(diff.find("+") != std::string::npos); // new region detected
    PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("MemDiag – DiffSnapshots detects removed region", "[memdiag]")
{
    Setup();
    uintptr_t b = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                     PS5x::Memory::Prot::RW,
                                     PS5x::Memory::AllocType::Data, "remove-me");
    TakeSnapshot("with-region");
    PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE);
    TakeSnapshot("without-region");
    auto snaps = GetSnapshots();
    std::string diff = DiffSnapshots(snaps[0], snaps[1]);
    REQUIRE(diff.find("-") != std::string::npos); // removed region detected
    Teardown();
}

// ── Allocation history ─────────────────────────────────────────────────────

TEST_CASE("MemDiag – History disabled by default", "[memdiag]")
{
    Setup();
    REQUIRE(!IsHistoryEnabled());
    RecordEvent({AllocEventType::Map, 0x1000, 4096});
    REQUIRE(GetHistory().empty()); // not recorded when disabled
    Teardown();
}

TEST_CASE("MemDiag – EnableHistory records events", "[memdiag]")
{
    Setup();
    EnableHistory(true);
    REQUIRE(IsHistoryEnabled());

    AllocEvent ev;
    ev.type      = AllocEventType::Map;
    ev.address   = 0x2000;
    ev.size      = 8192;
    ev.tag       = "test-alloc";
    ev.timestampUs = 12345;
    RecordEvent(ev);

    AllocEvent ev2;
    ev2.type    = AllocEventType::Alloc;
    ev2.address = 0x3000;
    ev2.size    = 64;
    RecordEvent(ev2);

    auto hist = GetHistory();
    REQUIRE(hist.size() == 2);
    REQUIRE(hist[0].address == 0x2000);
    REQUIRE(hist[0].tag == "test-alloc");
    REQUIRE(hist[1].type == AllocEventType::Alloc);

    EnableHistory(false);
    Teardown();
}

TEST_CASE("MemDiag – ClearHistory empties log", "[memdiag]")
{
    Setup();
    EnableHistory(true);
    RecordEvent({AllocEventType::Map, 0x1000, 4096});
    RecordEvent({AllocEventType::Unmap, 0x1000, 4096});
    REQUIRE(GetHistory().size() == 2);
    ClearHistory();
    REQUIRE(GetHistory().empty());
    EnableHistory(false);
    Teardown();
}

// ── Fragmentation ──────────────────────────────────────────────────────────

TEST_CASE("MemDiag – ComputeFragmentation with no regions", "[memdiag]")
{
    Setup();
    auto rep = ComputeFragmentation();
    REQUIRE(rep.regionCount    == 0);
    REQUIRE(rep.gapCount       == 0);
    REQUIRE(rep.fragmentationPct == 0.0);
    Teardown();
}

TEST_CASE("MemDiag – ComputeFragmentation with regions", "[memdiag]")
{
    Setup();
    uintptr_t a = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                     PS5x::Memory::Prot::RW,
                                     PS5x::Memory::AllocType::Data, "frag-a");
    uintptr_t b = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE * 2,
                                     PS5x::Memory::Prot::RW,
                                     PS5x::Memory::AllocType::Data, "frag-b");

    auto rep = ComputeFragmentation();
    REQUIRE(rep.regionCount >= 2);
    REQUIRE(rep.totalCommittedBytes >= PS5x::Memory::PAGE_SIZE * 3);
    REQUIRE(rep.fragmentationPct > 0.0);

    PS5x::Memory::Unmap(a, PS5x::Memory::PAGE_SIZE);
    PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE * 2);
    Teardown();
}

// ── Statistics overlay ─────────────────────────────────────────────────────

TEST_CASE("MemDiag – GetOverlay returns valid stats", "[memdiag]")
{
    Setup();
    uintptr_t b = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                     PS5x::Memory::Prot::RW,
                                     PS5x::Memory::AllocType::Data, "overlay-test");

    auto ov = GetOverlay();
    REQUIRE(ov.committed >= PS5x::Memory::PAGE_SIZE);
    REQUIRE(ov.regionCount >= 1);

    PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("MemDiag – FormatOverlay returns non-empty string", "[memdiag]")
{
    Setup();
    uintptr_t b = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                     PS5x::Memory::Prot::RW,
                                     PS5x::Memory::AllocType::Data, "fmt");
    auto s = FormatOverlay();
    REQUIRE(!s.empty());
    REQUIRE(s.find("Mem:") != std::string::npos);
    PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE);
    Teardown();
}

// ── Diagnostics ───────────────────────────────────────────────────────────

TEST_CASE("MemDiag – DumpMemoryMap does not crash", "[memdiag]")
{
    Setup();
    uintptr_t b = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                     PS5x::Memory::Prot::RW,
                                     PS5x::Memory::AllocType::Data, "dump-test");
    DumpMemoryMap();
    DumpFragmentation();
    PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("MemDiag – SearchPattern finds known bytes", "[memdiag]")
{
    Setup();
    uintptr_t b = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                     PS5x::Memory::Prot::RW,
                                     PS5x::Memory::AllocType::Data, "search");
    // Write a distinctive pattern at known offset
    auto* ptr = reinterpret_cast<uint8_t*>(b + 64);
    const uint8_t pattern[] = {0xDE, 0xAD, 0xBE, 0xEF};
    std::memcpy(ptr, pattern, 4);

    auto hits = SearchPattern(pattern, 4);
    REQUIRE(!hits.empty());
    bool found = false;
    for (auto addr : hits)
        if (addr == b + 64) { found = true; break; }
    REQUIRE(found);

    PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("MemDiag – SearchPattern with empty pattern returns empty", "[memdiag]")
{
    Setup();
    auto hits = SearchPattern(nullptr, 0);
    REQUIRE(hits.empty());
    Teardown();
}
