// ChuckStation5 – Config unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Config/Config.h"

TEST_CASE("Config – defaults after Reset", "[config]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Config::Reset();

    const auto& cfg = ChuckStation5::Config::Get();
    REQUIRE(cfg.graphics.width  == 1920);
    REQUIRE(cfg.graphics.height == 1080);
    REQUIRE(cfg.graphics.vsync  == true);
    REQUIRE(cfg.emulator.firmwarePath.empty());

    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Config – ValidateFirmwarePath rejects empty path", "[config]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);

    REQUIRE(!ChuckStation5::Config::ValidateFirmwarePath(""));

    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Config – ValidateFirmwarePath rejects nonexistent path", "[config]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);

    REQUIRE(!ChuckStation5::Config::ValidateFirmwarePath("/nonexistent/firmware/path"));

    ChuckStation5::Logger::Shutdown();
}
