// PS5x – Memory Manager unit tests (Phase 2)
// SPDX-License-Identifier: MIT
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include <catch2/catch_test_macros.hpp>

#include <cstring>

static void Setup() {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  PS5x::Memory::Init();
}
static void Teardown() {
  PS5x::Memory::Shutdown();
  PS5x::Logger::Shutdown();
}

TEST_CASE("Memory – Map + Unmap roundtrip", "[memory]") {
  Setup();
  uintptr_t base =
      PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE, PS5x::Memory::Prot::RW,
                        PS5x::Memory::AllocType::Data, "test-rw");
  REQUIRE(base != 0);
  REQUIRE(PS5x::Memory::IsReadable(base, 8));
  REQUIRE(PS5x::Memory::IsWritable(base, 8));

  // Should be writable
  *reinterpret_cast<uint64_t *>(base) = 0xDEADBEEFCAFEBABEULL;
  REQUIRE(*reinterpret_cast<uint64_t *>(base) == 0xDEADBEEFCAFEBABEULL);

  REQUIRE(PS5x::Memory::Unmap(base, PS5x::Memory::PAGE_SIZE));
  Teardown();
}

TEST_CASE("Memory – FindRegion locates mapped range", "[memory]") {
  Setup();
  uintptr_t base =
      PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE * 2, PS5x::Memory::Prot::RW,
                        PS5x::Memory::AllocType::Data, "find-test");
  REQUIRE(base != 0);

  auto r = PS5x::Memory::FindRegion(base + 100);
  REQUIRE(r.has_value());
  REQUIRE(r->base == base);
  REQUIRE(r->size == PS5x::Memory::PAGE_SIZE * 2);

  // Past the end – should not be found
  auto r2 = PS5x::Memory::FindRegion(base + PS5x::Memory::PAGE_SIZE * 2 + 1);
  REQUIRE(!r2.has_value());

  PS5x::Memory::Unmap(base, PS5x::Memory::PAGE_SIZE * 2);
  Teardown();
}

TEST_CASE("Memory – Protect changes permissions", "[memory]") {
  Setup();
  uintptr_t base =
      PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE, PS5x::Memory::Prot::RW,
                        PS5x::Memory::AllocType::Data, "prot-test");
  REQUIRE(base != 0);

  // Change to read-only
  REQUIRE(PS5x::Memory::Protect(base, PS5x::Memory::PAGE_SIZE,
                                PS5x::Memory::Prot::Read));

  PS5x::Memory::Unmap(base, PS5x::Memory::PAGE_SIZE);
  Teardown();
}

TEST_CASE("Memory – Alloc/Free with alignment", "[memory]") {
  Setup();
  void *p = PS5x::Memory::Alloc(256, 64, PS5x::Memory::AllocType::Heap);
  REQUIRE(p != nullptr);
  REQUIRE(reinterpret_cast<uintptr_t>(p) % 64 == 0);
  std::memset(p, 0xAB, 256);

  auto stats = PS5x::Memory::GetStats();
  REQUIRE(stats.totalAllocated >= 256);

  PS5x::Memory::Free(p);

  auto stats2 = PS5x::Memory::GetStats();
  REQUIRE(stats2.totalAllocated == 0);
  Teardown();
}

TEST_CASE("Memory – Stats track regions", "[memory]") {
  Setup();
  auto s0 = PS5x::Memory::GetStats();
  REQUIRE(s0.regionCount == 0);

  uintptr_t a =
      PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE, PS5x::Memory::Prot::RW,
                        PS5x::Memory::AllocType::Data, "s1");
  uintptr_t b =
      PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE, PS5x::Memory::Prot::Read,
                        PS5x::Memory::AllocType::Code, "s2");

  auto s1 = PS5x::Memory::GetStats();
  REQUIRE(s1.regionCount == 2);
  REQUIRE(s1.totalCommitted >= PS5x::Memory::PAGE_SIZE * 2);

  PS5x::Memory::Unmap(a, PS5x::Memory::PAGE_SIZE);
  PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE);

  auto s2 = PS5x::Memory::GetStats();
  REQUIRE(s2.regionCount == 0);
  Teardown();
}

TEST_CASE("Memory – ForEachRegion visits all", "[memory]") {
  Setup();
  uintptr_t a =
      PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE, PS5x::Memory::Prot::RW,
                        PS5x::Memory::AllocType::Data, "a");
  uintptr_t b =
      PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE, PS5x::Memory::Prot::RW,
                        PS5x::Memory::AllocType::Data, "b");

  int count = 0;
  PS5x::Memory::ForEachRegion([&](const PS5x::Memory::Region &) {
    ++count;
    return true;
  });
  REQUIRE(count == 2);

  PS5x::Memory::Unmap(a, PS5x::Memory::PAGE_SIZE);
  PS5x::Memory::Unmap(b, PS5x::Memory::PAGE_SIZE);
  Teardown();
}
