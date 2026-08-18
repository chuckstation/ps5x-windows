// PS5x – SaveState unit tests
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include <catch2/catch_test_macros.hpp>
#include "PS5x/SaveState/SaveState.h"
#include "PS5x/Logger/Logger.h"
#include <filesystem>

TEST_CASE("SaveState::Init", "[savestate][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    REQUIRE(PS5x::SaveState::Init("test_savestates"));
    auto dir = PS5x::SaveState::GetSaveDirectory();
    REQUIRE(!dir.empty());
    PS5x::SaveState::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("SaveState::ListSaves empty", "[savestate][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::SaveState::Init("test_savestates_empty");
    auto saves = PS5x::SaveState::ListSaves();
    REQUIRE(saves.empty());
    PS5x::SaveState::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("SaveState::HasSave false", "[savestate][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::SaveState::Init("test_savestates_has");
    REQUIRE_FALSE(PS5x::SaveState::HasSave(0));
    REQUIRE_FALSE(PS5x::SaveState::HasSave(1));
    PS5x::SaveState::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("SaveState::Delete nonexistent", "[savestate][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::SaveState::Init("test_savestates_del");
    REQUIRE_FALSE(PS5x::SaveState::Delete(99));
    PS5x::SaveState::Shutdown();
    PS5x::Logger::Shutdown();
}
