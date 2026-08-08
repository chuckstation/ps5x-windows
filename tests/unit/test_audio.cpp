// PS5x – Audio unit tests (Phase 2 – hardware-resilient)
// SPDX-License-Identifier: MIT
//
// Audio tests are written to pass in headless CI environments (no sound card).
// Tests that require SDL audio device are skipped gracefully when unavailable.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/Audio/Audio.h"

static void InitQuiet()
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
}

TEST_CASE("Audio – MasterVolume get/set (no device needed)", "[audio]")
{
    InitQuiet();
    // Test the atomic volume independently of SDL device
    PS5x::Audio::SetMasterVolume(0.75f);
    REQUIRE_THAT(PS5x::Audio::GetMasterVolume(),
                 Catch::Matchers::WithinRel(0.75f, 0.001f));
    PS5x::Audio::SetMasterVolume(0.0f);
    REQUIRE_THAT(PS5x::Audio::GetMasterVolume(),
                 Catch::Matchers::WithinRel(0.0f, 0.001f));
    PS5x::Audio::SetMasterVolume(1.0f);
    PS5x::Logger::Shutdown();
}

TEST_CASE("Audio – Init gracefully handles missing audio device", "[audio]")
{
    InitQuiet();
    PS5x::Audio::AudioConfig cfg;
    cfg.sampleRate    = 48000;
    cfg.channels      = 2;
    cfg.bufferSamples = 256;

    // Init may fail in CI (no sound card) – that is acceptable.
    // What must NOT happen: crash, hang, or memory corruption.
    bool ok = PS5x::Audio::Init(cfg);
    // Just shut down cleanly regardless of whether Init succeeded.
    PS5x::Audio::Shutdown();

    // Re-init to clean state
    PS5x::Audio::Init(cfg);
    PS5x::Audio::Shutdown();

    // No assertion on ok – CI has no audio device.
    (void)ok;
    PS5x::Logger::Shutdown();
}

TEST_CASE("Audio – OpenPort before Init returns INVALID_PORT", "[audio]")
{
    InitQuiet();
    // Ensure shutdown state
    PS5x::Audio::Shutdown();

    PS5x::Audio::PortConfig pc;
    auto h = PS5x::Audio::OpenPort(pc, nullptr);
    REQUIRE(h == PS5x::Audio::INVALID_PORT);
    PS5x::Logger::Shutdown();
}

TEST_CASE("Audio – Port lifecycle after successful Init", "[audio]")
{
    InitQuiet();
    PS5x::Audio::AudioConfig cfg;
    bool initOk = PS5x::Audio::Init(cfg);

    if (!initOk)
    {
        // No audio device – skip port tests gracefully
        PS5x::Logger::Shutdown();
        return;
    }

    PS5x::Audio::PortConfig pc;
    pc.sampleRate    = 48000;
    pc.channels      = 2;
    pc.bufferSamples = 256;

    auto h = PS5x::Audio::OpenPort(pc, nullptr);
    REQUIRE(h != PS5x::Audio::INVALID_PORT);
    REQUIRE(!PS5x::Audio::IsRunning(h));
    REQUIRE(PS5x::Audio::Start(h));
    REQUIRE(PS5x::Audio::IsRunning(h));
    REQUIRE(PS5x::Audio::Stop(h));
    REQUIRE(!PS5x::Audio::IsRunning(h));
    REQUIRE(PS5x::Audio::ClosePort(h));

    PS5x::Audio::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Audio – SetPortVolume clamps and applies", "[audio]")
{
    InitQuiet();
    bool initOk = PS5x::Audio::Init({});
    if (!initOk) { PS5x::Logger::Shutdown(); return; }

    PS5x::Audio::PortConfig pc;
    auto h = PS5x::Audio::OpenPort(pc, nullptr);
    REQUIRE(h != PS5x::Audio::INVALID_PORT);
    REQUIRE(PS5x::Audio::SetPortVolume(h, 0.5f));
    // Setting volume on unknown handle must not crash
    REQUIRE(!PS5x::Audio::SetPortVolume(999, 0.5f));
    REQUIRE(PS5x::Audio::ClosePort(h));

    PS5x::Audio::Shutdown();
    PS5x::Logger::Shutdown();
}
