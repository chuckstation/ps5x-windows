// ChuckStation5 – SaveState unit tests
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/SaveState/SaveState.h"
#include "ChuckStation5/Logger/Logger.h"
#include <filesystem>

TEST_CASE("SaveState::Init", "[savestate][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    REQUIRE(ChuckStation5::SaveState::Init("test_savestates"));
    auto dir = ChuckStation5::SaveState::GetSaveDirectory();
    REQUIRE(!dir.empty());
    ChuckStation5::SaveState::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("SaveState::ListSaves empty", "[savestate][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::SaveState::Init("test_savestates_empty");
    auto saves = ChuckStation5::SaveState::ListSaves();
    REQUIRE(saves.empty());
    ChuckStation5::SaveState::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("SaveState::HasSave false", "[savestate][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::SaveState::Init("test_savestates_has");
    REQUIRE_FALSE(ChuckStation5::SaveState::HasSave(0));
    REQUIRE_FALSE(ChuckStation5::SaveState::HasSave(1));
    ChuckStation5::SaveState::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("SaveState::Delete nonexistent", "[savestate][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::SaveState::Init("test_savestates_del");
    REQUIRE_FALSE(ChuckStation5::SaveState::Delete(99));
    ChuckStation5::SaveState::Shutdown();
    ChuckStation5::Logger::Shutdown();
}
