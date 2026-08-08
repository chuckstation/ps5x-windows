// PS5x – Config unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/Config/Config.h"

TEST_CASE("Config – defaults after Reset", "[config]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Config::Reset();

    const auto& cfg = PS5x::Config::Get();
    REQUIRE(cfg.graphics.width  == 1920);
    REQUIRE(cfg.graphics.height == 1080);
    REQUIRE(cfg.graphics.vsync  == true);
    REQUIRE(cfg.emulator.firmwarePath.empty());

    PS5x::Logger::Shutdown();
}

TEST_CASE("Config – ValidateFirmwarePath rejects empty path", "[config]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);

    REQUIRE(!PS5x::Config::ValidateFirmwarePath(""));

    PS5x::Logger::Shutdown();
}

TEST_CASE("Config – ValidateFirmwarePath rejects nonexistent path", "[config]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);

    REQUIRE(!PS5x::Config::ValidateFirmwarePath("/nonexistent/firmware/path"));

    PS5x::Logger::Shutdown();
}
