// ChuckStation5 – Input module
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ChuckStation5::Input {

// ── Button bitmask (mirrors DualSense layout) ─────────────────────────────

enum ButtonMask : uint32_t
{
    BTN_CROSS       = 1 <<  0,
    BTN_CIRCLE      = 1 <<  1,
    BTN_SQUARE      = 1 <<  2,
    BTN_TRIANGLE    = 1 <<  3,
    BTN_L1          = 1 <<  4,
    BTN_R1          = 1 <<  5,
    BTN_L2          = 1 <<  6,
    BTN_R2          = 1 <<  7,
    BTN_L3          = 1 <<  8,
    BTN_R3          = 1 <<  9,
    BTN_OPTIONS     = 1 << 10,
    BTN_CREATE      = 1 << 11,
    BTN_TOUCHPAD    = 1 << 12,
    BTN_DPAD_UP     = 1 << 13,
    BTN_DPAD_DOWN   = 1 << 14,
    BTN_DPAD_LEFT   = 1 << 15,
    BTN_DPAD_RIGHT  = 1 << 16,
    BTN_PS          = 1 << 17,
};

/// Normalised analog state.  All axes in [-1, 1], triggers in [0, 1].
struct PadState
{
    uint32_t buttons    = 0;

    float    leftStickX = 0.f;
    float    leftStickY = 0.f;
    float    rightStickX= 0.f;
    float    rightStickY= 0.f;

    float    leftTrigger = 0.f;
    float    rightTrigger= 0.f;

    // Touchpad (DualSense / DS4)
    bool     touch0Active = false;
    float    touch0X      = 0.f;
    float    touch0Y      = 0.f;

    // Gyro / accelerometer (raw, device units)
    float    gyroX = 0.f;
    float    gyroY = 0.f;
    float    gyroZ = 0.f;

    uint64_t timestamp = 0;  ///< microseconds
};

/// Rumble request.
struct RumbleCmd
{
    float largeMotor  = 0.f;   ///< [0, 1]
    float smallMotor  = 0.f;
};

// ── Callbacks ─────────────────────────────────────────────────────────────

using PadEventFn = std::function<void(int padIndex, const PadState&)>;

// ── Lifecycle ─────────────────────────────────────────────────────────────

void Init();
void Shutdown();
void Poll();   ///< Call once per frame; fires registered callbacks.

// ── Controller management ─────────────────────────────────────────────────

int  ConnectedPads();
bool GetPadState(int index, PadState& out);
void SetRumble(int index, const RumbleCmd& cmd);

// ── Callbacks ─────────────────────────────────────────────────────────────

void RegisterPadCallback(PadEventFn fn);





// ── Motion sensor abstraction ─────────────────────────────────────────────
struct MotionState
{
    float accelX = 0.f, accelY = 0.f, accelZ = 0.f;  ///< m/s²
    float gyroX  = 0.f, gyroY  = 0.f, gyroZ  = 0.f;  ///< rad/s
    uint64_t timestampUs = 0;
};

bool GetMotionState(int padIndex, MotionState& out);
void SetMotionEnabled(int padIndex, bool enable);
bool IsMotionEnabled(int padIndex);

// ── Touchpad abstraction ──────────────────────────────────────────────────
struct TouchPoint
{
    bool     active = false;
    uint8_t  id     = 0;
    float    x      = 0.f;   ///< [0, 1] normalised
    float    y      = 0.f;
};

struct TouchpadState
{
    TouchPoint points[2];    ///< DualSense supports 2 simultaneous touches
    bool       pressed = false;
};

bool GetTouchpadState(int padIndex, TouchpadState& out);

// ── Controller profiles ───────────────────────────────────────────────────
struct ControllerProfile
{
    std::string name;
    float       deadzoneLStick  = 0.08f;
    float       deadzoneRStick  = 0.08f;
    float       triggerThreshold= 0.05f;
    bool        invertLY        = false;
    bool        invertRY        = false;
    // Remap: for each ButtonMask bit, which physical bit it maps to
    uint32_t    buttonRemap     = 0;  ///< 0 = identity
};

bool  SetControllerProfile(int padIndex, const ControllerProfile& profile);
bool  GetControllerProfile(int padIndex, ControllerProfile& out);
void  ResetControllerProfile(int padIndex);

// ── Input recording / playback ────────────────────────────────────────────
struct InputFrame
{
    uint64_t  timestampUs = 0;
    int       padIndex    = 0;
    PadState  state;
};

void StartRecording();
void StopRecording();
bool IsRecording();
std::vector<InputFrame> GetRecording();

bool StartPlayback(const std::vector<InputFrame>& frames);
void StopPlayback();
bool IsPlayingBack();

// ── Latency measurement ───────────────────────────────────────────────────
struct InputLatencyStats
{
    double avgPollUs  = 0.0;
    double minPollUs  = 0.0;
    double maxPollUs  = 0.0;
    uint64_t pollCount = 0;
};

InputLatencyStats GetLatencyStats();
void              ResetLatencyStats();

// ── Hot-plug ──────────────────────────────────────────────────────────────
using HotplugFn = std::function<void(int padIndex, bool connected)>;
void SetHotplugCallback(HotplugFn fn);

} // namespace ChuckStation5::Input
