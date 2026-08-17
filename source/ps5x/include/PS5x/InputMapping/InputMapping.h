// PS5x – Input Mapping System
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
//
// Provides configurable mapping between host input devices
// (keyboard, mouse, gamepad, touch) and PS5 DualSense controls.
// Supports named profiles stored in INI format.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace PS5x::InputMapping
{

// ── PS5 DualSense button IDs ─────────────────────────────────────────────
enum class Ps5Button : uint16_t
{
	Triangle = 0,
	Circle,
	Cross,
	Square,
	L1,
	R1,
	L2,
	R2,
	Create,
	Options,
	PS,
	TouchPad,
	L3,
	R3,
	DPadUp,
	DPadDown,
	DPadLeft,
	DPadRight,
	GyroX,
	GyroY,
	GyroZ,
	Count,
};

// ── Host input source types ──────────────────────────────────────────────
enum class HostInputType : uint8_t
{
	Keyboard = 0,
	Mouse = 1,
	Gamepad = 2,
	Touch = 3,
};

// ── A single mapping entry ───────────────────────────────────────────────
struct MappingEntry
{
	Ps5Button target;
	HostInputType sourceType;
	uint32_t sourceId;     // Key code, button index, axis index
	float scale = 1.0f;    // Axis scaling factor
	float deadzone = 0.0f; // Deadzone for analog
	bool inverted = false;
};

// ── A named mapping profile ──────────────────────────────────────────────
struct MappingProfile
{
	std::string name = "Default";
	std::vector<MappingEntry> entries;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();

// ── Profile management ────────────────────────────────────────────────────
/// Load a mapping profile from INI config by name.
bool LoadProfile(const std::string& name);

/// Save the active profile to INI config.
bool SaveProfile(const std::string& name);

/// Delete a profile file.
bool DeleteProfile(const std::string& name);

// ── Active profile ────────────────────────────────────────────────────────
/// Get the currently active mapping profile.
const MappingProfile& GetActiveProfile();

/// Set the active mapping profile (makes a copy).
void SetActiveProfile(const MappingProfile& profile);

/// List available profile names from the input/ directory.
std::vector<std::string> ListProfiles();

// ── Built-in profiles ─────────────────────────────────────────────────────
/// Default keyboard: WASD/Arrows for DPad, J/K/L/I for face buttons.
MappingProfile DefaultKeyboardProfile();

/// Default Xbox controller: 1:1 mapping to DualSense.
MappingProfile DefaultXboxProfile();

/// Default DualSense: 1:1 identity mapping.
MappingProfile DefaultDualSenseProfile();

// ── Input translation ─────────────────────────────────────────────────────
/// Translate a host button/keypress to a PS5 button.
/// Returns true if a mapping was found, fills outButton.
bool TranslateButton(HostInputType type, uint32_t sourceId, Ps5Button& outButton);

/// Translate a host axis value through the active profile.
/// Applies scale, deadzone, and inversion from the matching entry.
float TranslateAxis(HostInputType type, uint32_t sourceId, float value);

} // namespace PS5x::InputMapping
