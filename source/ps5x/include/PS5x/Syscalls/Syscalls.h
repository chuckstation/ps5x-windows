// PS5x – Syscall Dispatcher
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
//
// Maps guest syscall numbers to host handler functions.
// Integrates with KernelRuntime, Process, Execution, and RuntimeEvents.
//
// Syscall numbers follow the Linux x86-64 ABI (which PS5 homebrew can use
// when the firmware is not available). Custom PS5-specific syscalls are
// registered separately with numbers >= 0x8000.
//
// Every call is logged. Unknown syscalls are safely skipped with a warning.
// No emulation of proprietary kernel internals is performed.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace PS5x::Cpu
{
struct CpuContext;
}

namespace PS5x::Syscalls
{

// ── Syscall argument extraction ───────────────────────────────────────────
/// Syscall ABI: number in RAX, args in RDI RSI RDX R10 R8 R9, result in RAX.
struct SyscallArgs
{
	uint64_t number = 0;
	uint64_t arg0 = 0; ///< RDI
	uint64_t arg1 = 0; ///< RSI
	uint64_t arg2 = 0; ///< RDX
	uint64_t arg3 = 0; ///< R10
	uint64_t arg4 = 0; ///< R8
	uint64_t arg5 = 0; ///< R9
};

/// Extract syscall args from a CPU context.
SyscallArgs ExtractArgs(const Cpu::CpuContext& ctx);

// ── Handler signature ─────────────────────────────────────────────────────
/// Return value is written back to RAX.
using HandlerFn = std::function<int64_t(const SyscallArgs& args)>;

// ── Syscall descriptor ────────────────────────────────────────────────────
struct SyscallDesc
{
	uint64_t number = 0;
	std::string name;
	HandlerFn handler;
	uint8_t argCount = 0; ///< for validation / logging
};

// ── Statistics ────────────────────────────────────────────────────────────
struct SyscallStats
{
	uint64_t total = 0;
	uint64_t known = 0;
	uint64_t unknown = 0;
	uint64_t errors = 0; ///< handlers returned < 0
};

// ── Well-known Linux x86-64 syscall numbers (subset) ─────────────────────
namespace Nr
{
inline constexpr uint64_t Read = 0;
inline constexpr uint64_t Write = 1;
inline constexpr uint64_t Open = 2;
inline constexpr uint64_t Close = 3;
inline constexpr uint64_t Stat = 4;
inline constexpr uint64_t Fstat = 5;
inline constexpr uint64_t Mmap = 9;
inline constexpr uint64_t Mprotect = 10;
inline constexpr uint64_t Munmap = 11;
inline constexpr uint64_t Brk = 12;
inline constexpr uint64_t Exit = 60;
inline constexpr uint64_t ExitGrp = 231;
inline constexpr uint64_t GetPid = 39;
inline constexpr uint64_t GetTid = 186;
inline constexpr uint64_t Nanosleep = 35;
inline constexpr uint64_t ClockGettime = 228;
inline constexpr uint64_t Sched_yield = 24;
inline constexpr uint64_t Futex = 202;
inline constexpr uint64_t Getrusage = 98;
inline constexpr uint64_t Ioctl = 16;
// PS5-custom range
inline constexpr uint64_t Ps5Base = 0x8000;
} // namespace Nr

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();

/// Register all built-in stub handlers (write → stdout, exit, etc.).
void RegisterBuiltins();

// ── Registration ──────────────────────────────────────────────────────────
void RegisterSyscall(const SyscallDesc& desc);
void RegisterSyscall(uint64_t number, std::string name, HandlerFn handler, uint8_t argCount = 0);

// ── Dispatch ──────────────────────────────────────────────────────────────

/// Dispatch a syscall from the CPU context.
/// Writes the return value back to ctx.gpr[RAX].
/// Returns false only on fatal emulator error (not on syscall error).
bool Dispatch(Cpu::CpuContext& ctx);

/// Validate argument count against registration (warn if mismatched).
bool ValidateArguments(const SyscallArgs& args);

// ── Lookup ────────────────────────────────────────────────────────────────
std::optional<SyscallDesc> Lookup(uint64_t number);
const char* SyscallName(uint64_t number);

// ── Statistics ────────────────────────────────────────────────────────────
SyscallStats GetStats();
void ResetStats();

/// Return the last N dispatched syscall records for the monitor panel.
struct SyscallRecord
{
	uint64_t number = 0;
	std::string name;
	SyscallArgs args;
	int64_t result = 0;
	uint64_t timestampUs = 0;
};
std::vector<SyscallRecord> GetRecentLog(size_t maxEntries = 256);

} // namespace PS5x::Syscalls
