// ChuckStation5 – Logger unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"

#include <string>
#include <vector>

TEST_CASE("Logger – level filtering", "[logger]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Warning);

    REQUIRE(!ChuckStation5::Logger::IsEnabled(ChuckStation5::Logger::Level::Trace));
    REQUIRE(!ChuckStation5::Logger::IsEnabled(ChuckStation5::Logger::Level::Debug));
    REQUIRE(!ChuckStation5::Logger::IsEnabled(ChuckStation5::Logger::Level::Info));
    REQUIRE( ChuckStation5::Logger::IsEnabled(ChuckStation5::Logger::Level::Warning));
    REQUIRE( ChuckStation5::Logger::IsEnabled(ChuckStation5::Logger::Level::Error));
    REQUIRE( ChuckStation5::Logger::IsEnabled(ChuckStation5::Logger::Level::Fatal));

    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Logger – sink receives messages", "[logger]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Trace);

    std::vector<std::string> received;
    ChuckStation5::Logger::AddSink([&](ChuckStation5::Logger::Level, std::string_view, std::string_view msg) {
        received.emplace_back(msg);
    });

    CHUCKSTATION5_INFO("hello %s", "world");
    CHUCKSTATION5_WARN("count=%d", 42);

    REQUIRE(received.size() == 2);
    REQUIRE(received[0].find("hello world") != std::string::npos);
    REQUIRE(received[1].find("count=42")    != std::string::npos);

    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Logger – SetLevel changes filter at runtime", "[logger]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    REQUIRE(!ChuckStation5::Logger::IsEnabled(ChuckStation5::Logger::Level::Fatal));

    ChuckStation5::Logger::SetLevel(ChuckStation5::Logger::Level::Trace);
    REQUIRE( ChuckStation5::Logger::IsEnabled(ChuckStation5::Logger::Level::Trace));

    ChuckStation5::Logger::Shutdown();
}
