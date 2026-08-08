// PS5x – KytyAdapter unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/KytyAdapter/KytyAdapter.h"

TEST_CASE("KytyAdapter – Init / Shutdown", "[kyty]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    REQUIRE(PS5x::KytyAdapter::Init());
    PS5x::KytyAdapter::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("KytyAdapter – IsAvailable without Kyty compiled in", "[kyty]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::KytyAdapter::Init();
#ifdef PS5X_KYTY_AVAILABLE
    REQUIRE(PS5x::KytyAdapter::IsAvailable());
#else
    REQUIRE(!PS5x::KytyAdapter::IsAvailable());
#endif
    PS5x::KytyAdapter::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("KytyAdapter – LoadProgram returns error without Kyty", "[kyty]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::KytyAdapter::Init();
    // Without Kyty, LoadProgram always returns -1
    int r = PS5x::KytyAdapter::LoadProgram("/nonexistent.elf");
    REQUIRE(r < 0);
    PS5x::KytyAdapter::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("KytyAdapter – VirtualAlloc delegates to Memory", "[kyty]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Memory::Init();
    PS5x::KytyAdapter::Init();

    void* p = PS5x::KytyAdapter::VirtualAlloc(nullptr, 0x4000, 0x3 /*R|W*/);
    REQUIRE(p != nullptr);
    REQUIRE(PS5x::KytyAdapter::VirtualFree(p));

    PS5x::KytyAdapter::Shutdown();
    PS5x::Memory::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("KytyAdapter – log bridge installs without crash", "[kyty]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::KytyAdapter::Init();
    PS5x::KytyAdapter::InstallLogBridge(); // should not throw or crash
    PS5x::KytyAdapter::Shutdown();
    PS5x::Logger::Shutdown();
}
