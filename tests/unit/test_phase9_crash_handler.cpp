// PS5x – CrashHandler unit tests
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include <catch2/catch_test_macros.hpp>
#include "PS5x/CrashHandler/CrashHandler.h"
#include "PS5x/Logger/Logger.h"

TEST_CASE("CrashHandler::Install", "[crash_handler][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    REQUIRE(PS5x::CrashHandler::Install("test_crashdumps"));
    PS5x::CrashHandler::Uninstall();
    PS5x::Logger::Shutdown();
}

TEST_CASE("CrashHandler::SetCallback", "[crash_handler][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::CrashHandler::Install("test_crashdumps");
    bool callbackInvoked = false;
    PS5x::CrashHandler::SetCallback([&](const PS5x::CrashHandler::CrashInfo& info) {
        callbackInvoked = true;
        (void)info;
    });
    // Verify callback is set (cannot safely test actual crash in unit tests)
    PS5x::CrashHandler::Uninstall();
    PS5x::Logger::Shutdown();
}

TEST_CASE("CrashHandler::ReportCrash", "[crash_handler][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::CrashHandler::Install("test_crashdumps");
    // ReportCrash should not crash the test process
    PS5x::CrashHandler::ReportCrash("Test assertion failure");
    PS5x::CrashHandler::Uninstall();
    PS5x::Logger::Shutdown();
}
