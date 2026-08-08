// PS5x – Kernel unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/Kernel/Kernel.h"

#include <cstring>

TEST_CASE("Kernel – VirtualAlloc + free roundtrip", "[kernel]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Kernel::Init();

    void* mem = PS5x::Kernel::VirtualAlloc(
        nullptr, 4096,
        static_cast<PS5x::Kernel::MapFlags>(
            static_cast<uint32_t>(PS5x::Kernel::MapFlags::Read) |
            static_cast<uint32_t>(PS5x::Kernel::MapFlags::Write)
        )
    );
    REQUIRE(mem != nullptr);

    // Should be writable
    std::memset(mem, 0xAB, 4096);
    REQUIRE(static_cast<uint8_t*>(mem)[0] == 0xAB);

    REQUIRE(PS5x::Kernel::VirtualFree(mem, 4096));

    PS5x::Kernel::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Kernel – FlexHeap alloc + free", "[kernel]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Kernel::Init();

    void* p = PS5x::Kernel::FlexHeapAlloc(1024, 64);
    REQUIRE(p != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(p) % 64 == 0);

    PS5x::Kernel::FlexHeapFree(p);

    PS5x::Kernel::Shutdown();
    PS5x::Logger::Shutdown();
}
