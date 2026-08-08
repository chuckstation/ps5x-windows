// PS5x – Phase 6 Input tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PS5x/Input/Input.h"
#include <thread>
#include <chrono>

using namespace PS5x::Input;

// ── Motion sensor ──────────────────────────────────────────────────────────

TEST_CASE("Phase6::Input::Motion::EnableDisable", "[input][phase6]")
{
    Init();
    SetMotionEnabled(0, true);
    CHECK(IsMotionEnabled(0));
    SetMotionEnabled(0, false);
    CHECK_FALSE(IsMotionEnabled(0));
    Shutdown();
}

TEST_CASE("Phase6::Input::Motion::InvalidIndex", "[input][phase6]")
{
    Init();
    SetMotionEnabled(-1, true);
    SetMotionEnabled(99, true);
    CHECK_FALSE(IsMotionEnabled(-1));
    CHECK_FALSE(IsMotionEnabled(99));
    Shutdown();
}

TEST_CASE("Phase6::Input::Motion::GetState", "[input][phase6]")
{
    Init();
    MotionState ms;
    bool ok = GetMotionState(0, ms);
    CHECK(ok);
    // Default state - all zero
    CHECK(ms.accelX == Catch::Approx(0.f));
    CHECK(ms.gyroX  == Catch::Approx(0.f));
    Shutdown();
}

TEST_CASE("Phase6::Input::Motion::GetStateInvalidIdx", "[input][phase6]")
{
    Init();
    MotionState ms;
    CHECK_FALSE(GetMotionState(-1, ms));
    CHECK_FALSE(GetMotionState(4, ms));
    Shutdown();
}

// ── Touchpad ──────────────────────────────────────────────────────────────

TEST_CASE("Phase6::Input::Touchpad::GetDefault", "[input][phase6]")
{
    Init();
    TouchpadState ts;
    CHECK(GetTouchpadState(0, ts));
    CHECK_FALSE(ts.points[0].active);
    CHECK_FALSE(ts.points[1].active);
    CHECK_FALSE(ts.pressed);
    Shutdown();
}

TEST_CASE("Phase6::Input::Touchpad::InvalidIndex", "[input][phase6]")
{
    Init();
    TouchpadState ts;
    CHECK_FALSE(GetTouchpadState(-1, ts));
    CHECK_FALSE(GetTouchpadState(4, ts));
    Shutdown();
}

// ── Controller profiles ────────────────────────────────────────────────────

TEST_CASE("Phase6::Input::Profile::SetAndGet", "[input][phase6]")
{
    Init();
    ControllerProfile p;
    p.name           = "racing";
    p.deadzoneLStick  = 0.15f;
    p.deadzoneRStick  = 0.12f;
    p.invertLY        = true;
    CHECK(SetControllerProfile(0, p));

    ControllerProfile got;
    CHECK(GetControllerProfile(0, got));
    CHECK(got.name           == "racing");
    CHECK(got.deadzoneLStick  == Catch::Approx(0.15f));
    CHECK(got.invertLY        == true);
    Shutdown();
}

TEST_CASE("Phase6::Input::Profile::Reset", "[input][phase6]")
{
    Init();
    ControllerProfile p;
    p.name = "custom";
    p.deadzoneLStick = 0.3f;
    SetControllerProfile(0, p);
    ResetControllerProfile(0);
    ControllerProfile got;
    GetControllerProfile(0, got);
    CHECK(got.name.empty());
    CHECK(got.deadzoneLStick == Catch::Approx(0.08f));
    Shutdown();
}

TEST_CASE("Phase6::Input::Profile::InvalidIndex", "[input][phase6]")
{
    Init();
    ControllerProfile p;
    CHECK_FALSE(SetControllerProfile(-1, p));
    CHECK_FALSE(SetControllerProfile(4,  p));
    Shutdown();
}

TEST_CASE("Phase6::Input::Profile::MultiPad", "[input][phase6]")
{
    Init();
    ControllerProfile p0, p1;
    p0.name = "pad0"; p1.name = "pad1";
    SetControllerProfile(0, p0);
    SetControllerProfile(1, p1);
    ControllerProfile g0, g1;
    GetControllerProfile(0, g0);
    GetControllerProfile(1, g1);
    CHECK(g0.name == "pad0");
    CHECK(g1.name == "pad1");
    Shutdown();
}

// ── Recording / playback ───────────────────────────────────────────────────

TEST_CASE("Phase6::Input::Recording::StartStop", "[input][phase6]")
{
    Init();
    CHECK_FALSE(IsRecording());
    StartRecording();
    CHECK(IsRecording());
    StopRecording();
    CHECK_FALSE(IsRecording());
    Shutdown();
}

TEST_CASE("Phase6::Input::Recording::GetEmptyBeforeRecord", "[input][phase6]")
{
    Init();
    auto frames = GetRecording();
    CHECK(frames.empty());
    Shutdown();
}

TEST_CASE("Phase6::Input::Recording::PlaybackEmpty", "[input][phase6]")
{
    Init();
    CHECK_FALSE(StartPlayback({}));
    CHECK_FALSE(IsPlayingBack());
    Shutdown();
}

TEST_CASE("Phase6::Input::Recording::PlaybackWithFrames", "[input][phase6]")
{
    Init();
    std::vector<InputFrame> frames;
    InputFrame f;
    f.timestampUs = 1000;
    f.padIndex    = 0;
    f.state.buttons = BTN_CROSS;
    frames.push_back(f);
    CHECK(StartPlayback(frames));
    CHECK(IsPlayingBack());
    StopPlayback();
    CHECK_FALSE(IsPlayingBack());
    Shutdown();
}

TEST_CASE("Phase6::Input::Recording::StopWhileNotPlaying", "[input][phase6]")
{
    Init();
    REQUIRE_NOTHROW(StopPlayback());
    Shutdown();
}

// ── Latency stats ──────────────────────────────────────────────────────────

TEST_CASE("Phase6::Input::Latency::DefaultStats", "[input][phase6]")
{
    Init();
    ResetLatencyStats();
    auto s = GetLatencyStats();
    CHECK(s.pollCount == 0);
    CHECK(s.avgPollUs == Catch::Approx(0.0));
    Shutdown();
}

TEST_CASE("Phase6::Input::Latency::ResetWorks", "[input][phase6]")
{
    Init();
    ResetLatencyStats();
    auto s = GetLatencyStats();
    CHECK(s.pollCount == 0);
    Shutdown();
}

// ── Hotplug ───────────────────────────────────────────────────────────────

TEST_CASE("Phase6::Input::Hotplug::SetCallback", "[input][phase6]")
{
    Init();
    bool fired = false;
    SetHotplugCallback([&](int, bool){ fired = true; });
    SUCCEED("Hotplug callback registered without crash");
    SetHotplugCallback(nullptr); // clear
    Shutdown();
}
