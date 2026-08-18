// ChuckStation5 – Execution Framework
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
// Provides a clean guest-execution API that sits above Process, KernelRuntime,
// KytyAdapter and Memory without tightly coupling them.
// Startup sequence:
//   Execution::Init()
//   Execution::LoadProgram(path, opts)
//   Execution::Start()
//   Execution::WaitForExit(timeout)
//   Execution::Shutdown()
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ChuckStation5::Execution {

// ── Options ───────────────────────────────────────────────────────────────
struct LoadOptions
{
    std::filesystem::path firmwarePath;   ///< User-supplied; never bundled.
    std::filesystem::path contentPath;    ///< Guest /app0/
    std::filesystem::path saveDataPath;   ///< Guest /savedata/
    bool                  validateElf    = true;
    bool                  traceEntry     = false;  ///< Log entry-point call
    bool                  debuggerAttach = false;
};

// ── Execution state ───────────────────────────────────────────────────────
enum class ExecState : uint8_t
{
    Idle       = 0,
    Loading    = 1,
    Ready      = 2,
    Running    = 3,
    Paused     = 4,
    Exiting    = 5,
    Terminated = 6,
    Faulted    = 7,
};
const char* ExecStateName(ExecState s);

// ── Statistics ────────────────────────────────────────────────────────────
struct ExecStats
{
    uint64_t  pid              = 0;
    ExecState state            = ExecState::Idle;
    double    loadTimeMs       = 0.0;
    double    uptimeMs         = 0.0;
    uint64_t  framesRendered   = 0;
    uint64_t  syscallsEmulated = 0;
    uint32_t  threadCount      = 0;
    uint32_t  moduleCount      = 0;
    size_t    memoryUsedBytes  = 0;
    int       exitCode         = 0;
    std::string lastError;
};

// ── Callbacks ─────────────────────────────────────────────────────────────
using StateChangeFn = std::function<void(ExecState oldState, ExecState newState)>;
using ExitFn        = std::function<void(int exitCode, const std::string& reason)>;
using FaultFn       = std::function<void(uint64_t faultAddr, const std::string& desc)>;

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();

// ── Program management ────────────────────────────────────────────────────

/// Load an ELF executable. Returns false and logs on failure.
/// Firmware path must point to user-supplied firmware – never bundled.
bool LoadProgram(const std::filesystem::path& elfPath, const LoadOptions& opts = {});

/// Start execution. Must call LoadProgram first.
bool Start();

/// Request graceful exit.
bool RequestExit(int exitCode = 0);

/// Block until program exits or timeout elapses.
/// @returns true if program exited, false if timeout
bool WaitForExit(uint64_t timeoutUs = UINT64_MAX);

/// Force terminate without cleanup.
void ForceTerminate();

/// Pause execution (if debugger attached).
bool Pause();

/// Resume from paused state.
bool Resume();

// ── Queries ───────────────────────────────────────────────────────────────
ExecState   GetState();
bool        IsRunning();
ExecStats   GetStats();
std::string GetLastError();

// ── Callbacks ─────────────────────────────────────────────────────────────
void OnStateChange(StateChangeFn fn);
void OnExit(ExitFn fn);
void OnFault(FaultFn fn);

// ── Frame accounting (for renderer integration) ───────────────────────────
void NotifyFrameRendered();
void NotifySyscall(uint32_t sysno);





namespace GuestLoop {

/// Guest instruction-stepping result.
enum class StepResult : uint8_t
{
    Ok          = 0,   ///< stepped one instruction
    Breakpoint  = 1,   ///< hit a breakpoint
    Fault       = 2,   ///< guest fault / exception
    Exit        = 3,   ///< guest requested exit
};

/// Step the guest execution by one instruction.
/// Only valid when state == Paused.
StepResult StepInstruction();

/// Dispatch a guest exception (trap / fault).
/// @param vector   x86-64 exception vector (0 = #DE, 14 = #PF, etc.)
/// @param errorCode  error code if applicable
void DispatchException(uint8_t vector, uint64_t errorCode = 0);

/// Inject a software trap (INT n).
void InjectTrap(uint8_t trapNumber);

} // namespace GuestLoop

/// Exit reason when execution terminates.
enum class ExitReason : uint8_t
{
    Normal      = 0,
    GuestPanic  = 1,
    Fault       = 2,
    Timeout     = 3,
    Requested   = 4,
    Unknown     = 255,
};
const char* ExitReasonName(ExitReason r);

/// Detailed exit information.
struct ExitInfo
{
    ExitReason  reason    = ExitReason::Unknown;
    int         exitCode  = 0;
    uint64_t    faultAddr = 0;
    std::string message;
};

/// Returns exit info after state is Terminated or Faulted.
ExitInfo GetExitInfo();

/// Report a runtime fault from the guest.
void ReportFault(uint64_t faultAddr, const std::string& description);

/// Report a guest panic (unrecoverable).
void ReportGuestPanic(const std::string& message);

} // namespace ChuckStation5::Execution
