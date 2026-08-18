// ChuckStation5 – Audio module
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
// Wraps Kyty's miniaudio-based audio backend with a typed C++20 API.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace ChuckStation5::Audio {

// ── Constants ─────────────────────────────────────────────────────────────
static constexpr uint32_t PS5_SAMPLE_RATE    = 48000;
static constexpr uint16_t PS5_CHANNEL_COUNT  = 8;    // 7.1 surround
static constexpr uint16_t PS5_BITS_PER_SAMPLE= 32;   // float

// ── Format ────────────────────────────────────────────────────────────────
enum class SampleFormat : uint8_t
{
    Float32 = 0,
    Int16   = 1,
    Int32   = 2,
};

struct AudioConfig
{
    uint32_t     sampleRate    = PS5_SAMPLE_RATE;
    uint16_t     channels      = 2;               ///< output channels (stereo for now)
    uint16_t     bufferSamples = 512;
    SampleFormat format        = SampleFormat::Float32;
    float        masterVolume  = 1.0f;
};

// ── Audio port (mirrors PS5 sceAudioOut port concept) ─────────────────────
struct PortConfig
{
    uint32_t     sampleRate = PS5_SAMPLE_RATE;
    uint16_t     channels   = 2;
    uint16_t     bufferSamples = 256;
    SampleFormat format     = SampleFormat::Int16;
};

using PortHandle = int32_t;
static constexpr PortHandle INVALID_PORT = -1;

// ── Data callback ─────────────────────────────────────────────────────────
/// Called by the audio thread when it needs more data.
/// @param buf      Target buffer (interleaved samples, format per PortConfig)
/// @param frames   Number of audio frames requested
using AudioFillFn = std::function<void(void* buf, uint32_t frames)>;

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(const AudioConfig& cfg);
void Shutdown();

// ── Port management (sceAudioOut style) ──────────────────────────────────
PortHandle OpenPort(const PortConfig& cfg, AudioFillFn fillCallback);
bool       ClosePort(PortHandle handle);
bool       SetPortVolume(PortHandle handle, float volume);

// ── Playback control ──────────────────────────────────────────────────────
bool Start(PortHandle handle);
bool Stop(PortHandle handle);
bool IsRunning(PortHandle handle);

// ── Global controls ───────────────────────────────────────────────────────
void SetMasterVolume(float volume);
float GetMasterVolume();





// ── Audio statistics ──────────────────────────────────────────────────────
struct AudioStats
{
    uint64_t  framesRendered  = 0;   ///< total frames mixed across all ports
    uint64_t  underruns       = 0;   ///< ring-buffer underflows
    uint64_t  overruns        = 0;   ///< ring-buffer overflows
    double    latencyMs       = 0.0; ///< current backend latency
    uint32_t  activePorts     = 0;
    double    peakAmplitude   = 0.0; ///< peak absolute sample value in last mix
};

AudioStats GetStats();
void       ResetStats();

// ── Channel mixing control ─────────────────────────────────────────────────
/// Per-port channel volume map (up to 8 channels).
/// Values are in [0, 1]; channels beyond port count are ignored.
bool SetChannelVolumes(PortHandle h, const float volumes[8]);
bool GetChannelVolumes(PortHandle h, float volumes[8]);

// ── Spatial audio (position hint for future attenuation) ─────────────────
struct SpatialPos { float x = 0.f, y = 0.f, z = 0.f; };
bool SetPortSpatialPosition(PortHandle h, const SpatialPos& pos);

// ── Latency reporting ─────────────────────────────────────────────────────
double GetLatencyMs();

// ── Device switching ──────────────────────────────────────────────────────
struct AudioDeviceInfo
{
    std::string name;
    uint32_t    maxChannels = 2;
    uint32_t    sampleRate  = 48000;
};
std::vector<AudioDeviceInfo> EnumerateDevices();
bool                         SelectDevice(const std::string& name);
std::string                  GetCurrentDevice();

// ── Underrun / overrun diagnostics ────────────────────────────────────────
/// Register a callback invoked on each underrun event.
using UnderrunFn = std::function<void(PortHandle, uint64_t totalUnderruns)>;
void SetUnderrunCallback(UnderrunFn fn);

} // namespace ChuckStation5::Audio
