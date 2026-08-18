// ChuckStation5 – InputMapping unit tests
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ChuckStation5/InputMapping/InputMapping.h"
#include "ChuckStation5/Logger/Logger.h"

TEST_CASE("InputMapping::Init", "[input_mapping][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    REQUIRE(ChuckStation5::InputMapping::Init());
    ChuckStation5::InputMapping::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("InputMapping::DefaultKeyboardProfile", "[input_mapping][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::InputMapping::Init();
    auto profile = ChuckStation5::InputMapping::DefaultKeyboardProfile();
    REQUIRE(profile.name == "Keyboard");
    REQUIRE_FALSE(profile.entries.empty());
    ChuckStation5::InputMapping::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("InputMapping::DefaultXboxProfile", "[input_mapping][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::InputMapping::Init();
    auto profile = ChuckStation5::InputMapping::DefaultXboxProfile();
    REQUIRE(profile.name == "Xbox");
    REQUIRE_FALSE(profile.entries.empty());
    ChuckStation5::InputMapping::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("InputMapping::DefaultDualSenseProfile", "[input_mapping][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::InputMapping::Init();
    auto profile = ChuckStation5::InputMapping::DefaultDualSenseProfile();
    REQUIRE(profile.name == "DualSense");
    REQUIRE_FALSE(profile.entries.empty());
    ChuckStation5::InputMapping::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("InputMapping::TranslateAxis with deadzone", "[input_mapping][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::InputMapping::Init();
    ChuckStation5::InputMapping::SetActiveProfile(ChuckStation5::InputMapping::DefaultDualSenseProfile());
    // Value below deadzone should be zeroed
    float result = ChuckStation5::InputMapping::TranslateAxis(
        ChuckStation5::InputMapping::HostInputType::Gamepad, static_cast<uint32_t>(ChuckStation5::InputMapping::Ps5Button::L2), 0.03f);
    REQUIRE_THAT(result, Catch::Matchers::WithinAbs(0.0, 0.01));
    // Value above deadzone should pass through
    result = ChuckStation5::InputMapping::TranslateAxis(
        ChuckStation5::InputMapping::HostInputType::Gamepad, static_cast<uint32_t>(ChuckStation5::InputMapping::Ps5Button::L2), 0.8f);
    REQUIRE(std::abs(result) > 0.0f);
    ChuckStation5::InputMapping::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("InputMapping::SetActiveProfile", "[input_mapping][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::InputMapping::Init();
    auto kb = ChuckStation5::InputMapping::DefaultKeyboardProfile();
    ChuckStation5::InputMapping::SetActiveProfile(kb);
    const auto& active = ChuckStation5::InputMapping::GetActiveProfile();
    REQUIRE(active.name == "Keyboard");
    ChuckStation5::InputMapping::Shutdown();
    ChuckStation5::Logger::Shutdown();
}
