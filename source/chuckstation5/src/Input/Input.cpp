// ChuckStation5 – Input implementation (Phase 2 – SDL2 backend)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Supports: DualSense, DualShock 4, Xbox controllers, keyboard, mouse.
// Uses SDL2 GameController + Haptic APIs.
// DualSense / DS4 detected by VID:PID; Xbox by SDL_IsGameController.
#include "ChuckStation5/Input/Input.h"
#include "ChuckStation5/Config/Config.h"
using ChuckStation5::Config::InputMode;
#include "ChuckStation5/Logger/Logger.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

// SDL2 – optional; graceful degradation if unavailable at link time
#if defined(CHUCKSTATION5_HAVE_SDL2)
#  include <SDL2/SDL.h>
#endif

namespace ChuckStation5::Input {

// ── Controller identification ─────────────────────────────────────────────
static constexpr uint16_t VID_SONY      = 0x054C;
static constexpr uint16_t PID_DS4_V1    = 0x05C4;
static constexpr uint16_t PID_DS4_V2    = 0x09CC;
static constexpr uint16_t PID_DUALSENSE = 0x0CE6;

static constexpr uint16_t VID_MICROSOFT = 0x045E;

// ── Keyboard virtual pad ──────────────────────────────────────────────────
namespace KbMap {
    // Default keyboard → PadState mapping
#if defined(CHUCKSTATION5_HAVE_SDL2)
    static constexpr SDL_Scancode Cross    = SDL_SCANCODE_SPACE;
    static constexpr SDL_Scancode Circle   = SDL_SCANCODE_E;
    static constexpr SDL_Scancode Square   = SDL_SCANCODE_Q;
    static constexpr SDL_Scancode Triangle = SDL_SCANCODE_R;
    static constexpr SDL_Scancode L1       = SDL_SCANCODE_LSHIFT;
    static constexpr SDL_Scancode R1       = SDL_SCANCODE_RSHIFT;
    static constexpr SDL_Scancode Options  = SDL_SCANCODE_ESCAPE;
    static constexpr SDL_Scancode DUp      = SDL_SCANCODE_UP;
    static constexpr SDL_Scancode DDown    = SDL_SCANCODE_DOWN;
    static constexpr SDL_Scancode DLeft    = SDL_SCANCODE_LEFT;
    static constexpr SDL_Scancode DRight   = SDL_SCANCODE_RIGHT;
#endif
}

// ── Internal slot ─────────────────────────────────────────────────────────
struct ControllerSlot {
    PadState        state{};
    InputMode       mode    = InputMode::Keyboard;
    bool            active  = false;
    float           deadzone = 0.10f;
    float           triggerDeadzone = 0.05f;
#if defined(CHUCKSTATION5_HAVE_SDL2)
    SDL_GameController* gc     = nullptr;
    SDL_Haptic*         haptic = nullptr;
    SDL_JoystickID      jid    = -1;
#endif
};

// ── State ─────────────────────────────────────────────────────────────────
namespace {

struct InputState {
    std::array<ControllerSlot, 4>  slots;
    std::vector<PadEventFn>        callbacks;
    std::mutex                     mtx;
    bool                           sdlOwned = false; // did we SDL_Init?
    bool                           initialised = false;

    static InputState& Get() { static InputState s; return s; }
};

[[maybe_unused]] static float ApplyDeadzone(float v, float dz) {
    if (std::fabs(v) < dz) return 0.f;
    float sign = v > 0.f ? 1.f : -1.f;
    return sign * (std::fabs(v) - dz) / (1.f - dz);
}

[[maybe_unused]] static float NormAxis(int16_t raw) {
    // SDL axis: -32768..32767
    return static_cast<float>(raw) / 32767.f;
}

[[maybe_unused]] static float NormTrigger(int16_t raw) {
    // SDL trigger: 0..32767
    return static_cast<float>(raw) / 32767.f;
}

#if defined(CHUCKSTATION5_HAVE_SDL2)
InputMode DetectMode(SDL_GameController* gc) {
    if (!gc) return InputMode::Keyboard;
    auto* joy = SDL_GameControllerGetJoystick(gc);
    uint16_t vid = SDL_JoystickGetVendor(joy);
    uint16_t pid = SDL_JoystickGetProduct(joy);
    if (vid == VID_SONY) {
        if (pid == PID_DUALSENSE) return InputMode::DualSense;
        if (pid == PID_DS4_V1 || pid == PID_DS4_V2) return InputMode::DS4;
    }
    if (vid == VID_MICROSOFT) return InputMode::Xbox;
    return InputMode::Xbox; // generic gamepad → Xbox layout
}

void UpdateSlotFromGamepad(ControllerSlot& slot) {
    auto* gc  = slot.gc;
    if (!gc) return;
    PadState& ps = slot.state;
    ps.buttons = 0;

    auto btn = [&](SDL_GameControllerButton b) -> bool {
        return SDL_GameControllerGetButton(gc, b) != 0;
    };

    if (btn(SDL_CONTROLLER_BUTTON_A))             ps.buttons |= BTN_CROSS;
    if (btn(SDL_CONTROLLER_BUTTON_B))             ps.buttons |= BTN_CIRCLE;
    if (btn(SDL_CONTROLLER_BUTTON_X))             ps.buttons |= BTN_SQUARE;
    if (btn(SDL_CONTROLLER_BUTTON_Y))             ps.buttons |= BTN_TRIANGLE;
    if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  ps.buttons |= BTN_L1;
    if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) ps.buttons |= BTN_R1;
    if (btn(SDL_CONTROLLER_BUTTON_LEFTSTICK))     ps.buttons |= BTN_L3;
    if (btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK))    ps.buttons |= BTN_R3;
    if (btn(SDL_CONTROLLER_BUTTON_START))         ps.buttons |= BTN_OPTIONS;
    if (btn(SDL_CONTROLLER_BUTTON_BACK))          ps.buttons |= BTN_CREATE;
    if (btn(SDL_CONTROLLER_BUTTON_GUIDE))         ps.buttons |= BTN_PS;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP))       ps.buttons |= BTN_DPAD_UP;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN))     ps.buttons |= BTN_DPAD_DOWN;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT))     ps.buttons |= BTN_DPAD_LEFT;
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT))    ps.buttons |= BTN_DPAD_RIGHT;

    auto ax = [&](SDL_GameControllerAxis a) -> int16_t {
        return SDL_GameControllerGetAxis(gc, a);
    };

    float dz = slot.deadzone;
    ps.leftStickX  = ApplyDeadzone(NormAxis(ax(SDL_CONTROLLER_AXIS_LEFTX)),  dz);
    ps.leftStickY  = ApplyDeadzone(NormAxis(ax(SDL_CONTROLLER_AXIS_LEFTY)),  dz);
    ps.rightStickX = ApplyDeadzone(NormAxis(ax(SDL_CONTROLLER_AXIS_RIGHTX)), dz);
    ps.rightStickY = ApplyDeadzone(NormAxis(ax(SDL_CONTROLLER_AXIS_RIGHTY)), dz);

    float lt = NormTrigger(ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    float rt = NormTrigger(ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
    ps.leftTrigger  = lt > slot.triggerDeadzone ? lt : 0.f;
    ps.rightTrigger = rt > slot.triggerDeadzone ? rt : 0.f;

    if (ps.leftTrigger  > 0.5f) ps.buttons |= BTN_L2;
    if (ps.rightTrigger > 0.5f) ps.buttons |= BTN_R2;

    ps.timestamp = static_cast<uint64_t>(SDL_GetTicks64()) * 1000u;
}

void UpdateKeyboardSlot(ControllerSlot& slot) {
    const uint8_t* kb = SDL_GetKeyboardState(nullptr);
    if (!kb) return;
    PadState& ps = slot.state;
    ps.buttons = 0;

    if (kb[KbMap::Cross])    ps.buttons |= BTN_CROSS;
    if (kb[KbMap::Circle])   ps.buttons |= BTN_CIRCLE;
    if (kb[KbMap::Square])   ps.buttons |= BTN_SQUARE;
    if (kb[KbMap::Triangle]) ps.buttons |= BTN_TRIANGLE;
    if (kb[KbMap::L1])       ps.buttons |= BTN_L1;
    if (kb[KbMap::R1])       ps.buttons |= BTN_R1;
    if (kb[KbMap::Options])  ps.buttons |= BTN_OPTIONS;
    if (kb[KbMap::DUp])      ps.buttons |= BTN_DPAD_UP;
    if (kb[KbMap::DDown])    ps.buttons |= BTN_DPAD_DOWN;
    if (kb[KbMap::DLeft])    ps.buttons |= BTN_DPAD_LEFT;
    if (kb[KbMap::DRight])   ps.buttons |= BTN_DPAD_RIGHT;

    // WASD → left stick
    float lx = 0.f, ly = 0.f;
    if (kb[SDL_SCANCODE_A]) lx -= 1.f;
    if (kb[SDL_SCANCODE_D]) lx += 1.f;
    if (kb[SDL_SCANCODE_W]) ly -= 1.f;
    if (kb[SDL_SCANCODE_S]) ly += 1.f;
    ps.leftStickX = lx; ps.leftStickY = ly;

    ps.timestamp = static_cast<uint64_t>(SDL_GetTicks64()) * 1000u;
}

int FindFreeSlot(InputState& st) {
    for (int i = 0; i < 4; ++i)
        if (!st.slots[i].active) return i;
    return -1;
}

void OpenController(InputState& st, int sdlIdx) {
    if (!SDL_IsGameController(sdlIdx)) return;
    int slot = FindFreeSlot(st);
    if (slot < 0) { CHUCKSTATION5_WARN("[Input] No free pad slot."); return; }

    auto* gc = SDL_GameControllerOpen(sdlIdx);
    if (!gc) {
        CHUCKSTATION5_WARN("[Input] SDL_GameControllerOpen failed: %s", SDL_GetError());
        return;
    }

    st.slots[slot].gc     = gc;
    st.slots[slot].active = true;
    st.slots[slot].mode   = DetectMode(gc);
    st.slots[slot].jid    = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));

    // Try haptic
    SDL_Haptic* hap = SDL_HapticOpenFromJoystick(SDL_GameControllerGetJoystick(gc));
    if (hap && SDL_HapticRumbleInit(hap) == 0)
        st.slots[slot].haptic = hap;

    CHUCKSTATION5_INFO("[Input] Pad %d connected: %s (mode=%u)",
              slot, SDL_GameControllerName(gc),
              static_cast<uint32_t>(st.slots[slot].mode));
}

void CloseController(InputState& st, SDL_JoystickID jid) {
    for (int i = 0; i < 4; ++i) {
        auto& sl = st.slots[i];
        if (!sl.active || sl.jid != jid) continue;
        if (sl.haptic) { SDL_HapticClose(sl.haptic); sl.haptic = nullptr; }
        SDL_GameControllerClose(sl.gc);
        sl.gc = nullptr; sl.active = false; sl.state = PadState{};
        CHUCKSTATION5_INFO("[Input] Pad %d disconnected.", i);
        return;
    }
}
#endif // CHUCKSTATION5_HAVE_SDL2

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────

void Init() {
    auto& st = InputState::Get();
    std::lock_guard lk(st.mtx);
    if (st.initialised) return;

    // Slot 0 = keyboard/mouse virtual pad (always present)
    st.slots[0].active = true;
    st.slots[0].mode   = InputMode::Keyboard;

#if defined(CHUCKSTATION5_HAVE_SDL2)
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) == 0)
            st.sdlOwned = true;
        else
            CHUCKSTATION5_WARN("[Input] SDL GameController init failed: %s", SDL_GetError());
    }

    // Open any controllers already connected
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        OpenController(st, i);
#else
    CHUCKSTATION5_WARN("[Input] SDL2 not available – keyboard virtual pad only.");
#endif

    st.initialised = true;
    int pads = 0;
    for (const auto& sl : st.slots) if (sl.active) ++pads;
    CHUCKSTATION5_INFO("[Input] Initialised. %d controller(s) connected.", pads - 1);
}

void Shutdown() {
    auto& st = InputState::Get();
    std::lock_guard lk(st.mtx);
#if defined(CHUCKSTATION5_HAVE_SDL2)
    for (auto& sl : st.slots) {
        if (!sl.active) continue;
        if (sl.haptic) { SDL_HapticClose(sl.haptic); sl.haptic = nullptr; }
        if (sl.gc)     { SDL_GameControllerClose(sl.gc); sl.gc = nullptr; }
        sl.active = false;
    }
    if (st.sdlOwned) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
#endif
    st.callbacks.clear();
    st.initialised = false;
    CHUCKSTATION5_INFO("[Input] Shutdown.");
}

void Poll() {
    auto& st = InputState::Get();
    std::lock_guard lk(st.mtx);

#if defined(CHUCKSTATION5_HAVE_SDL2)
    // Process SDL hotplug events (non-blocking)
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_CONTROLLERDEVICEADDED)
            OpenController(st, ev.cdevice.which);
        else if (ev.type == SDL_CONTROLLERDEVICEREMOVED)
            CloseController(st, ev.cdevice.which);
        // other events handled by UI layer
    }

    for (int i = 0; i < 4; ++i) {
        auto& sl = st.slots[i];
        if (!sl.active) continue;
        if (sl.mode == InputMode::Keyboard)
            UpdateKeyboardSlot(sl);
        else
            UpdateSlotFromGamepad(sl);
    }
#else
    // No SDL: just leave keyboard slot zeroed (no events available)
#endif

    for (auto& cb : st.callbacks)
        for (int i = 0; i < 4; ++i)
            if (st.slots[i].active) cb(i, st.slots[i].state);
}

int ConnectedPads() {
    auto& st = InputState::Get();
    std::lock_guard lk(st.mtx);
    int n = 0;
    for (const auto& sl : st.slots) if (sl.active) ++n;
    return n;
}

bool GetPadState(int index, PadState& out) {
    if (index < 0 || index >= 4) return false;
    auto& st = InputState::Get();
    std::lock_guard lk(st.mtx);
    if (!st.slots[index].active) return false;
    out = st.slots[index].state;
    return true;
}

void SetRumble(int index, const RumbleCmd& cmd) {
    if (index < 0 || index >= 4) return;
    auto& st = InputState::Get();
    std::lock_guard lk(st.mtx);
#if defined(CHUCKSTATION5_HAVE_SDL2)
    auto& sl = st.slots[index];
    if (sl.haptic && (cmd.largeMotor > 0.f || cmd.smallMotor > 0.f)) {
        float strength = std::max(cmd.largeMotor, cmd.smallMotor);
        SDL_HapticRumblePlay(sl.haptic, strength, 200 /*ms*/);
    }
#else
    (void)cmd;
#endif
}

void RegisterPadCallback(PadEventFn fn) {
    auto& st = InputState::Get();
    std::lock_guard lk(st.mtx);
    st.callbacks.push_back(std::move(fn));
}






namespace {

using Clock = std::chrono::steady_clock;

struct InputExtState
{
    // Motion
    std::array<MotionState, 4>        motion{};
    std::array<bool, 4>               motionEnabled{false, false, false, false};

    // Touchpad
    std::array<TouchpadState, 4>      touchpad{};

    // Profiles
    std::array<ControllerProfile, 4>  profiles;

    // Recording
    std::atomic<bool>                 recording{false};
    std::mutex                        recMtx;
    std::vector<InputFrame>           recording_frames;
    Clock::time_point                 recStart;

    // Playback
    std::atomic<bool>                 playback{false};
    std::vector<InputFrame>           playback_frames;
    size_t                            playbackIdx = 0;
    Clock::time_point                 playbackStart;

    // Latency stats
    std::mutex          latMtx;
    double              latSumUs  = 0.0;
    double              latMinUs  = 1e18;
    double              latMaxUs  = 0.0;
    uint64_t            latCount  = 0;
    Clock::time_point   lastPoll;
    bool                latFirst  = true;

    // Hotplug
    HotplugFn           hotplugCb;
    std::mutex          hotplugMtx;

    static InputExtState& Get() { static InputExtState s; return s; }
};

} // namespace (Phase 6)

// ── Motion ────────────────────────────────────────────────────────────────

bool GetMotionState(int idx, MotionState& out)
{
    if (idx < 0 || idx >= 4) return false;
    auto& s = InputExtState::Get();
    out = s.motion[idx];
    return true;
}

void SetMotionEnabled(int idx, bool enable)
{
    if (idx < 0 || idx >= 4) return;
    InputExtState::Get().motionEnabled[idx] = enable;
    CHUCKSTATION5_DEBUG("[Input] Motion sensor pad%d %s", idx, enable ? "on" : "off");
}

bool IsMotionEnabled(int idx)
{
    if (idx < 0 || idx >= 4) return false;
    return InputExtState::Get().motionEnabled[idx];
}

// ── Touchpad ──────────────────────────────────────────────────────────────

bool GetTouchpadState(int idx, TouchpadState& out)
{
    if (idx < 0 || idx >= 4) return false;
    out = InputExtState::Get().touchpad[idx];
    return true;
}

// ── Controller profiles ────────────────────────────────────────────────────

bool SetControllerProfile(int idx, const ControllerProfile& profile)
{
    if (idx < 0 || idx >= 4) return false;
    InputExtState::Get().profiles[idx] = profile;
    CHUCKSTATION5_INFO("[Input] Profile '%s' applied to pad%d",
              profile.name.c_str(), idx);
    return true;
}

bool GetControllerProfile(int idx, ControllerProfile& out)
{
    if (idx < 0 || idx >= 4) return false;
    out = InputExtState::Get().profiles[idx];
    return true;
}

void ResetControllerProfile(int idx)
{
    if (idx < 0 || idx >= 4) return;
    InputExtState::Get().profiles[idx] = ControllerProfile{};
    CHUCKSTATION5_DEBUG("[Input] Profile reset for pad%d", idx);
}

// ── Recording ─────────────────────────────────────────────────────────────

void StartRecording()
{
    auto& s = InputExtState::Get();
    std::lock_guard lk(s.recMtx);
    s.recording_frames.clear();
    s.recStart = Clock::now();
    s.recording.store(true);
    CHUCKSTATION5_INFO("[Input] Recording started");
}

void StopRecording()
{
    auto& s = InputExtState::Get();
    s.recording.store(false);
    CHUCKSTATION5_INFO("[Input] Recording stopped (%zu frames)",
              s.recording_frames.size());
}

bool IsRecording() { return InputExtState::Get().recording.load(); }

std::vector<InputFrame> GetRecording()
{
    auto& s = InputExtState::Get();
    std::lock_guard lk(s.recMtx);
    return s.recording_frames;
}

bool StartPlayback(const std::vector<InputFrame>& frames)
{
    if (frames.empty()) return false;
    auto& s = InputExtState::Get();
    {
        std::lock_guard lk(s.recMtx);
        s.playback_frames = frames;
        s.playbackIdx     = 0;
        s.playbackStart   = Clock::now();
    }
    s.playback.store(true);
    CHUCKSTATION5_INFO("[Input] Playback started (%zu frames)", frames.size());
    return true;
}

void StopPlayback()
{
    InputExtState::Get().playback.store(false);
    CHUCKSTATION5_INFO("[Input] Playback stopped");
}

bool IsPlayingBack() { return InputExtState::Get().playback.load(); }

// ── Latency stats ──────────────────────────────────────────────────────────

InputLatencyStats GetLatencyStats()
{
    auto& s = InputExtState::Get();
    std::lock_guard lk(s.latMtx);
    InputLatencyStats out;
    out.pollCount = s.latCount;
    if (s.latCount == 0) return out;
    out.avgPollUs = s.latSumUs / static_cast<double>(s.latCount);
    out.minPollUs = s.latMinUs;
    out.maxPollUs = s.latMaxUs;
    return out;
}

void ResetLatencyStats()
{
    auto& s = InputExtState::Get();
    std::lock_guard lk(s.latMtx);
    s.latSumUs  = 0.0;
    s.latMinUs  = 1e18;
    s.latMaxUs  = 0.0;
    s.latCount  = 0;
    s.latFirst  = true;
}

// ── Hot-plug ──────────────────────────────────────────────────────────────

void SetHotplugCallback(HotplugFn fn)
{
    auto& s = InputExtState::Get();
    std::lock_guard lk(s.hotplugMtx);
    s.hotplugCb = std::move(fn);
}

} // namespace ChuckStation5::Input
