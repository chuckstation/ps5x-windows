// PS5x – InputMapping unit tests
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/InputMapping/InputMapping.h"
#include "PS5x/Logger/Logger.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("InputMapping::Init", "[input_mapping][phase9]") {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  REQUIRE(PS5x::InputMapping::Init());
  PS5x::InputMapping::Shutdown();
  PS5x::Logger::Shutdown();
}

TEST_CASE("InputMapping::DefaultKeyboardProfile", "[input_mapping][phase9]") {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  PS5x::InputMapping::Init();
  auto profile = PS5x::InputMapping::DefaultKeyboardProfile();
  REQUIRE(profile.name == "Keyboard");
  REQUIRE_FALSE(profile.entries.empty());
  PS5x::InputMapping::Shutdown();
  PS5x::Logger::Shutdown();
}

TEST_CASE("InputMapping::DefaultXboxProfile", "[input_mapping][phase9]") {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  PS5x::InputMapping::Init();
  auto profile = PS5x::InputMapping::DefaultXboxProfile();
  REQUIRE(profile.name == "Xbox");
  REQUIRE_FALSE(profile.entries.empty());
  PS5x::InputMapping::Shutdown();
  PS5x::Logger::Shutdown();
}

TEST_CASE("InputMapping::DefaultDualSenseProfile", "[input_mapping][phase9]") {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  PS5x::InputMapping::Init();
  auto profile = PS5x::InputMapping::DefaultDualSenseProfile();
  REQUIRE(profile.name == "DualSense");
  REQUIRE_FALSE(profile.entries.empty());
  PS5x::InputMapping::Shutdown();
  PS5x::Logger::Shutdown();
}

TEST_CASE("InputMapping::TranslateAxis with deadzone",
          "[input_mapping][phase9]") {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  PS5x::InputMapping::Init();
  PS5x::InputMapping::SetActiveProfile(
      PS5x::InputMapping::DefaultDualSenseProfile());
  // Value below deadzone should be zeroed
  float result = PS5x::InputMapping::TranslateAxis(
      PS5x::InputMapping::HostInputType::Gamepad,
      static_cast<uint32_t>(PS5x::InputMapping::Ps5Button::L2), 0.03f);
  REQUIRE_THAT(result, Catch::Matchers::WithinAbs(0.0, 0.01));
  // Value above deadzone should pass through
  result = PS5x::InputMapping::TranslateAxis(
      PS5x::InputMapping::HostInputType::Gamepad,
      static_cast<uint32_t>(PS5x::InputMapping::Ps5Button::L2), 0.8f);
  REQUIRE(std::abs(result) > 0.0f);
  PS5x::InputMapping::Shutdown();
  PS5x::Logger::Shutdown();
}

TEST_CASE("InputMapping::SetActiveProfile", "[input_mapping][phase9]") {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  PS5x::InputMapping::Init();
  auto kb = PS5x::InputMapping::DefaultKeyboardProfile();
  PS5x::InputMapping::SetActiveProfile(kb);
  const auto &active = PS5x::InputMapping::GetActiveProfile();
  REQUIRE(active.name == "Keyboard");
  PS5x::InputMapping::Shutdown();
  PS5x::Logger::Shutdown();
}
