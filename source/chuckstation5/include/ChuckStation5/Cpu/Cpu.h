// ChuckStation5 – Guest CPU Backend
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Provides an x86-64 software interpreter backend for executing
// user-provided homebrew ELF binaries.
// Architecture assumptions:
//   • Guest ISA: x86-64 (AMD64)
//   • ABI:       System V AMD64 (same as Linux)
//   • Address space: 64-bit flat, managed by Memory module


//                    TEST/CMOV/two-byte Jcc (0F 8x), RET imm16, XCHG, LAHF/SAHF.
// Instructions not yet implemented return StepResult::Unimplemented and log a warning.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ChuckStation5::Cpu {

// ── General-purpose register file ────────────────────────────────────────
/// 16 x 64-bit GPRs in the order defined by the x86-64 ABI.
enum class Reg : uint8_t
{
    RAX = 0, RCX, RDX, RBX,
    RSP, RBP, RSI, RDI,
    R8,  R9,  R10, R11,
    R12, R13, R14, R15,
    COUNT,
};
const char* RegName(Reg r);

// ── RFLAGS bits ───────────────────────────────────────────────────────────
namespace Flags {
    inline constexpr uint64_t CF  = 1ULL << 0;   ///< Carry
    inline constexpr uint64_t PF  = 1ULL << 2;   ///< Parity
    inline constexpr uint64_t AF  = 1ULL << 4;   ///< Adjust
    inline constexpr uint64_t ZF  = 1ULL << 6;   ///< Zero
    inline constexpr uint64_t SF  = 1ULL << 7;   ///< Sign
    inline constexpr uint64_t TF  = 1ULL << 8;   ///< Trap
    inline constexpr uint64_t IF  = 1ULL << 9;   ///< Interrupt-enable
    inline constexpr uint64_t DF  = 1ULL << 10;  ///< Direction
    inline constexpr uint64_t OF  = 1ULL << 11;  ///< Overflow
}

// ── CPU context ───────────────────────────────────────────────────────────
struct CpuContext
{
    std::array<uint64_t, static_cast<size_t>(Reg::COUNT)> gpr{};
    uint64_t  rip    = 0;   ///< instruction pointer
    uint64_t  rflags = 0;   ///< RFLAGS
    uint64_t  cs     = 0x33; ///< code segment (64-bit user mode)
    uint64_t  ss     = 0x2b; ///< stack segment

    // SIMD placeholders (XMM0–15, 128 bits each)
    std::array<std::array<uint8_t, 16>, 16> xmm{};

    uint64_t& gpr_ref(Reg r) { return gpr[static_cast<size_t>(r)]; }
    uint64_t  gpr_get(Reg r) const { return gpr[static_cast<size_t>(r)]; }
    void      gpr_set(Reg r, uint64_t v) { gpr[static_cast<size_t>(r)] = v; }

    bool flag(uint64_t mask) const { return (rflags & mask) != 0; }
    void set_flag(uint64_t mask, bool v) {
        if (v) rflags |= mask; else rflags &= ~mask;
    }
};

// ── Decoded instruction (minimal representation) ─────────────────────────
struct DecodedInsn
{
    uint8_t   length   = 0;    ///< byte length of the instruction
    uint8_t   opcode   = 0;    ///< primary opcode byte (after prefixes)
    uint8_t   modrm    = 0;
    uint8_t   sib      = 0;
    int64_t   imm      = 0;    ///< sign-extended immediate
    int32_t   disp     = 0;    ///< memory displacement
    bool      hasModrm = false;
    bool      hasSib   = false;
    bool      hasImm   = false;
    bool      rex_w    = false; ///< REX.W — 64-bit operand
    bool      rex_r    = false; ///< REX.R — extends reg field
    bool      rex_x    = false; ///< REX.X — extends SIB index
    bool      rex_b    = false; ///< REX.B — extends rm/base/opcode reg
    std::string mnemonic;       ///< human-readable, filled by Disassemble()
};

// ── Step result ───────────────────────────────────────────────────────────
enum class StepResult : uint8_t
{
    Ok             = 0,  ///< instruction executed normally
    Breakpoint     = 1,  ///< hit a software breakpoint (INT 3)
    Syscall        = 2,  ///< SYSCALL / INT 0x80 — dispatcher handles it
    Fault          = 3,  ///< #GP, #PF, or other CPU exception
    Halt           = 4,  ///< HLT instruction
    Unimplemented  = 5,  ///< opcode not yet implemented
    Exit           = 6,  ///< guest requested process exit
};
const char* StepResultName(StepResult r);

// ── Call stack frame ──────────────────────────────────────────────────────
struct CallFrame
{
    uint64_t    returnAddr  = 0;
    uint64_t    frameBase   = 0;  ///< RBP at call time
    std::string symbol;           ///< resolved symbol name (best-effort)
};

// ── Interpreter statistics ────────────────────────────────────────────────
struct InterpreterStats
{
    uint64_t  instructionsExecuted = 0;
    uint64_t  syscallsDispatched   = 0;
    uint64_t  faults               = 0;
    uint64_t  breakpointsHit       = 0;
    uint64_t  unimplemented        = 0;
};

// ── Callbacks ─────────────────────────────────────────────────────────────
using InsnEventFn  = std::function<void(uint64_t rip, const DecodedInsn&)>;
using FaultEventFn = std::function<void(uint64_t rip, const char* reason)>;

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();
void Reset();

// ── Context access ────────────────────────────────────────────────────────
CpuContext&       GetContext();
const CpuContext& GetContextConst();
void              SetContext(const CpuContext& ctx);
void              SetRip(uint64_t rip);
void              SetRsp(uint64_t rsp);

// ── Single-step execution ─────────────────────────────────────────────────
StepResult Step();

// ── Run loop ──────────────────────────────────────────────────────────────
/// Run until Halt / Fault / Breakpoint / Syscall or Stop() is called.
/// On Unimplemented the interpreter skips one byte and continues (best-effort).
StepResult Run();

// ── Execution control ─────────────────────────────────────────────────────
void Pause();
void Resume();
void Stop();
bool IsRunning(); ///< Non-blocking query — true if Run() loop is active

// ── Disassembly ───────────────────────────────────────────────────────────
std::optional<DecodedInsn> Decode(const uint8_t* bytes, size_t maxBytes);
std::vector<DecodedInsn>   Disassemble(uint64_t rip, uint32_t count);

// ── Call-stack tracking ───────────────────────────────────────────────────
std::vector<CallFrame> GetCallStack(uint32_t maxDepth = 64);

// ── Breakpoints ───────────────────────────────────────────────────────────
uint32_t AddBreakpoint(uint64_t addr, const std::string& label = "");
bool     RemoveBreakpoint(uint32_t id);
void     ClearBreakpoints();
bool     IsBreakpoint(uint64_t addr);

// ── Statistics ────────────────────────────────────────────────────────────
InterpreterStats GetStats();
void             ResetStats();

// ── Hooks ─────────────────────────────────────────────────────────────────
void SetInsnCallback(InsnEventFn fn);
void SetFaultCallback(FaultEventFn fn);


/// Returns a list of opcode bytes implemented by the interpreter.
std::vector<uint8_t> ImplementedOpcodes();

} // namespace ChuckStation5::Cpu
