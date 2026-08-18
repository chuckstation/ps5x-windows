// ChuckStation5 – Kernel unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Kernel/Kernel.h"

#include <cstring>

TEST_CASE("Kernel – VirtualAlloc + free roundtrip", "[kernel]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Kernel::Init();

    void* mem = ChuckStation5::Kernel::VirtualAlloc(
        nullptr, 4096,
        static_cast<ChuckStation5::Kernel::MapFlags>(
            static_cast<uint32_t>(ChuckStation5::Kernel::MapFlags::Read) |
            static_cast<uint32_t>(ChuckStation5::Kernel::MapFlags::Write)
        )
    );
    REQUIRE(mem != nullptr);

    // Should be writable
    std::memset(mem, 0xAB, 4096);
    REQUIRE(static_cast<uint8_t*>(mem)[0] == 0xAB);

    REQUIRE(ChuckStation5::Kernel::VirtualFree(mem, 4096));

    ChuckStation5::Kernel::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Kernel – FlexHeap alloc + free", "[kernel]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Kernel::Init();

    void* p = ChuckStation5::Kernel::FlexHeapAlloc(1024, 64);
    REQUIRE(p != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(p) % 64 == 0);

    ChuckStation5::Kernel::FlexHeapFree(p);

    ChuckStation5::Kernel::Shutdown();
    ChuckStation5::Logger::Shutdown();
}
