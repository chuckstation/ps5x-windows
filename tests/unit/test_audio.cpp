// ChuckStation5 – Audio unit tests (Phase 2 – hardware-resilient)
// SPDX-License-Identifier: MIT
// Audio tests are written to pass in headless CI environments (no sound card).
// Tests that require SDL audio device are skipped gracefully when unavailable.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Audio/Audio.h"

static void InitQuiet()
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
}

TEST_CASE("Audio – MasterVolume get/set (no device needed)", "[audio]")
{
    InitQuiet();
    // Test the atomic volume independently of SDL device
    ChuckStation5::Audio::SetMasterVolume(0.75f);
    REQUIRE_THAT(ChuckStation5::Audio::GetMasterVolume(),
                 Catch::Matchers::WithinRel(0.75f, 0.001f));
    ChuckStation5::Audio::SetMasterVolume(0.0f);
    REQUIRE_THAT(ChuckStation5::Audio::GetMasterVolume(),
                 Catch::Matchers::WithinRel(0.0f, 0.001f));
    ChuckStation5::Audio::SetMasterVolume(1.0f);
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Audio – Init gracefully handles missing audio device", "[audio]")
{
    InitQuiet();
    ChuckStation5::Audio::AudioConfig cfg;
    cfg.sampleRate    = 48000;
    cfg.channels      = 2;
    cfg.bufferSamples = 256;

    // Init may fail in CI (no sound card) – that is acceptable.
    // What must NOT happen: crash, hang, or memory corruption.
    bool ok = ChuckStation5::Audio::Init(cfg);
    // Just shut down cleanly regardless of whether Init succeeded.
    ChuckStation5::Audio::Shutdown();

    // Re-init to clean state
    ChuckStation5::Audio::Init(cfg);
    ChuckStation5::Audio::Shutdown();

    // No assertion on ok – CI has no audio device.
    (void)ok;
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Audio – OpenPort before Init returns INVALID_PORT", "[audio]")
{
    InitQuiet();
    // Ensure shutdown state
    ChuckStation5::Audio::Shutdown();

    ChuckStation5::Audio::PortConfig pc;
    auto h = ChuckStation5::Audio::OpenPort(pc, nullptr);
    REQUIRE(h == ChuckStation5::Audio::INVALID_PORT);
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Audio – Port lifecycle after successful Init", "[audio]")
{
    InitQuiet();
    ChuckStation5::Audio::AudioConfig cfg;
    bool initOk = ChuckStation5::Audio::Init(cfg);

    if (!initOk)
    {
        // No audio device – skip port tests gracefully
        ChuckStation5::Logger::Shutdown();
        return;
    }

    ChuckStation5::Audio::PortConfig pc;
    pc.sampleRate    = 48000;
    pc.channels      = 2;
    pc.bufferSamples = 256;

    auto h = ChuckStation5::Audio::OpenPort(pc, nullptr);
    REQUIRE(h != ChuckStation5::Audio::INVALID_PORT);
    REQUIRE(!ChuckStation5::Audio::IsRunning(h));
    REQUIRE(ChuckStation5::Audio::Start(h));
    REQUIRE(ChuckStation5::Audio::IsRunning(h));
    REQUIRE(ChuckStation5::Audio::Stop(h));
    REQUIRE(!ChuckStation5::Audio::IsRunning(h));
    REQUIRE(ChuckStation5::Audio::ClosePort(h));

    ChuckStation5::Audio::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Audio – SetPortVolume clamps and applies", "[audio]")
{
    InitQuiet();
    bool initOk = ChuckStation5::Audio::Init({});
    if (!initOk) { ChuckStation5::Logger::Shutdown(); return; }

    ChuckStation5::Audio::PortConfig pc;
    auto h = ChuckStation5::Audio::OpenPort(pc, nullptr);
    REQUIRE(h != ChuckStation5::Audio::INVALID_PORT);
    REQUIRE(ChuckStation5::Audio::SetPortVolume(h, 0.5f));
    // Setting volume on unknown handle must not crash
    REQUIRE(!ChuckStation5::Audio::SetPortVolume(999, 0.5f));
    REQUIRE(ChuckStation5::Audio::ClosePort(h));

    ChuckStation5::Audio::Shutdown();
    ChuckStation5::Logger::Shutdown();
}
