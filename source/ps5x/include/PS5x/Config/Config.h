// PS5x – Config module
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
#pragma once

#include "PS5x/Logger/Logger.h"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

namespace PS5x::Config {

// ── Graphics backend enum ─────────────────────────────────────────────────

enum class GraphicsBackend : uint8_t
{
    DirectX12 = 0,   ///< Primary Windows backend (DXGI + D3D12)
    DirectX11 = 1,   ///< Fallback Windows backend (D3D11)
    Vulkan    = 2,   ///< Optional (vulkan-1.dll runtime-loaded)
    OpenGL    = 3,   ///< Legacy
    Null      = 4,   ///< Headless / unit-test mode
};

// ── Input device enum ─────────────────────────────────────────────────────

enum class InputMode : uint8_t
{
    DualSense  = 0,
    DS4        = 1,
    Xbox       = 2,
    Keyboard   = 3,
};

// ── Top-level config structures ───────────────────────────────────────────

struct EmulatorConfig
{
    std::filesystem::path firmwarePath;      ///< User must supply; never bundled.
    std::filesystem::path gameContentPath;
    std::filesystem::path saveDataPath;
    std::filesystem::path logPath;

    Logger::Level         logLevel    = Logger::Level::Info;
    bool                  logConsole  = true;
};

struct GraphicsConfig
{
    GraphicsBackend backend     = GraphicsBackend::DirectX12;
    uint32_t        width       = 1920;
    uint32_t        height      = 1080;
    bool            fullscreen  = false;
    bool            vsync       = true;
    uint32_t        msaa        = 1;
    bool            validation  = false;   ///< Enable GPU validation layers (debug)
};

struct InputConfig
{
    InputMode       mode        = InputMode::DualSense;
    float           deadzone    = 0.1f;
    float           rumbleStrength = 1.0f;
};

struct AudioConfig
{
    uint32_t        sampleRate  = 48000;
    uint16_t        bufferSize  = 512;
    float           masterVolume = 1.0f;
};

struct DebugConfig
{
    bool            enableDebugger    = false;
    bool            dumpShaders       = false;
    bool            dumpMemory        = false;
    bool            traceSyscalls     = false;
    bool            validateMemory    = false;
    std::string     debugDumpPath;
};

struct PS5xConfig
{
    EmulatorConfig  emulator;
    GraphicsConfig  graphics;
    InputConfig     input;
    AudioConfig     audio;
    DebugConfig     debug;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────

/// Load configuration from a TOML/JSON file.
/// Returns false and logs an error if the file cannot be parsed.
bool Load(const std::filesystem::path& path);

/// Save the current config to a file (creates parent directories).
bool Save(const std::filesystem::path& path);

/// Apply built-in defaults.  Called automatically before Load().
void Reset();

/// Access the live singleton config (read-only after Init).
const PS5xConfig& Get();

/// Mutable access – prefer Load()/Save() rather than mutating directly.
PS5xConfig& GetMutable();

// ── Simple key-value string store (Phase 8 UI persistence) ───────────────
/// Set an arbitrary string value by key (dot-separated, e.g. "ui.theme").
void        Set(const std::string& key, const std::string& value);
/// Get a string value by key; returns empty string if not found.
std::string Get(const std::string& key);

// ── Firmware validation ───────────────────────────────────────────────────

/// Returns true if the firmware path points to a valid, user-supplied firmware.
/// PS5x never bundles, downloads, or supplies firmware itself.
bool ValidateFirmwarePath(const std::filesystem::path& path);

} // namespace PS5x::Config
