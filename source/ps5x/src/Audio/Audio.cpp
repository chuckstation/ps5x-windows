// PS5x – Audio implementation (Phase 2 – ring-buffer + sine validation)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
//
// Implements port-based audio (sceAudioOut style) with:
//   • Ring-buffer per port
//   • SDL2 audio device backend
//   • Sine-wave self-test for validation without a game
#include "PS5x/Audio/Audio.h"
#include "PS5x/Logger/Logger.h"

#include <algorithm>
#include <array>
#include <memory>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numbers>
#include <unordered_map>
#include <vector>

#if defined(PS5X_HAVE_SDL2)
#  include <SDL2/SDL.h>
#endif

namespace PS5x::Audio {

// ── Ring buffer ────────────────────────────────────────────────────────────
struct RingBuffer {
    std::vector<uint8_t> data;
    size_t               readPos  = 0;
    size_t               writePos = 0;
    size_t               used     = 0;

    explicit RingBuffer(size_t capacity) : data(capacity) {}

    size_t Capacity() const { return data.size(); }
    size_t Used()     const { return used; }
    size_t Free()     const { return data.size() - used; }

    bool Write(const void* src, size_t bytes) {
        if (bytes > Free()) return false;
        const auto* s = static_cast<const uint8_t*>(src);
        size_t cap = data.size();
        size_t first = std::min(bytes, cap - writePos);
        std::memcpy(data.data() + writePos, s, first);
        if (bytes > first)
            std::memcpy(data.data(), s + first, bytes - first);
        writePos = (writePos + bytes) % cap;
        used += bytes;
        return true;
    }

    size_t Read(void* dst, size_t bytes) {
        size_t toRead = std::min(bytes, used);
        auto* d = static_cast<uint8_t*>(dst);
        size_t cap = data.size();
        size_t first = std::min(toRead, cap - readPos);
        std::memcpy(d, data.data() + readPos, first);
        if (toRead > first)
            std::memcpy(d + first, data.data(), toRead - first);
        readPos = (readPos + toRead) % cap;
        used -= toRead;
        return toRead;
    }

    void Clear() { readPos = writePos = used = 0; }
};

// ── Port state ─────────────────────────────────────────────────────────────
struct Port {
    PortConfig           cfg;
    AudioFillFn          fill;
    float                volume  = 1.0f;
    bool                 running = false;
    RingBuffer           ring;
    std::mutex           ringMtx;

    // Sine self-test
    bool                 sineTest  = false;
    double               sinePhase = 0.0;
    float                sineFreq  = 440.f;

    explicit Port(const PortConfig& c, AudioFillFn fn, size_t ringBytes)
        : cfg(c), fill(std::move(fn)), ring(ringBytes) {}
};

// ── Global audio state ─────────────────────────────────────────────────────
namespace {

struct AudioState {
    bool                                     initialised = false;
    AudioConfig                              config;
    std::atomic<float>                       masterVolume{1.0f};
    std::mutex                               mutex;
    std::unordered_map<PortHandle, std::unique_ptr<Port>> ports;
    PortHandle                               nextHandle = 1;
    double                                   latencyMs  = 0.0;

#if defined(PS5X_HAVE_SDL2)
    SDL_AudioDeviceID                        device = 0;
#endif

    static AudioState& Get() { static AudioState s; return s; }
};

// SDL2 audio callback – mixes all running ports into the output buffer
#if defined(PS5X_HAVE_SDL2)
void SDLCALL AudioCallback(void* /*userdata*/, uint8_t* stream, int len)
{
    std::memset(stream, 0, static_cast<size_t>(len));
    auto& st = AudioState::Get();

    float mv = st.masterVolume.load(std::memory_order_relaxed);
    std::lock_guard lk(st.mutex);

    for (auto& [h, port] : st.ports) {
        if (!port->running) continue;

        size_t needed = static_cast<size_t>(len);

        // Fill the ring buffer via user callback
        if (port->fill) {
            size_t frameSz = port->cfg.channels *
                             (port->cfg.format == SampleFormat::Int16 ? 2 : 4);
            size_t frames  = needed / frameSz;
            std::vector<uint8_t> tmp(needed, 0);
            port->fill(tmp.data(), static_cast<uint32_t>(frames));
            {
                std::lock_guard rl(port->ringMtx);
                port->ring.Write(tmp.data(), tmp.size());
            }
        } else if (port->sineTest) {
            // Generate sine wave for self-test
            size_t frames = needed / (port->cfg.channels * 2); // assume int16
            std::vector<int16_t> sine(frames * port->cfg.channels);
            for (size_t i = 0; i < frames; ++i) {
                float s = std::sin(
                    static_cast<float>(port->sinePhase * 2.0 * std::numbers::pi));
                port->sinePhase += port->sineFreq / static_cast<double>(port->cfg.sampleRate);
                if (port->sinePhase >= 1.0) port->sinePhase -= 1.0;
                int16_t sample = static_cast<int16_t>(s * 16000.f);
                for (uint16_t ch = 0; ch < port->cfg.channels; ++ch)
                    sine[i * port->cfg.channels + ch] = sample;
            }
            std::lock_guard rl(port->ringMtx);
            port->ring.Write(sine.data(), frames * port->cfg.channels * 2);
        }

        // Mix from ring into stream
        std::vector<uint8_t> mix(needed, 0);
        {
            std::lock_guard rl(port->ringMtx);
            port->ring.Read(mix.data(), needed);
        }

        // Simple mix: float32 master volume on int16 samples
        auto* out = reinterpret_cast<int16_t*>(stream);
        const auto* src = reinterpret_cast<const int16_t*>(mix.data());
        size_t samples = needed / 2;
        float vol = port->volume * mv;
        for (size_t i = 0; i < samples; ++i) {
            int32_t v = static_cast<int32_t>(out[i]) +
                        static_cast<int32_t>(static_cast<float>(src[i]) * vol);
            out[i] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
        }
    }
}
#endif

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────

bool Init(const AudioConfig& cfg)
{
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    if (st.initialised) return true;

    st.config = cfg;
    st.masterVolume.store(cfg.masterVolume);

#if defined(PS5X_HAVE_SDL2)
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            PS5X_ERROR("[Audio] SDL_InitSubSystem(AUDIO): %s", SDL_GetError());
            return false;
        }
    }

    SDL_AudioSpec want{}, have{};
    want.freq     = static_cast<int>(cfg.sampleRate);
    want.format   = AUDIO_S16SYS;
    want.channels = static_cast<uint8_t>(cfg.channels);
    want.samples  = cfg.bufferSamples;
    want.callback = AudioCallback;
    want.userdata = nullptr;

    st.device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (st.device == 0) {
        PS5X_ERROR("[Audio] SDL_OpenAudioDevice: %s", SDL_GetError());
        return false;
    }

    st.latencyMs = static_cast<double>(have.samples) /
                   static_cast<double>(have.freq) * 1000.0;
    PS5X_INFO("[Audio] SDL device %u: %d Hz %u ch buf=%u lat=%.1f ms",
              st.device, have.freq, have.channels, have.samples, st.latencyMs);

    SDL_PauseAudioDevice(st.device, 0); // start
#else
    PS5X_WARN("[Audio] SDL2 not available – audio is silent.");
#endif

    st.initialised = true;
    PS5X_INFO("[Audio] Init: %u Hz %u ch buf=%u",
              cfg.sampleRate, cfg.channels, cfg.bufferSamples);
    return true;
}

void Shutdown()
{
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
#if defined(PS5X_HAVE_SDL2)
    if (st.device) {
        SDL_PauseAudioDevice(st.device, 1);
        SDL_CloseAudioDevice(st.device);
        st.device = 0;
    }
#endif
    st.ports.clear();
    st.initialised = false;
    PS5X_INFO("[Audio] Shutdown.");
}

PortHandle OpenPort(const PortConfig& cfg, AudioFillFn fillCallback)
{
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    if (!st.initialised) {
        PS5X_ERROR("[Audio] OpenPort before Init.");
        return INVALID_PORT;
    }

    // Ring: 4 × buffer worth of samples
    size_t frameSz = cfg.channels * (cfg.format == SampleFormat::Int16 ? 2 : 4);
    size_t ringBytes = frameSz * cfg.bufferSamples * 4;

    PortHandle h = st.nextHandle++;
    st.ports[h] = std::make_unique<Port>(cfg, std::move(fillCallback), ringBytes);
    PS5X_INFO("[Audio] Port %d: %u Hz %u ch buf=%u", h, cfg.sampleRate, cfg.channels, cfg.bufferSamples);
    return h;
}

bool ClosePort(PortHandle h)
{
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    return st.ports.erase(h) > 0;
}

bool SetPortVolume(PortHandle h, float v)
{
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    auto it = st.ports.find(h);
    if (it == st.ports.end()) return false;
    it->second->volume = std::clamp(v, 0.f, 1.f);
    return true;
}

bool Start(PortHandle h)
{
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    auto it = st.ports.find(h);
    if (it == st.ports.end()) return false;
    it->second->running = true;
    PS5X_INFO("[Audio] Port %d started.", h);
    return true;
}

bool Stop(PortHandle h)
{
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    auto it = st.ports.find(h);
    if (it == st.ports.end()) return false;
    it->second->running = false;
    PS5X_INFO("[Audio] Port %d stopped.", h);
    return true;
}

bool IsRunning(PortHandle h)
{
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    auto it = st.ports.find(h);
    return it != st.ports.end() && it->second->running;
}

void SetMasterVolume(float v)
{
    AudioState::Get().masterVolume.store(std::clamp(v, 0.f, 1.f));
}

float GetMasterVolume() { return AudioState::Get().masterVolume.load(); }



// ── Phase 6 implementations ───────────────────────────────────────────────

namespace {

struct AudioExtState
{
    // Stats
    std::atomic<uint64_t> framesRendered{0};
    std::atomic<uint64_t> underruns{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<double>   peakAmplitude{0.0};

    // Channel volumes per port
    std::unordered_map<PortHandle, std::array<float, 8>> channelVols;

    // Spatial positions per port
    std::unordered_map<PortHandle, SpatialPos> spatialPos;

    // Device management
    std::string currentDevice = "default";
    std::mutex  devMtx;

    // Underrun callback
    UnderrunFn  underrunCb;
    std::mutex  cbMtx;

    static AudioExtState& Get() { static AudioExtState s; return s; }
};

} // namespace (Phase 6)

AudioStats GetStats()
{
    auto& s  = AudioExtState::Get();
    auto& st = AudioState::Get();
    AudioStats stats;
    stats.framesRendered = s.framesRendered.load();
    stats.underruns      = s.underruns.load();
    stats.overruns       = s.overruns.load();
    stats.peakAmplitude  = s.peakAmplitude.load();
    stats.latencyMs      = st.latencyMs;
    std::lock_guard lk(st.mutex);
    stats.activePorts    = 0;
    for (auto& [h, p] : st.ports)
        if (p->running) ++stats.activePorts;
    return stats;
}

void ResetStats()
{
    auto& s = AudioExtState::Get();
    s.framesRendered.store(0);
    s.underruns.store(0);
    s.overruns.store(0);
    s.peakAmplitude.store(0.0);
}

bool SetChannelVolumes(PortHandle h, const float volumes[8])
{
    auto& s  = AudioExtState::Get();
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    if (st.ports.find(h) == st.ports.end()) return false;
    auto& arr = s.channelVols[h];
    for (int i = 0; i < 8; ++i)
        arr[i] = std::clamp(volumes[i], 0.f, 1.f);
    return true;
}

bool GetChannelVolumes(PortHandle h, float volumes[8])
{
    auto& s  = AudioExtState::Get();
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    if (st.ports.find(h) == st.ports.end()) return false;
    auto it = s.channelVols.find(h);
    if (it == s.channelVols.end()) {
        for (int i = 0; i < 8; ++i) volumes[i] = 1.f;
    } else {
        for (int i = 0; i < 8; ++i) volumes[i] = it->second[i];
    }
    return true;
}

bool SetPortSpatialPosition(PortHandle h, const SpatialPos& pos)
{
    auto& s  = AudioExtState::Get();
    auto& st = AudioState::Get();
    std::lock_guard lk(st.mutex);
    if (st.ports.find(h) == st.ports.end()) return false;
    s.spatialPos[h] = pos;
    PS5X_DEBUG("[Audio] Port %d spatial pos (%.2f, %.2f, %.2f)",
               h, pos.x, pos.y, pos.z);
    return true;
}

double GetLatencyMs()
{
    return AudioState::Get().latencyMs;
}

std::vector<AudioDeviceInfo> EnumerateDevices()
{
    // Without real SDL2, return a simulated device list.
    // In a real environment this would call SDL_GetNumAudioDevices etc.
    std::vector<AudioDeviceInfo> devs;
#if defined(PS5X_HAVE_SDL2)
    int n = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < n; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        if (name) devs.push_back({name, 8, 48000});
    }
#endif
    if (devs.empty())
        devs.push_back({"default", 8, 48000});
    return devs;
}

bool SelectDevice(const std::string& name)
{
    auto& s = AudioExtState::Get();
    std::lock_guard lk(s.devMtx);
    PS5X_INFO("[Audio] SelectDevice '%s'", name.c_str());
    s.currentDevice = name;
    // A full implementation would re-open the SDL device here.
    return true;
}

std::string GetCurrentDevice()
{
    auto& s = AudioExtState::Get();
    std::lock_guard lk(s.devMtx);
    return s.currentDevice;
}

void SetUnderrunCallback(UnderrunFn fn)
{
    auto& s = AudioExtState::Get();
    std::lock_guard lk(s.cbMtx);
    s.underrunCb = std::move(fn);
}

} // namespace PS5x::Audio
