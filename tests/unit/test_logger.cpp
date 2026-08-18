// PS5x – Logger unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"

#include <string>
#include <vector>

TEST_CASE("Logger – level filtering", "[logger]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Warning);

    REQUIRE(!PS5x::Logger::IsEnabled(PS5x::Logger::Level::Trace));
    REQUIRE(!PS5x::Logger::IsEnabled(PS5x::Logger::Level::Debug));
    REQUIRE(!PS5x::Logger::IsEnabled(PS5x::Logger::Level::Info));
    REQUIRE( PS5x::Logger::IsEnabled(PS5x::Logger::Level::Warning));
    REQUIRE( PS5x::Logger::IsEnabled(PS5x::Logger::Level::Error));
    REQUIRE( PS5x::Logger::IsEnabled(PS5x::Logger::Level::Fatal));

    PS5x::Logger::Shutdown();
}

TEST_CASE("Logger – sink receives messages", "[logger]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Trace);

    std::vector<std::string> received;
    PS5x::Logger::AddSink([&](PS5x::Logger::Level, std::string_view, std::string_view msg) {
        received.emplace_back(msg);
    });

    PS5X_INFO("hello %s", "world");
    PS5X_WARN("count=%d", 42);

    REQUIRE(received.size() == 2);
    REQUIRE(received[0].find("hello world") != std::string::npos);
    REQUIRE(received[1].find("count=42")    != std::string::npos);

    PS5x::Logger::Shutdown();
}

TEST_CASE("Logger – SetLevel changes filter at runtime", "[logger]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    REQUIRE(!PS5x::Logger::IsEnabled(PS5x::Logger::Level::Fatal));

    PS5x::Logger::SetLevel(PS5x::Logger::Level::Trace);
    REQUIRE( PS5x::Logger::IsEnabled(PS5x::Logger::Level::Trace));

    PS5x::Logger::Shutdown();
}
