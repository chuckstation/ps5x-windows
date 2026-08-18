// ChuckStation5 – KytyAdapter unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/KytyAdapter/KytyAdapter.h"

TEST_CASE("KytyAdapter – Init / Shutdown", "[kyty]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    REQUIRE(ChuckStation5::KytyAdapter::Init());
    ChuckStation5::KytyAdapter::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("KytyAdapter – IsAvailable without Kyty compiled in", "[kyty]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::KytyAdapter::Init();
#ifdef CHUCKSTATION5_KYTY_AVAILABLE
    REQUIRE(ChuckStation5::KytyAdapter::IsAvailable());
#else
    REQUIRE(!ChuckStation5::KytyAdapter::IsAvailable());
#endif
    ChuckStation5::KytyAdapter::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("KytyAdapter – LoadProgram returns error without Kyty", "[kyty]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::KytyAdapter::Init();
    // Without Kyty, LoadProgram always returns -1
    int r = ChuckStation5::KytyAdapter::LoadProgram("/nonexistent.elf");
    REQUIRE(r < 0);
    ChuckStation5::KytyAdapter::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("KytyAdapter – VirtualAlloc delegates to Memory", "[kyty]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();
    ChuckStation5::KytyAdapter::Init();

    void* p = ChuckStation5::KytyAdapter::VirtualAlloc(nullptr, 0x4000, 0x3 /*R|W*/);
    REQUIRE(p != nullptr);
    REQUIRE(ChuckStation5::KytyAdapter::VirtualFree(p));

    ChuckStation5::KytyAdapter::Shutdown();
    ChuckStation5::Memory::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("KytyAdapter – log bridge installs without crash", "[kyty]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::KytyAdapter::Init();
    ChuckStation5::KytyAdapter::InstallLogBridge(); // should not throw or crash
    ChuckStation5::KytyAdapter::Shutdown();
    ChuckStation5::Logger::Shutdown();
}
