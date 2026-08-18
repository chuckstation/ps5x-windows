// PS5x – Process Manager
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// Manages PS5 process lifecycle:
//   • Address space setup
//   • Module loading and registration
//   • Main thread creation
//   • Exit handling and resource cleanup
#pragma once

#include "PS5x/Loader/Loader.h"
#include "PS5x/KernelRuntime/KernelRuntime.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace PS5x::Process {

// ── Process state ─────────────────────────────────────────────────────────
enum class ProcessState : uint8_t
{
    None       = 0,
    Created    = 1,
    Loading    = 2,
    Running    = 3,
    Exiting    = 4,
    Terminated = 5,
    Faulted    = 6,
};

const char* ProcessStateName(ProcessState s);

// ── Module record ─────────────────────────────────────────────────────────
struct Module
{
    std::string                  name;
    std::filesystem::path        path;
    PS5x::Loader::ExecutableInfo elfInfo;
    bool                         isMain = false;
};

// ── Process descriptor ────────────────────────────────────────────────────
struct ProcessInfo
{
    uint32_t                     pid         = 0;
    std::string                  titleId;
    std::string                  appVersion;
    ProcessState                 state       = ProcessState::None;
    KernelRuntime::KHandle       mainThread  = KernelRuntime::INVALID_HANDLE;
    std::vector<Module>          modules;
    int                          exitCode    = 0;
    uint64_t                     startTimeUs = 0;
    uint64_t                     exitTimeUs  = 0;
};

// ── Exit callback ─────────────────────────────────────────────────────────
using ExitCallbackFn = std::function<void(uint32_t pid, int exitCode)>;

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();

// ── Process management ────────────────────────────────────────────────────

/// Create a new process from an ELF executable.
/// Validates the firmware path before loading.
/// @param elfPath     Path to the main executable.
/// @param contentPath Guest /app0/ host directory.
/// @param firmwarePath User-supplied firmware directory (not bundled).
/// @returns PID on success, 0 on failure.
uint32_t Create(const std::filesystem::path& elfPath,
                const std::filesystem::path& contentPath,
                const std::filesystem::path& firmwarePath);

/// Start the main thread of the created process.
bool Start(uint32_t pid);

/// Request graceful exit.
bool RequestExit(uint32_t pid, int exitCode = 0);

/// Wait for process to terminate.
bool Wait(uint32_t pid, int* exitCode = nullptr,
          uint64_t timeoutUs = UINT64_MAX);

/// Forcibly terminate a process and clean up all resources.
void Terminate(uint32_t pid);

// ── Module management ─────────────────────────────────────────────────────

/// Load an additional shared module into an existing process.
bool LoadModule(uint32_t pid, const std::filesystem::path& path);

/// Get all loaded modules for a process.
std::vector<Module> GetModules(uint32_t pid);

// ── Queries ───────────────────────────────────────────────────────────────
ProcessInfo     GetInfo(uint32_t pid);
ProcessState    GetState(uint32_t pid);
bool            IsRunning(uint32_t pid);
uint32_t        GetCurrentPid();   ///< 0 if no process running

// ── Callbacks ─────────────────────────────────────────────────────────────
void RegisterExitCallback(ExitCallbackFn fn);

} // namespace PS5x::Process
