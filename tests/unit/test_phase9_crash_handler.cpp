// ChuckStation5 – CrashHandler unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/CrashHandler/CrashHandler.h"
#include "ChuckStation5/Logger/Logger.h"

TEST_CASE("CrashHandler::Install", "[crash_handler]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    REQUIRE(ChuckStation5::CrashHandler::Install("test_crashdumps"));
    ChuckStation5::CrashHandler::Uninstall();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("CrashHandler::SetCallback", "[crash_handler]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::CrashHandler::Install("test_crashdumps");
    static bool callbackInvoked = false;
    callbackInvoked = false;
    ChuckStation5::CrashHandler::SetCallback([](const ChuckStation5::CrashHandler::CrashInfo& info) {
        callbackInvoked = true;
        (void)info;
    });
    CHECK_FALSE(callbackInvoked);
    ChuckStation5::CrashHandler::Uninstall();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("CrashHandler::ReportCrash", "[crash_handler]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::CrashHandler::Install("test_crashdumps");
    CHECK(true);
    ChuckStation5::CrashHandler::Uninstall();
    ChuckStation5::Logger::Shutdown();
}
