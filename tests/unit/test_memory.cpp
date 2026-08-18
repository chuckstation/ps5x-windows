// ChuckStation5 – Memory Manager unit tests (Phase 2)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"

#include <cstring>

static void Setup()
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();
}
static void Teardown()
{
    ChuckStation5::Memory::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Memory – Map + Unmap roundtrip", "[memory]")
{
    Setup();
    uintptr_t base = ChuckStation5::Memory::Map(0, ChuckStation5::Memory::PAGE_SIZE,
                                        ChuckStation5::Memory::Prot::RW,
                                        ChuckStation5::Memory::AllocType::Data, "test-rw");
    REQUIRE(base != 0);
    REQUIRE(ChuckStation5::Memory::IsReadable(base, 8));
    REQUIRE(ChuckStation5::Memory::IsWritable(base, 8));

    // Should be writable
    *reinterpret_cast<uint64_t*>(base) = 0xDEADBEEFCAFEBABEULL;
    REQUIRE(*reinterpret_cast<uint64_t*>(base) == 0xDEADBEEFCAFEBABEULL);

    REQUIRE(ChuckStation5::Memory::Unmap(base, ChuckStation5::Memory::PAGE_SIZE));
    Teardown();
}

TEST_CASE("Memory – FindRegion locates mapped range", "[memory]")
{
    Setup();
    uintptr_t base = ChuckStation5::Memory::Map(0, ChuckStation5::Memory::PAGE_SIZE * 2,
                                        ChuckStation5::Memory::Prot::RW,
                                        ChuckStation5::Memory::AllocType::Data, "find-test");
    REQUIRE(base != 0);

    auto r = ChuckStation5::Memory::FindRegion(base + 100);
    REQUIRE(r.has_value());
    REQUIRE(r->base == base);
    REQUIRE(r->size == ChuckStation5::Memory::PAGE_SIZE * 2);

    // Past the end – should not be found
    auto r2 = ChuckStation5::Memory::FindRegion(base + ChuckStation5::Memory::PAGE_SIZE * 2 + 1);
    REQUIRE(!r2.has_value());

    ChuckStation5::Memory::Unmap(base, ChuckStation5::Memory::PAGE_SIZE * 2);
    Teardown();
}

TEST_CASE("Memory – Protect changes permissions", "[memory]")
{
    Setup();
    uintptr_t base = ChuckStation5::Memory::Map(0, ChuckStation5::Memory::PAGE_SIZE,
                                        ChuckStation5::Memory::Prot::RW,
                                        ChuckStation5::Memory::AllocType::Data, "prot-test");
    REQUIRE(base != 0);

    // Change to read-only
    REQUIRE(ChuckStation5::Memory::Protect(base, ChuckStation5::Memory::PAGE_SIZE, ChuckStation5::Memory::Prot::Read));

    ChuckStation5::Memory::Unmap(base, ChuckStation5::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("Memory – Alloc/Free with alignment", "[memory]")
{
    Setup();
    void* p = ChuckStation5::Memory::Alloc(256, 64, ChuckStation5::Memory::AllocType::Heap);
    REQUIRE(p != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(p) % 64 == 0);
    std::memset(p, 0xAB, 256);

    auto stats = ChuckStation5::Memory::GetStats();
    REQUIRE(stats.totalAllocated >= 256);

    ChuckStation5::Memory::Free(p);

    auto stats2 = ChuckStation5::Memory::GetStats();
    REQUIRE(stats2.totalAllocated == 0);
    Teardown();
}

TEST_CASE("Memory – Stats track regions", "[memory]")
{
    Setup();
    auto s0 = ChuckStation5::Memory::GetStats();
    REQUIRE(s0.regionCount == 0);

    uintptr_t a = ChuckStation5::Memory::Map(0, ChuckStation5::Memory::PAGE_SIZE,
                                     ChuckStation5::Memory::Prot::RW,
                                     ChuckStation5::Memory::AllocType::Data, "s1");
    uintptr_t b = ChuckStation5::Memory::Map(0, ChuckStation5::Memory::PAGE_SIZE,
                                     ChuckStation5::Memory::Prot::Read,
                                     ChuckStation5::Memory::AllocType::Code, "s2");

    auto s1 = ChuckStation5::Memory::GetStats();
    REQUIRE(s1.regionCount == 2);
    REQUIRE(s1.totalCommitted >= ChuckStation5::Memory::PAGE_SIZE * 2);

    ChuckStation5::Memory::Unmap(a, ChuckStation5::Memory::PAGE_SIZE);
    ChuckStation5::Memory::Unmap(b, ChuckStation5::Memory::PAGE_SIZE);

    auto s2 = ChuckStation5::Memory::GetStats();
    REQUIRE(s2.regionCount == 0);
    Teardown();
}

TEST_CASE("Memory – ForEachRegion visits all", "[memory]")
{
    Setup();
    uintptr_t a = ChuckStation5::Memory::Map(0, ChuckStation5::Memory::PAGE_SIZE,
                                     ChuckStation5::Memory::Prot::RW,
                                     ChuckStation5::Memory::AllocType::Data, "a");
    uintptr_t b = ChuckStation5::Memory::Map(0, ChuckStation5::Memory::PAGE_SIZE,
                                     ChuckStation5::Memory::Prot::RW,
                                     ChuckStation5::Memory::AllocType::Data, "b");

    int count = 0;
    ChuckStation5::Memory::ForEachRegion([&](const ChuckStation5::Memory::Region&) {
        ++count; return true;
    });
    REQUIRE(count == 2);

    ChuckStation5::Memory::Unmap(a, ChuckStation5::Memory::PAGE_SIZE);
    ChuckStation5::Memory::Unmap(b, ChuckStation5::Memory::PAGE_SIZE);
    Teardown();
}
