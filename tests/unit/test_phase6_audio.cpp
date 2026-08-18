// ChuckStation5 – Phase 6 Audio tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ChuckStation5/Audio/Audio.h"

using namespace ChuckStation5::Audio;

// ── Stats ──────────────────────────────────────────────────────────────────

TEST_CASE("Phase6::Audio::Stats::DefaultStats", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    auto s = GetStats();
    CHECK(s.framesRendered == 0);
    CHECK(s.underruns      == 0);
    CHECK(s.overruns       == 0);
    CHECK(s.activePorts    == 0);
    Shutdown();
}

TEST_CASE("Phase6::Audio::Stats::ResetStats", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    ResetStats();
    auto s = GetStats();
    CHECK(s.framesRendered == 0);
    CHECK(s.underruns      == 0);
    Shutdown();
}

TEST_CASE("Phase6::Audio::Stats::ActivePortsTracked", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    PortConfig pc;
    auto h = OpenPort(pc, [](void*, uint32_t){});
    Start(h);
    auto s = GetStats();
    CHECK(s.activePorts >= 1);
    Stop(h);
    ClosePort(h);
    Shutdown();
}

// ── Channel volumes ────────────────────────────────────────────────────────

TEST_CASE("Phase6::Audio::ChannelVol::SetAndGet", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    PortConfig pc;
    auto h = OpenPort(pc, [](void*, uint32_t){});

    float set[8] = {0.5f, 0.8f, 1.0f, 0.3f, 0.9f, 0.1f, 0.7f, 0.4f};
    CHECK(SetChannelVolumes(h, set));

    float got[8] = {};
    CHECK(GetChannelVolumes(h, got));
    for (int i = 0; i < 8; ++i)
        CHECK(got[i] == Catch::Approx(set[i]).epsilon(0.001));

    ClosePort(h);
    Shutdown();
}

TEST_CASE("Phase6::Audio::ChannelVol::InvalidPort", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    float vols[8] = {};
    CHECK_FALSE(SetChannelVolumes(INVALID_PORT, vols));
    CHECK_FALSE(GetChannelVolumes(INVALID_PORT, vols));
    Shutdown();
}

TEST_CASE("Phase6::Audio::ChannelVol::ClampToOne", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    PortConfig pc;
    auto h = OpenPort(pc, [](void*, uint32_t){});
    float set[8] = {2.0f, -0.5f, 1.0f, 0.f, 0.f, 0.f, 0.f, 0.f};
    CHECK(SetChannelVolumes(h, set));
    float got[8] = {};
    GetChannelVolumes(h, got);
    CHECK(got[0] <= 1.0f);
    CHECK(got[1] >= 0.0f);
    ClosePort(h);
    Shutdown();
}

// ── Spatial position ───────────────────────────────────────────────────────

TEST_CASE("Phase6::Audio::Spatial::SetPosition", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    PortConfig pc;
    auto h = OpenPort(pc, [](void*, uint32_t){});
    SpatialPos pos{1.5f, 0.f, -2.0f};
    CHECK(SetPortSpatialPosition(h, pos));
    ClosePort(h);
    Shutdown();
}

TEST_CASE("Phase6::Audio::Spatial::InvalidPort", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    SpatialPos pos{};
    CHECK_FALSE(SetPortSpatialPosition(INVALID_PORT, pos));
    Shutdown();
}

// ── Latency ───────────────────────────────────────────────────────────────

TEST_CASE("Phase6::Audio::Latency::GetDoesNotCrash", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    double lat = GetLatencyMs();
    CHECK(lat >= 0.0);
    Shutdown();
}

// ── Device enumeration ─────────────────────────────────────────────────────

TEST_CASE("Phase6::Audio::Device::EnumerateReturnsAtLeastOne", "[audio][phase6]")
{
    auto devs = EnumerateDevices();
    CHECK(!devs.empty());
}

TEST_CASE("Phase6::Audio::Device::SelectDevice", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    CHECK(SelectDevice("default"));
    CHECK(GetCurrentDevice() == "default");
    Shutdown();
}

TEST_CASE("Phase6::Audio::Device::GetCurrentDefault", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    std::string dev = GetCurrentDevice();
    CHECK(!dev.empty());
    Shutdown();
}

// ── Underrun callback ──────────────────────────────────────────────────────

TEST_CASE("Phase6::Audio::Underrun::SetCallbackDoesNotCrash", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    bool called = false;
    SetUnderrunCallback([&](PortHandle, uint64_t){ called = true; });
    // Callback fires only on real underrun; just verify no crash.
    SUCCEED("SetUnderrunCallback set without crash");
    Shutdown();
}

TEST_CASE("Phase6::Audio::Underrun::ClearCallback", "[audio][phase6]")
{
    AudioConfig cfg;
    Init(cfg);
    SetUnderrunCallback(nullptr);  // clear
    SUCCEED("Cleared underrun callback without crash");
    Shutdown();
}
