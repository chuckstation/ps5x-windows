// PS5x – Guest CPU Backend (x86-64 interpreter)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
//
// Phase 8 expansion adds:
//   AND/OR/NOT/NEG (logical/unary), MUL/IMUL/DIV/IDIV (multiply/divide),
//   SHL/SHR/SAR (shifts), MOVZX/MOVSX (zero/sign-extend),
//   TEST (and-without-write), XCHG (exchange),
//   CMOV (conditional move, 0F 4x),
//   Two-byte Jcc near (0F 8x), RET imm16 (0xC2),
//   LAHF / SAHF (0x9F / 0x9E).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "PS5x/Cpu/Cpu.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"

#include <algorithm>
#include <thread>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace PS5x::Cpu {

// ── Internal state ────────────────────────────────────────────────────────

namespace {

using Clock = std::chrono::steady_clock;

struct BpEntry
{
    uint32_t    id;
    uint64_t    addr;
    std::string label;
    bool        active = true;
};

struct CpuState
{
    CpuContext           ctx;
    std::atomic<bool>    running{false};
    std::atomic<bool>    paused{false};
    bool                 initialised = false;
    std::mutex           mtx;

    // Breakpoints
    std::vector<BpEntry>             breakpoints;
    std::unordered_map<uint64_t, uint32_t> bpByAddr;
    uint32_t nextBpId = 1;

    // Call stack tracking
    std::vector<CallFrame>  callStack;

    // Callbacks
    InsnEventFn  insnCb;
    FaultEventFn faultCb;

    // Statistics
    InterpreterStats stats;

    static CpuState& Get() { static CpuState s; return s; }
};

// ── Register name table ────────────────────────────────────────────────────

static constexpr std::array<const char*, 16> kRegNames = {
    "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
    "r8", "r9", "r10","r11","r12","r13","r14","r15"
};

// ── Guest memory helpers ───────────────────────────────────────────────────

static bool GuestRead(uint64_t addr, void* dst, size_t n)
{
    if (!Memory::IsReadable(addr, n)) return false;
    auto hostPtr = reinterpret_cast<const void*>(addr);
    std::memcpy(dst, hostPtr, n);
    return true;
}

static bool GuestWrite(uint64_t addr, const void* src, size_t n)
{
    if (!Memory::IsWritable(addr, n)) return false;
    auto hostPtr = reinterpret_cast<void*>(addr);
    std::memcpy(hostPtr, src, n);
    return true;
}

template<typename T>
static bool GuestRead(uint64_t addr, T& val) {
    return GuestRead(addr, &val, sizeof(T));
}

template<typename T>
static bool GuestWrite(uint64_t addr, T val) {
    return GuestWrite(addr, &val, sizeof(T));
}

// ── Flag update helpers ────────────────────────────────────────────────────

static bool Parity(uint64_t result)
{
    uint8_t v = static_cast<uint8_t>(result & 0xFF);
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1) == 0;
}

static void Mul128U(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) {
#if defined(__SIZEOF_INT128__)
    unsigned __int128 r = static_cast<unsigned __int128>(a) * b;
    lo = static_cast<uint64_t>(r);
    hi = static_cast<uint64_t>(r >> 64);
#elif defined(_MSC_VER) && !defined(__clang__)
    lo = _umul128(a, b, &hi);
#else
    uint64_t a_lo = static_cast<uint32_t>(a), a_hi = a >> 32;
    uint64_t b_lo = static_cast<uint32_t>(b), b_hi = b >> 32;
    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;
    uint64_t cy = (p0 >> 32) + static_cast<uint32_t>(p1) + static_cast<uint32_t>(p2);
    lo = (p0 & 0xFFFFFFFFULL) | ((cy & 0xFFFFFFFFULL) << 32);
    hi = p3 + (p1 >> 32) + (p2 >> 32) + (cy >> 32);
#endif
}

static void Mul128S(int64_t a, int64_t b, int64_t& hi, uint64_t& lo) {
#if defined(__SIZEOF_INT128__)
    __int128 r = static_cast<__int128>(a) * b;
    lo = static_cast<uint64_t>(r);
    hi = static_cast<int64_t>(r >> 64);
#elif defined(_MSC_VER) && !defined(__clang__)
    lo = static_cast<uint64_t>(_mul128(a, b, &hi));
#else
    uint64_t u_hi = 0, u_lo = 0;
    Mul128U(static_cast<uint64_t>(a), static_cast<uint64_t>(b), u_hi, u_lo);
    if (a < 0) u_hi -= static_cast<uint64_t>(b);
    if (b < 0) u_hi -= static_cast<uint64_t>(a);
    lo = u_lo;
    hi = static_cast<int64_t>(u_hi);
#endif
}

static void UpdateFlagsLogic(CpuContext& ctx, uint64_t result, int bits)
{
    uint64_t mask = (bits == 64) ? UINT64_MAX :
                    (bits == 32) ? UINT32_MAX :
                    (bits == 16) ? 0xFFFF      : 0xFF;
    result &= mask;
    ctx.set_flag(Flags::CF, false);
    ctx.set_flag(Flags::OF, false);
    ctx.set_flag(Flags::ZF, result == 0);
    ctx.set_flag(Flags::SF, (result >> (bits - 1)) & 1);
    ctx.set_flag(Flags::PF, Parity(result));
}

static void UpdateFlagsArith(CpuContext& ctx, uint64_t a, uint64_t b,
                              uint64_t result, bool isSub, int bits)
{
    uint64_t mask = (bits == 64) ? UINT64_MAX :
                    (bits == 32) ? UINT32_MAX :
                    (bits == 16) ? 0xFFFF      : 0xFF;
    uint64_t r = result & mask;
    uint64_t sign_bit = 1ULL << (bits - 1);

    ctx.set_flag(Flags::ZF, r == 0);
    ctx.set_flag(Flags::SF, (r >> (bits - 1)) & 1);
    ctx.set_flag(Flags::PF, Parity(r));

    if (isSub) {
        ctx.set_flag(Flags::CF, (a & mask) < (b & mask));
        bool of = (((a ^ b) & (a ^ r)) >> (bits - 1)) & 1;
        ctx.set_flag(Flags::OF, of);
    } else {
        ctx.set_flag(Flags::CF, result > mask);
        bool of = (~(a ^ b) & (a ^ r) & sign_bit) != 0;
        ctx.set_flag(Flags::OF, of);
    }
}

// ── Core execution dispatcher ─────────────────────────────────────────────

static StepResult ExecuteOne(CpuState& st)
{
    auto& ctx = st.ctx;

    if (ctx.rip == 0) {
        PS5X_ERROR("[Cpu] Null RIP – aborting");
        return StepResult::Fault;
    }

    // Pre-fetch up to 32 bytes safely into local buffer padded with 0xF4 (HLT)
    uint8_t code_buf[48];
    std::fill(std::begin(code_buf), std::end(code_buf), 0xF4);

    size_t page_offset = ctx.rip & 4095;
    size_t first_chunk = std::min<size_t>(32, 4096 - page_offset);

    if (Memory::IsReadable(ctx.rip, first_chunk)) {
        std::memcpy(code_buf, reinterpret_cast<const void*>(ctx.rip), first_chunk);
        if (first_chunk < 32) {
            size_t second_chunk = 32 - first_chunk;
            if (Memory::IsReadable(ctx.rip + first_chunk, second_chunk)) {
                std::memcpy(code_buf + first_chunk, reinterpret_cast<const void*>(ctx.rip + first_chunk), second_chunk);
            }
        }
    } else {
        bool any = false;
        for (size_t i = 0; i < 32; ++i) {
            if (Memory::IsReadable(ctx.rip + i, 1)) {
                code_buf[i] = *reinterpret_cast<const uint8_t*>(ctx.rip + i);
                any = true;
            } else {
                break;
            }
        }
        if (!any) return StepResult::Fault;
    }

    const uint8_t* ip = code_buf;

    // Breakpoint check
    if (!st.bpByAddr.empty()) {
        auto it = st.bpByAddr.find(ctx.rip);
        if (it != st.bpByAddr.end()) {
            ++st.stats.breakpointsHit;
            PS5X_DEBUG("[Cpu] Breakpoint hit @ 0x%llx",
                       static_cast<unsigned long long>(ctx.rip));
            return StepResult::Breakpoint;
        }
    }

    size_t off = 0;
    bool rex_w = false, rex_r = false, rex_x = false, rex_b = false;
    bool prefix_66 = false;
    (void)rex_x; (void)prefix_66;

    uint8_t byte = ip[off++];

    while (true) {
        if (byte >= 0x40 && byte <= 0x4F) {
            rex_w = (byte & 0x08) != 0;
            rex_r = (byte & 0x04) != 0;
            rex_x = (byte & 0x02) != 0;
            rex_b = (byte & 0x01) != 0;
            byte  = ip[off++];
        } else if (byte == 0x66) {
            prefix_66 = true;
            byte = ip[off++];
        } else {
            break;
        }
    }

    uint8_t opcode = byte;
    uint64_t saved_rip = ctx.rip;

    auto Advance = [&]{ ctx.rip += off; };

    switch (opcode) {

    // NOP
    case 0x90: Advance(); break;

    // LAHF  (0x9F)
    case 0x9F: {
        uint8_t ah = static_cast<uint8_t>((ctx.rflags & 0xFF) | 0x02); // bit1 always 1
        ctx.gpr[static_cast<size_t>(Reg::RAX)] =
            (ctx.gpr[static_cast<size_t>(Reg::RAX)] & ~0xFF00ULL) | (static_cast<uint64_t>(ah) << 8);
        Advance();
        break;
    }

    // SAHF  (0x9E)
    case 0x9E: {
        uint8_t ah = static_cast<uint8_t>((ctx.gpr[static_cast<size_t>(Reg::RAX)] >> 8) & 0xFF);
        // Transfer SF/ZF/AF/PF/CF from AH to RFLAGS
        const uint64_t mask = Flags::CF | Flags::PF | Flags::AF | Flags::ZF | Flags::SF;
        ctx.rflags = (ctx.rflags & ~mask) | (static_cast<uint64_t>(ah) & mask);
        Advance();
        break;
    }

    // PUSH r64 (0x50–0x57)
    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57: {
        uint8_t reg = (opcode & 0x7) | (rex_b ? 8 : 0);
        uint64_t sp = ctx.gpr[static_cast<size_t>(Reg::RSP)] - 8;
        if (!GuestWrite<uint64_t>(sp, ctx.gpr[reg])) {
            ++st.stats.faults; return StepResult::Fault;
        }
        ctx.gpr[static_cast<size_t>(Reg::RSP)] = sp;
        Advance();
        break;
    }

    // POP r64 (0x58–0x5F)
    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
        uint8_t reg = (opcode & 0x7) | (rex_b ? 8 : 0);
        uint64_t val = 0;
        if (!GuestRead<uint64_t>(ctx.gpr[static_cast<size_t>(Reg::RSP)], val)) {
            ++st.stats.faults; return StepResult::Fault;
        }
        ctx.gpr[reg] = val;
        ctx.gpr[static_cast<size_t>(Reg::RSP)] += 8;
        Advance();
        break;
    }

    // XCHG r64, RAX  (0x90 already handled as NOP; 0x91–0x97 are XCHG)
    case 0x91: case 0x92: case 0x93: case 0x94:
    case 0x95: case 0x96: case 0x97: {
        uint8_t reg = (opcode & 0x7) | (rex_b ? 8 : 0);
        uint64_t tmp = ctx.gpr[static_cast<size_t>(Reg::RAX)];
        ctx.gpr[static_cast<size_t>(Reg::RAX)] = ctx.gpr[reg];
        ctx.gpr[reg] = tmp;
        Advance();
        break;
    }

    // MOV r64, imm64 (B8+r with REX.W)
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
        uint8_t reg = (opcode & 0x7) | (rex_b ? 8 : 0);
        if (rex_w) {
            uint64_t imm; std::memcpy(&imm, ip + off, 8); off += 8;
            ctx.gpr[reg] = imm;
        } else {
            uint32_t imm; std::memcpy(&imm, ip + off, 4); off += 4;
            ctx.gpr[reg] = imm;
        }
        Advance();
        break;
    }

    // ADD / OR / ADC / SBB / AND / SUB / XOR / CMP r/m64, imm8
    case 0x83: {
        uint8_t modrm = ip[off++];
        uint8_t ext   = (modrm >> 3) & 0x7;
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        int8_t  imm8;  std::memcpy(&imm8, ip + off, 1); off++;
        int64_t imm   = imm8;
        Advance();
        uint64_t& dst = ctx.gpr[rm];
        uint64_t  a   = dst;
        uint64_t  b   = static_cast<uint64_t>(imm);
        switch (ext) {
            case 0: { uint64_t r = a + b; UpdateFlagsArith(ctx,a,b,r,false,64); dst=r; break; }
            case 1: { uint64_t r = a | b; UpdateFlagsLogic(ctx,r,64); dst=r; break; }
            case 4: { uint64_t r = a & b; UpdateFlagsLogic(ctx,r,64); dst=r; break; }
            case 5: { uint64_t r = a - b; UpdateFlagsArith(ctx,a,b,r,true, 64); dst=r; break; }
            case 6: { uint64_t r = a ^ b; UpdateFlagsLogic(ctx,r,64); dst=r; break; }
            case 7: { uint64_t r = a - b; UpdateFlagsArith(ctx,a,b,r,true, 64);        break; }
            default:
                PS5X_WARN("[Cpu] 0x83 /%u not implemented @ 0x%llx", ext,
                           static_cast<unsigned long long>(saved_rip));
                ++st.stats.unimplemented;
                return StepResult::Unimplemented;
        }
        break;
    }

    // ADD r/m64, imm32 (sign-extended) — 0x81 /0
    case 0x81: {
        uint8_t modrm = ip[off++];
        uint8_t ext   = (modrm >> 3) & 0x7;
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        int32_t imm32; std::memcpy(&imm32, ip + off, 4); off += 4;
        Advance();
        uint64_t b = static_cast<uint64_t>(static_cast<int64_t>(imm32));
        if ((modrm >> 6) == 3) {
            uint64_t& dst = ctx.gpr[rm];
            uint64_t a = dst;
            switch (ext) {
                case 0: { uint64_t r = a + b; UpdateFlagsArith(ctx,a,b,r,false,64); dst=r; break; }
                case 1: { uint64_t r = a | b; UpdateFlagsLogic(ctx,r,64); dst=r; break; }
                case 4: { uint64_t r = a & b; UpdateFlagsLogic(ctx,r,64); dst=r; break; }
                case 5: { uint64_t r = a - b; UpdateFlagsArith(ctx,a,b,r,true, 64); dst=r; break; }
                case 6: { uint64_t r = a ^ b; UpdateFlagsLogic(ctx,r,64); dst=r; break; }
                case 7: { uint64_t r = a - b; UpdateFlagsArith(ctx,a,b,r,true, 64);        break; }
                default:
                    ++st.stats.unimplemented; return StepResult::Unimplemented;
            }
        }
        break;
    }

    // ADD r64, r/m64  (0x03)
    case 0x03: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            uint64_t a = ctx.gpr[reg], b = ctx.gpr[rm], r = a + b;
            UpdateFlagsArith(ctx, a, b, r, false, 64);
            ctx.gpr[reg] = r;
        } else {
            int32_t disp = 0;
            if (((modrm >> 6) & 3) == 1) { int8_t d; std::memcpy(&d, ip + off - 1, 1); ctx.rip++; disp = d; }
            uint64_t ea  = ctx.gpr[rm] + disp;
            uint64_t b = 0;
            if (!GuestRead<uint64_t>(ea, b)) { ++st.stats.faults; return StepResult::Fault; }
            uint64_t a   = ctx.gpr[reg];
            uint64_t r   = a + b;
            UpdateFlagsArith(ctx, a, b, r, false, 64);
            ctx.gpr[reg] = r;
        }
        break;
    }

    // OR r64, r/m64  (0x0B)
    case 0x0B: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            uint64_t r = ctx.gpr[reg] | ctx.gpr[rm];
            UpdateFlagsLogic(ctx, r, 64);
            ctx.gpr[reg] = r;
        }
        break;
    }

    // AND r64, r/m64  (0x23)
    case 0x23: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            uint64_t r = ctx.gpr[reg] & ctx.gpr[rm];
            UpdateFlagsLogic(ctx, r, 64);
            ctx.gpr[reg] = r;
        }
        break;
    }

    // SUB r64, r/m64  (0x2B)
    case 0x2B: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            uint64_t a = ctx.gpr[reg], b = ctx.gpr[rm], r = a - b;
            UpdateFlagsArith(ctx, a, b, r, true, 64);
            ctx.gpr[reg] = r;
        }
        break;
    }

    // XOR r64, r/m64  (0x33)
    case 0x33: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            uint64_t r = ctx.gpr[reg] ^ ctx.gpr[rm];
            UpdateFlagsLogic(ctx, r, 64);
            ctx.gpr[reg] = r;
        }
        break;
    }

    // TEST r/m64, r64  (0x85) — AND without store
    case 0x85: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            uint64_t r = ctx.gpr[rm] & ctx.gpr[reg];
            UpdateFlagsLogic(ctx, r, 64);
        }
        break;
    }

    // TEST rAX, imm32 (0xA9)
    case 0xA9: {
        int32_t imm; std::memcpy(&imm, ip + off, 4); off += 4;
        Advance();
        uint64_t r = ctx.gpr[static_cast<size_t>(Reg::RAX)] & static_cast<uint64_t>(static_cast<int64_t>(imm));
        UpdateFlagsLogic(ctx, r, rex_w ? 64 : 32);
        break;
    }

    // MOV r/m64, r64  (0x89)
    case 0x89: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            ctx.gpr[rm] = ctx.gpr[reg];
        } else {
            int32_t disp = 0;
            uint8_t mod = (modrm >> 6) & 3;
            if (mod == 1) { int8_t d; std::memcpy(&d, ip + off, 1); off++; ctx.rip++; disp = d; }
            else if (mod == 2) { std::memcpy(&disp, ip + off, 4); off += 4; ctx.rip += 4; }
            uint64_t ea = ctx.gpr[rm] + disp;
            if (rex_w) {
                if (!GuestWrite<uint64_t>(ea, ctx.gpr[reg])) { ++st.stats.faults; return StepResult::Fault; }
            } else {
                if (!GuestWrite<uint32_t>(ea, static_cast<uint32_t>(ctx.gpr[reg]))) { ++st.stats.faults; return StepResult::Fault; }
            }
        }
        break;
    }

    // MOV r64, r/m64  (0x8B)
    case 0x8B: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        uint8_t mod   = (modrm >> 6) & 3;
        if (mod == 3) {
            ctx.gpr[reg] = ctx.gpr[rm];
        } else {
            int32_t disp = 0;
            if (mod == 1) { int8_t d; std::memcpy(&d, ip + off, 1); off++; ctx.rip++; disp = d; }
            else if (mod == 2) { std::memcpy(&disp, ip + off, 4); off += 4; ctx.rip += 4; }
            uint64_t ea = ctx.gpr[rm] + disp;
            if (rex_w) {
                uint64_t v = 0;
                if (!GuestRead<uint64_t>(ea, v)) { ++st.stats.faults; return StepResult::Fault; }
                ctx.gpr[reg] = v;
            } else {
                uint32_t v = 0;
                if (!GuestRead<uint32_t>(ea, v)) { ++st.stats.faults; return StepResult::Fault; }
                ctx.gpr[reg] = static_cast<uint64_t>(v);
            }
        }
        break;
    }

    // JMP rel8  (0xEB)
    case 0xEB: {
        int8_t rel; std::memcpy(&rel, ip + off, 1); off++;
        Advance();
        ctx.rip += static_cast<int64_t>(rel);
        break;
    }

    // JMP rel32  (0xE9)
    case 0xE9: {
        int32_t rel; std::memcpy(&rel, ip + off, 4); off += 4;
        Advance();
        ctx.rip += static_cast<int64_t>(rel);
        break;
    }

    // Jcc rel8 (0x70–0x7F)
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B:
    case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        int8_t rel; std::memcpy(&rel, ip + off, 1); off++;
        Advance();
        uint8_t cc = opcode & 0xF;
        bool taken = false;
        bool cf = ctx.flag(Flags::CF), zf = ctx.flag(Flags::ZF),
             sf = ctx.flag(Flags::SF), of = ctx.flag(Flags::OF),
             pf = ctx.flag(Flags::PF);
        switch (cc) {
            case 0x0: taken =  of; break;            // JO
            case 0x1: taken = !of; break;            // JNO
            case 0x2: taken =  cf; break;            // JB/JC
            case 0x3: taken = !cf; break;            // JAE/JNB
            case 0x4: taken =  zf; break;            // JE/JZ
            case 0x5: taken = !zf; break;            // JNE/JNZ
            case 0x6: taken = cf || zf; break;       // JBE/JNA
            case 0x7: taken = !cf && !zf; break;     // JA/JNBE
            case 0x8: taken =  sf; break;            // JS
            case 0x9: taken = !sf; break;            // JNS
            case 0xA: taken =  pf; break;            // JP/JPE
            case 0xB: taken = !pf; break;            // JNP/JPO
            case 0xC: taken = sf != of; break;       // JL/JNGE
            case 0xD: taken = sf == of; break;       // JGE/JNL
            case 0xE: taken = zf || (sf != of); break; // JLE/JNG
            case 0xF: taken = !zf && (sf == of); break; // JG/JNLE
        }
        if (taken) ctx.rip += static_cast<int64_t>(rel);
        break;
    }

    // CALL rel32  (0xE8)
    case 0xE8: {
        int32_t rel; std::memcpy(&rel, ip + off, 4); off += 4;
        Advance();
        uint64_t sp = ctx.gpr[static_cast<size_t>(Reg::RSP)] - 8;
        if (!GuestWrite<uint64_t>(sp, ctx.rip)) {
            ++st.stats.faults; return StepResult::Fault;
        }
        ctx.gpr[static_cast<size_t>(Reg::RSP)] = sp;
        CallFrame frame;
        frame.returnAddr = ctx.rip;
        frame.frameBase  = ctx.gpr[static_cast<size_t>(Reg::RBP)];
        st.callStack.push_back(frame);
        ctx.rip += static_cast<int64_t>(rel);
        break;
    }

    // RET  (0xC3)
    case 0xC3: {
        off++;
        uint64_t retAddr = 0;
        if (!GuestRead<uint64_t>(ctx.gpr[static_cast<size_t>(Reg::RSP)], retAddr)) {
            ++st.stats.faults; return StepResult::Fault;
        }
        ctx.gpr[static_cast<size_t>(Reg::RSP)] += 8;
        ctx.rip = retAddr;
        if (!st.callStack.empty()) st.callStack.pop_back();
        break;
    }

    // RET imm16  (0xC2)
    case 0xC2: {
        uint16_t imm16; std::memcpy(&imm16, ip + off, 2); off += 2;
        off++;
        uint64_t retAddr = 0;
        if (!GuestRead<uint64_t>(ctx.gpr[static_cast<size_t>(Reg::RSP)], retAddr)) {
            ++st.stats.faults; return StepResult::Fault;
        }
        ctx.gpr[static_cast<size_t>(Reg::RSP)] += 8 + imm16;
        ctx.rip = retAddr;
        if (!st.callStack.empty()) st.callStack.pop_back();
        break;
    }

    // INT imm8  (0xCD)
    case 0xCD: {
        uint8_t vec = ip[off++];
        Advance();
        if (vec == 3) {
            ++st.stats.breakpointsHit;
            return StepResult::Breakpoint;
        }
        if (vec == 0x80) {
            ++st.stats.syscallsDispatched;
            return StepResult::Syscall;
        }
        PS5X_WARN("[Cpu] INT 0x%02x @ 0x%llx", vec,
                   static_cast<unsigned long long>(saved_rip));
        return StepResult::Unimplemented;
    }

    // SYSCALL  (0x0F 0x05) and two-byte opcodes
    case 0x0F: {
        uint8_t op2 = ip[off++];

        // SYSCALL
        if (op2 == 0x05) {
            Advance();
            ++st.stats.syscallsDispatched;
            return StepResult::Syscall;
        }

        // Two-byte Jcc near (0F 80–0F 8F)
        if (op2 >= 0x80 && op2 <= 0x8F) {
            int32_t rel; std::memcpy(&rel, ip + off, 4); off += 4;
            Advance();
            uint8_t cc = op2 & 0xF;
            bool taken = false;
            bool cf = ctx.flag(Flags::CF), zf = ctx.flag(Flags::ZF),
                 sf = ctx.flag(Flags::SF), of = ctx.flag(Flags::OF),
                 pf = ctx.flag(Flags::PF);
            switch (cc) {
                case 0x0: taken =  of; break;
                case 0x1: taken = !of; break;
                case 0x2: taken =  cf; break;
                case 0x3: taken = !cf; break;
                case 0x4: taken =  zf; break;
                case 0x5: taken = !zf; break;
                case 0x6: taken = cf || zf; break;
                case 0x7: taken = !cf && !zf; break;
                case 0x8: taken =  sf; break;
                case 0x9: taken = !sf; break;
                case 0xA: taken =  pf; break;
                case 0xB: taken = !pf; break;
                case 0xC: taken = sf != of; break;
                case 0xD: taken = sf == of; break;
                case 0xE: taken = zf || (sf != of); break;
                case 0xF: taken = !zf && (sf == of); break;
            }
            if (taken) ctx.rip += static_cast<int64_t>(rel);
            break;
        }

        // CMOVcc r64, r/m64 (0F 40–0F 4F)
        if (op2 >= 0x40 && op2 <= 0x4F) {
            uint8_t modrm = ip[off++]; Advance();
            uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
            uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
            if ((modrm >> 6) == 3) {
                uint8_t cc = op2 & 0xF;
                bool taken = false;
                bool cf = ctx.flag(Flags::CF), zf = ctx.flag(Flags::ZF),
                     sf = ctx.flag(Flags::SF), of = ctx.flag(Flags::OF);
                switch (cc) {
                    case 0x0: taken =  of; break;
                    case 0x1: taken = !of; break;
                    case 0x2: taken =  cf; break;
                    case 0x3: taken = !cf; break;
                    case 0x4: taken =  zf; break;
                    case 0x5: taken = !zf; break;
                    case 0x6: taken = cf || zf; break;
                    case 0x7: taken = !cf && !zf; break;
                    case 0x8: taken =  sf; break;
                    case 0x9: taken = !sf; break;
                    case 0xC: taken = sf != of; break;
                    case 0xD: taken = sf == of; break;
                    case 0xE: taken = zf || (sf != of); break;
                    case 0xF: taken = !zf && (sf == of); break;
                    default: taken = false; break;
                }
                if (taken) ctx.gpr[reg] = ctx.gpr[rm];
            }
            break;
        }

        // MOVZX r64, r/m8 (0F B6) / MOVZX r64, r/m16 (0F B7)
        if (op2 == 0xB6 || op2 == 0xB7) {
            uint8_t modrm = ip[off++]; Advance();
            uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
            uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
            if ((modrm >> 6) == 3) {
                if (op2 == 0xB6)
                    ctx.gpr[reg] = ctx.gpr[rm] & 0xFF;       // zero-extend byte
                else
                    ctx.gpr[reg] = ctx.gpr[rm] & 0xFFFF;     // zero-extend word
            }
            break;
        }

        // MOVSX r64, r/m8 (0F BE) / MOVSX r64, r/m16 (0F BF)
        if (op2 == 0xBE || op2 == 0xBF) {
            uint8_t modrm = ip[off++]; Advance();
            uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
            uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
            if ((modrm >> 6) == 3) {
                if (op2 == 0xBE) {
                    int8_t  v = static_cast<int8_t>(ctx.gpr[rm] & 0xFF);
                    ctx.gpr[reg] = static_cast<uint64_t>(static_cast<int64_t>(v));
                } else {
                    int16_t v = static_cast<int16_t>(ctx.gpr[rm] & 0xFFFF);
                    ctx.gpr[reg] = static_cast<uint64_t>(static_cast<int64_t>(v));
                }
            }
            break;
        }

        // IMUL r64, r/m64 (0F AF)
        if (op2 == 0xAF) {
            uint8_t modrm = ip[off++]; Advance();
            uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
            uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
            if ((modrm >> 6) == 3) {
                int64_t a = static_cast<int64_t>(ctx.gpr[reg]);
                int64_t b = static_cast<int64_t>(ctx.gpr[rm]);
                int64_t hi = 0;
                uint64_t lo = 0;
                Mul128S(a, b, hi, lo);
                ctx.gpr[reg] = lo;
                bool overflow = (hi != (static_cast<int64_t>(lo) >> 63));
                ctx.set_flag(Flags::CF, overflow);
                ctx.set_flag(Flags::OF, overflow);
            }
            break;
        }

        PS5X_WARN("[Cpu] 0x0F 0x%02x not implemented @ 0x%llx", op2,
                   static_cast<unsigned long long>(saved_rip));
        ++st.stats.unimplemented;
        return StepResult::Unimplemented;
    }

    // HLT  (0xF4)
    case 0xF4:
        Advance();
        return StepResult::Halt;

    // CMP r/m64, r64  (0x39)
    case 0x39: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            uint64_t a = ctx.gpr[rm], b = ctx.gpr[reg], r = a - b;
            UpdateFlagsArith(ctx, a, b, r, true, 64);
        }
        break;
    }

    // CMP r64, r/m64  (0x3B)
    case 0x3B: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            uint64_t a = ctx.gpr[reg], b = ctx.gpr[rm], r = a - b;
            UpdateFlagsArith(ctx, a, b, r, true, 64);
        }
        break;
    }

    // LEA r64, m  (0x8D)
    case 0x8D: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        uint8_t mod   = (modrm >> 6) & 3;
        int32_t disp  = 0;
        if (mod == 1) { int8_t d; std::memcpy(&d, ip + off, 1); off++; ctx.rip++; disp = d; }
        else if (mod == 2) { std::memcpy(&disp, ip + off, 4); off += 4; ctx.rip += 4; }
        uint64_t ea = ctx.gpr[rm] + static_cast<int64_t>(disp);
        ctx.gpr[reg] = ea;
        break;
    }

    // MOV r/m64, imm32 (sign-extended) — 0xC7 /0
    case 0xC7: {
        uint8_t modrm = ip[off++];
        uint8_t ext   = (modrm >> 3) & 0x7;
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        int32_t imm;  std::memcpy(&imm, ip + off, 4); off += 4;
        Advance();
        if (ext == 0 && (modrm >> 6) == 3) {
            ctx.gpr[rm] = rex_w ? static_cast<uint64_t>(imm)
                                 : static_cast<uint64_t>(static_cast<uint32_t>(imm));
        }
        break;
    }

    // Shift/Rotate group — 0xC1 (imm8) and 0xD3 (CL)
    case 0xC1: case 0xD3: {
        uint8_t modrm = ip[off++];
        uint8_t ext   = (modrm >> 3) & 0x7;
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        uint8_t count = 0;
        if (opcode == 0xC1) { count = ip[off++] & 0x3F; }
        else                 { count = static_cast<uint8_t>(ctx.gpr[static_cast<size_t>(Reg::RCX)]) & 0x3F; }
        Advance();
        if ((modrm >> 6) == 3) {
            uint64_t& v = ctx.gpr[rm];
            switch (ext) {
                case 4: { // SHL / SAL
                    uint64_t r = (count < 64) ? (v << count) : 0;
                    ctx.set_flag(Flags::CF, count > 0 && count <= 64 && ((v >> (64 - count)) & 1));
                    UpdateFlagsLogic(ctx, r, 64);
                    v = r;
                    break;
                }
                case 5: { // SHR
                    uint64_t r = (count < 64) ? (v >> count) : 0;
                    ctx.set_flag(Flags::CF, count > 0 && ((v >> (count-1)) & 1));
                    UpdateFlagsLogic(ctx, r, 64);
                    v = r;
                    break;
                }
                case 7: { // SAR
                    int64_t sv = static_cast<int64_t>(v);
                    uint64_t r = (count < 64) ? static_cast<uint64_t>(sv >> count)
                                              : static_cast<uint64_t>(sv >> 63);
                    ctx.set_flag(Flags::CF, count > 0 && ((sv >> (count-1)) & 1));
                    UpdateFlagsLogic(ctx, r, 64);
                    v = r;
                    break;
                }
                default:
                    ++st.stats.unimplemented; return StepResult::Unimplemented;
            }
        }
        break;
    }

    // INC/DEC/CALL/JMP/etc — 0xFF
    case 0xFF: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t ext   = (modrm >> 3) & 0x7;
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            if (ext == 0) {
                uint64_t& v = ctx.gpr[rm]; uint64_t r = v + 1;
                ctx.set_flag(Flags::ZF, r == 0);
                ctx.set_flag(Flags::SF, (r >> 63) & 1);
                ctx.set_flag(Flags::OF, v == UINT64_MAX>>1);
                v = r;
            } else if (ext == 1) {
                uint64_t& v = ctx.gpr[rm]; uint64_t r = v - 1;
                ctx.set_flag(Flags::ZF, r == 0);
                ctx.set_flag(Flags::SF, (r >> 63) & 1);
                v = r;
            } else if (ext == 2) {
                uint64_t target = ctx.gpr[rm];
                uint64_t sp = ctx.gpr[static_cast<size_t>(Reg::RSP)] - 8;
                if (!GuestWrite<uint64_t>(sp, ctx.rip)) {
                    ++st.stats.faults; return StepResult::Fault;
                }
                ctx.gpr[static_cast<size_t>(Reg::RSP)] = sp;
                CallFrame fr; fr.returnAddr = ctx.rip;
                st.callStack.push_back(fr);
                ctx.rip = target;
            } else if (ext == 4) {
                ctx.rip = ctx.gpr[rm];
            }
        }
        break;
    }

    // NOT r/m64 (0xF7 /2) / NEG (0xF7 /3) / MUL (0xF7 /4) / IMUL (0xF7 /5) / DIV (0xF7 /6) / IDIV (0xF7 /7)
    case 0xF7: {
        uint8_t modrm = ip[off++]; Advance();
        uint8_t ext   = (modrm >> 3) & 0x7;
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        if ((modrm >> 6) == 3) {
            uint64_t& v = ctx.gpr[rm];
            if (ext == 2) {
                v = ~v;
            } else if (ext == 3) {
                uint64_t r = 0 - v;
                ctx.set_flag(Flags::CF, v != 0);
                ctx.set_flag(Flags::ZF, r == 0);
                ctx.set_flag(Flags::SF, (r >> 63) & 1);
                v = r;
            } else if (ext == 4) {
                // MUL RAX × rm → RDX:RAX
                uint64_t hi = 0, lo = 0;
                Mul128U(ctx.gpr[static_cast<size_t>(Reg::RAX)], v, hi, lo);
                ctx.gpr[static_cast<size_t>(Reg::RAX)] = lo;
                ctx.gpr[static_cast<size_t>(Reg::RDX)] = hi;
                bool high = hi != 0;
                ctx.set_flag(Flags::CF, high);
                ctx.set_flag(Flags::OF, high);
            } else if (ext == 5) {
                // IMUL RAX × rm → RDX:RAX
                int64_t hi = 0;
                uint64_t lo = 0;
                Mul128S(static_cast<int64_t>(ctx.gpr[static_cast<size_t>(Reg::RAX)]), static_cast<int64_t>(v), hi, lo);
                ctx.gpr[static_cast<size_t>(Reg::RAX)] = lo;
                ctx.gpr[static_cast<size_t>(Reg::RDX)] = static_cast<uint64_t>(hi);
                bool overflow = (hi != (static_cast<int64_t>(lo) >> 63));
                ctx.set_flag(Flags::CF, overflow);
                ctx.set_flag(Flags::OF, overflow);
            } else if (ext == 6) {
                // DIV
                uint64_t denom = v;
                if (denom == 0) { ++st.stats.faults; return StepResult::Fault; }
                uint64_t num = ctx.gpr[static_cast<size_t>(Reg::RAX)];
                ctx.gpr[static_cast<size_t>(Reg::RAX)] = num / denom;
                ctx.gpr[static_cast<size_t>(Reg::RDX)] = num % denom;
            } else if (ext == 7) {
                // IDIV
                int64_t denom = static_cast<int64_t>(v);
                if (denom == 0) { ++st.stats.faults; return StepResult::Fault; }
                int64_t num = static_cast<int64_t>(ctx.gpr[static_cast<size_t>(Reg::RAX)]);
                ctx.gpr[static_cast<size_t>(Reg::RAX)] = static_cast<uint64_t>(num / denom);
                ctx.gpr[static_cast<size_t>(Reg::RDX)] = static_cast<uint64_t>(num % denom);
            }
        }
        break;
    }

    // TEST r/m64, imm32 (0xF7 /0)  — distinct from above: handled together via 0xF7
    // (already covered in 0xF7 case; here we handle IMUL r64, r/m64, imm32 via 0x69)
    case 0x69: {
        uint8_t modrm = ip[off++];
        uint8_t reg   = ((modrm >> 3) & 0x7) | (rex_r ? 8 : 0);
        uint8_t rm    = (modrm & 0x7) | (rex_b ? 8 : 0);
        int32_t imm;  std::memcpy(&imm, ip + off, 4); off += 4;
        Advance();
        if ((modrm >> 6) == 3) {
            int64_t a = static_cast<int64_t>(ctx.gpr[rm]);
            int64_t b = static_cast<int64_t>(imm);
            int64_t hi = 0;
            uint64_t lo = 0;
            Mul128S(a, b, hi, lo);
            ctx.gpr[reg] = lo;
            bool overflow = (hi != (static_cast<int64_t>(lo) >> 63));
            ctx.set_flag(Flags::CF, overflow);
            ctx.set_flag(Flags::OF, overflow);
        }
        break;
    }

    default:
        PS5X_WARN("[Cpu] Unimplemented opcode 0x%02x @ 0x%llx", opcode,
                   static_cast<unsigned long long>(saved_rip));
        ++st.stats.unimplemented;
        return StepResult::Unimplemented;
    }

    ++st.stats.instructionsExecuted;

    if (st.insnCb) {
        DecodedInsn di;
        di.opcode  = opcode;
        di.rex_w   = rex_w;
        di.length  = static_cast<uint8_t>(off);
        di.mnemonic = "(executed)";
        st.insnCb(saved_rip, di);
    }

    return StepResult::Ok;
}

} // namespace (anonymous)

// ── Public API ────────────────────────────────────────────────────────────

const char* RegName(Reg r)
{
    auto idx = static_cast<size_t>(r);
    if (idx >= kRegNames.size()) return "?";
    return kRegNames[idx];
}

const char* StepResultName(StepResult r)
{
    switch (r) {
        case StepResult::Ok:            return "Ok";
        case StepResult::Breakpoint:    return "Breakpoint";
        case StepResult::Syscall:       return "Syscall";
        case StepResult::Fault:         return "Fault";
        case StepResult::Halt:          return "Halt";
        case StepResult::Unimplemented: return "Unimplemented";
        case StepResult::Exit:          return "Exit";
    }
    return "?";
}

bool Init()
{
    auto& st = CpuState::Get();
    std::lock_guard lk(st.mtx);
    st.ctx        = CpuContext{};
    st.callStack.clear();
    st.breakpoints.clear();
    st.bpByAddr.clear();
    st.stats      = InterpreterStats{};
    st.running.store(false);
    st.paused.store(false);
    st.initialised = true;
    PS5X_INFO("[Cpu] x86-64 interpreter initialised (Phase 8).");
    return true;
}

void Shutdown()
{
    auto& st = CpuState::Get();
    st.running.store(false);
    std::lock_guard lk(st.mtx);
    st.initialised = false;
    st.insnCb  = nullptr;
    st.faultCb = nullptr;
    PS5X_INFO("[Cpu] Interpreter shut down. Instructions executed: %llu",
              static_cast<unsigned long long>(st.stats.instructionsExecuted));
}

void Reset()
{
    Shutdown();
    Init();
}

CpuContext& GetContext()       { return CpuState::Get().ctx; }
const CpuContext& GetContextConst() { return CpuState::Get().ctx; }

void SetContext(const CpuContext& ctx)
{
    std::lock_guard lk(CpuState::Get().mtx);
    CpuState::Get().ctx = ctx;
}

void SetRip(uint64_t rip) { CpuState::Get().ctx.rip = rip; }

void SetRsp(uint64_t rsp)
{
    CpuState::Get().ctx.gpr[static_cast<size_t>(Reg::RSP)] = rsp;
}

StepResult Step()
{
    auto& st = CpuState::Get();
    return ExecuteOne(st);
}

StepResult Run()
{
    auto& st = CpuState::Get();
    st.running.store(true);
    st.paused.store(false);

    StepResult result = StepResult::Ok;
    while (st.running.load()) {
        while (st.paused.load()) {
            std::this_thread::yield();
        }
        if (!st.running.load()) break;

        result = ExecuteOne(st);

        if (result != StepResult::Ok) {
            if (result == StepResult::Unimplemented) {
                ++st.ctx.rip;
                result = StepResult::Ok;
                continue;
            }
            break;
        }
    }
    st.running.store(false);
    PS5X_DEBUG("[Cpu] Run() ended with %s", StepResultName(result));
    return result;
}

void Pause()     { CpuState::Get().paused.store(true);  }
void Resume()    { CpuState::Get().paused.store(false); }
void Stop()      { CpuState::Get().running.store(false); }
bool IsRunning() { return CpuState::Get().running.load(); }

std::optional<DecodedInsn> Decode(const uint8_t* bytes, size_t maxBytes)
{
    if (!bytes || maxBytes == 0) return std::nullopt;

    DecodedInsn d;
    size_t off = 0;
    bool rex_w = false;
    [[maybe_unused]] bool rex_r = false;
    [[maybe_unused]] bool rex_b = false;
    uint8_t byte = bytes[off++];

    while (byte >= 0x40 && byte <= 0x4F && off < maxBytes) {
        d.rex_w = (byte & 0x08) != 0;
        d.rex_r = (byte & 0x04) != 0;
        d.rex_x = (byte & 0x02) != 0;
        d.rex_b = (byte & 0x01) != 0;
        rex_w = d.rex_w; rex_r = d.rex_r; rex_b = d.rex_b;
        byte = bytes[off++];
    }

    d.opcode  = byte;
    d.length  = static_cast<uint8_t>(off);

    static const std::unordered_map<uint8_t, const char*> mnemonics = {
        {0x03,"add"}, {0x0B,"or"},  {0x23,"and"}, {0x2B,"sub"}, {0x33,"xor"},
        {0x39,"cmp"}, {0x3B,"cmp"}, {0x50,"push"},{0x58,"pop"},
        {0x69,"imul"},{0x81,"alu"}, {0x83,"alu"}, {0x85,"test"},{0x89,"mov"},
        {0x8B,"mov"}, {0x8D,"lea"}, {0x90,"nop"}, {0x9E,"sahf"},{0x9F,"lahf"},
        {0xA9,"test"},{0xC1,"shl"}, {0xC2,"ret"}, {0xC3,"ret"}, {0xC7,"mov"},
        {0xD3,"shl"}, {0xE8,"call"},{0xE9,"jmp"}, {0xEB,"jmp short"},
        {0xF4,"hlt"}, {0xF7,"alu"}, {0xFF,"call/jmp/inc"},{0xCD,"int"},
    };
    auto it = mnemonics.find(byte);
    d.mnemonic = (it != mnemonics.end()) ? it->second : "???";
    if (rex_w) d.mnemonic += " [64]";

    return d;
}

std::vector<DecodedInsn> Disassemble(uint64_t rip, uint32_t count)
{
    std::vector<DecodedInsn> out;
    out.reserve(count);
    uint64_t addr = rip;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(addr);
        auto d = Decode(ptr, 15);
        if (!d) break;
        out.push_back(*d);
        uint8_t len = std::max<uint8_t>(d->length, 1);
        addr += len;
    }
    return out;
}

std::vector<CallFrame> GetCallStack(uint32_t maxDepth)
{
    auto& st = CpuState::Get();
    std::lock_guard lk(st.mtx);
    auto& cs = st.callStack;
    uint32_t n = std::min(static_cast<uint32_t>(cs.size()), maxDepth);
    return std::vector<CallFrame>(cs.end() - static_cast<ptrdiff_t>(n), cs.end());
}

uint32_t AddBreakpoint(uint64_t addr, const std::string& label)
{
    auto& st = CpuState::Get();
    std::lock_guard lk(st.mtx);
    uint32_t id = st.nextBpId++;
    st.breakpoints.push_back({id, addr, label, true});
    st.bpByAddr[addr] = id;
    PS5X_DEBUG("[Cpu] Breakpoint %u set @ 0x%llx (%s)",
               id, static_cast<unsigned long long>(addr), label.c_str());
    return id;
}

bool RemoveBreakpoint(uint32_t id)
{
    auto& st = CpuState::Get();
    std::lock_guard lk(st.mtx);
    auto it = std::find_if(st.breakpoints.begin(), st.breakpoints.end(),
                           [id](const BpEntry& b){ return b.id == id; });
    if (it == st.breakpoints.end()) return false;
    st.bpByAddr.erase(it->addr);
    st.breakpoints.erase(it);
    return true;
}

void ClearBreakpoints()
{
    auto& st = CpuState::Get();
    std::lock_guard lk(st.mtx);
    st.breakpoints.clear();
    st.bpByAddr.clear();
}

bool IsBreakpoint(uint64_t addr)
{
    return CpuState::Get().bpByAddr.count(addr) > 0;
}

InterpreterStats GetStats() { return CpuState::Get().stats; }

void ResetStats()
{
    std::lock_guard lk(CpuState::Get().mtx);
    CpuState::Get().stats = InterpreterStats{};
}

void SetInsnCallback(InsnEventFn fn)
{
    std::lock_guard lk(CpuState::Get().mtx);
    CpuState::Get().insnCb = std::move(fn);
}

void SetFaultCallback(FaultEventFn fn)
{
    std::lock_guard lk(CpuState::Get().mtx);
    CpuState::Get().faultCb = std::move(fn);
}

std::vector<uint8_t> ImplementedOpcodes()
{
    return {
        0x03, 0x0B, 0x0F, 0x23, 0x2B, 0x33, 0x39, 0x3B,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
        0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x69, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
        0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x81, 0x83, 0x85, 0x89, 0x8B, 0x8D, 0x90,
        0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x9E, 0x9F, 0xA9, 0xB8, 0xB9, 0xBA, 0xBB,
        0xBC, 0xBD, 0xBE, 0xBF,
        0xC1, 0xC2, 0xC3, 0xC7, 0xCD, 0xD3,
        0xE8, 0xE9, 0xEB, 0xF4, 0xF7, 0xFF,
    };
}

} // namespace PS5x::Cpu
