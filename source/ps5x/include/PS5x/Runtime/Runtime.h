// PS5x – Runtime Manager
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// Manages ordered startup and teardown of all PS5x subsystems.
// Records per-subsystem initialisation timing.
// Enforces the dependency graph so subsystems never start out-of-order.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace PS5x::Runtime
{

// ── Subsystem IDs (topological order) ────────────────────────────────────
enum class SubsystemId : uint32_t
{
	Logger = 0,
	Config = 1,
	Memory = 2,
	Kernel = 3,
	Filesystem = 4,
	Loader = 5,
	KytyAdapter = 6,
	Renderer = 7,
	GPU = 8,
	Audio = 9,
	Input = 10,
	Process = 11,
	Debugger = 12,
	UI = 13,
	COUNT = 14,
};

const char* SubsystemName(SubsystemId id);

// ── Subsystem state ───────────────────────────────────────────────────────
enum class SubsystemState : uint8_t
{
	Unregistered = 0,
	Registered = 1,
	Initialising = 2,
	Running = 3,
	ShuttingDown = 4,
	Stopped = 5,
	Failed = 6,
};

const char* SubsystemStateName(SubsystemState s);

// ── Timing record ─────────────────────────────────────────────────────────
struct SubsystemTiming
{
	SubsystemId id = SubsystemId::COUNT;
	SubsystemState state = SubsystemState::Unregistered;
	double initMs = 0.0; ///< milliseconds to initialise
	double shutdownMs = 0.0;
	std::string failReason;
};

// ── Callbacks ─────────────────────────────────────────────────────────────
using InitFn = std::function<bool()>;
using ShutdownFn = std::function<void()>;

// ── Subsystem descriptor ──────────────────────────────────────────────────
struct SubsystemDesc
{
	SubsystemId id;
	std::string name;
	InitFn init;
	ShutdownFn shutdown;
	std::vector<SubsystemId> deps; ///< must be Running before this starts
	bool optional = false;         ///< failure doesn't abort startup
};

// ── Runtime Manager ───────────────────────────────────────────────────────

/// Register a subsystem. Call before InitAll().
void Register(SubsystemDesc desc);

/// Initialise all registered subsystems in dependency order.
/// Returns false if any required subsystem fails.
bool InitAll();

/// Shut down all running subsystems in reverse order.
void ShutdownAll();

/// Clear all registrations (for testing). Call ShutdownAll() first.
void Reset();

/// Initialise a single subsystem (and all its unstarted deps).
bool InitOne(SubsystemId id);

/// Shut down a single subsystem (after shutting down dependents).
void ShutdownOne(SubsystemId id);

// ── Queries ───────────────────────────────────────────────────────────────
SubsystemState GetState(SubsystemId id);
bool IsRunning(SubsystemId id);
std::optional<SubsystemTiming> GetTiming(SubsystemId id);
std::vector<SubsystemTiming> GetAllTimings();

/// Log a formatted timing report to the PS5x logger.
void ReportTimings();

// ── Default registration ──────────────────────────────────────────────────
/// Register all standard PS5x subsystems with their default init/shutdown
/// functions.  Call once before InitAll().
void RegisterDefaults();

} // namespace PS5x::Runtime
