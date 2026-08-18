// ChuckStation5 – Phase 8 CPU Interpreter tests (expanded opcode coverage)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Cpu/Cpu.h"
#include <array>
#include <cstring>

using namespace ChuckStation5::Cpu;

// ── Helpers ───────────────────────────────────────────────────────────────

static std::vector<uint8_t> Bytes(std::initializer_list<uint8_t> b)
{
    return std::vector<uint8_t>(b);
}

static void Prep(const std::vector<uint8_t>& code)
{
    Init();
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
}

// ── ImplementedOpcodes ────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::ImplementedOpcodes::NonEmpty", "[cpu][phase8]")
{
    auto ops = ImplementedOpcodes();
    CHECK(ops.size() >= 40);
}

TEST_CASE("Phase8::Cpu::ImplementedOpcodes::ContainsPhase7Core", "[cpu][phase8]")
{
    auto ops = ImplementedOpcodes();
    auto has = [&](uint8_t b){ return std::find(ops.begin(), ops.end(), b) != ops.end(); };
    CHECK(has(0x90)); // NOP
    CHECK(has(0xC3)); // RET
    CHECK(has(0xE8)); // CALL
    CHECK(has(0xEB)); // JMP short
    CHECK(has(0xF4)); // HLT
}

TEST_CASE("Phase8::Cpu::ImplementedOpcodes::ContainsPhase8Additions", "[cpu][phase8]")
{
    auto ops = ImplementedOpcodes();
    auto has = [&](uint8_t b){ return std::find(ops.begin(), ops.end(), b) != ops.end(); };
    CHECK(has(0x0B)); // OR
    CHECK(has(0x23)); // AND
    CHECK(has(0x69)); // IMUL r,r/m,imm
    CHECK(has(0x85)); // TEST
    CHECK(has(0xC1)); // SHL
    CHECK(has(0xD3)); // SHL/SHR/SAR (CL)
    CHECK(has(0xF7)); // NOT/NEG/MUL/DIV
    CHECK(has(0x9E)); // SAHF
    CHECK(has(0x9F)); // LAHF
}

// ── AND instruction ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::AND_reg_reg", "[cpu][phase8]")
{
    // AND rax, rcx  (REX.W 0x23 ModRM:rax,rcx)
    // Encoding: 48 23 C1  (REX.W + AND r64,r/m64; ModRM = 11 000 001 = C1)
    auto code = Bytes({0x48, 0x23, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 0xFF0F);
    GetContext().gpr_set(Reg::RCX, 0x0FF0);
    auto r = Step();
    CHECK(r == StepResult::Ok);
    CHECK(GetContext().gpr_get(Reg::RAX) == 0x0F00);
    CHECK(GetContext().flag(Flags::ZF) == false);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::AND_zero_result", "[cpu][phase8]")
{
    auto code = Bytes({0x48, 0x23, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 0xF0);
    GetContext().gpr_set(Reg::RCX, 0x0F);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 0);
    CHECK(GetContext().flag(Flags::ZF) == true);
    Shutdown();
}

// ── OR instruction ────────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::OR_reg_reg", "[cpu][phase8]")
{
    // OR rax, rcx: 48 0B C1
    auto code = Bytes({0x48, 0x0B, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 0xF0);
    GetContext().gpr_set(Reg::RCX, 0x0F);
    auto r = Step();
    CHECK(r == StepResult::Ok);
    CHECK(GetContext().gpr_get(Reg::RAX) == 0xFF);
    CHECK(GetContext().flag(Flags::ZF) == false);
    Shutdown();
}

// ── TEST instruction ──────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::TEST_sets_ZF", "[cpu][phase8]")
{
    // TEST rax, rcx: 48 85 C8  (ModRM 11 001 000 = C8? No: TEST r/m64,r64; ModRM rm=RAX, reg=RCX)
    // TEST r/m64, r64: opcode 0x85, ModRM 11 reg rm. reg=RCX(1), rm=RAX(0) → 0b11_001_000 = 0xC8
    auto code = Bytes({0x48, 0x85, 0xC8});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 0xF0);
    GetContext().gpr_set(Reg::RCX, 0x0F);
    Step();
    CHECK(GetContext().flag(Flags::ZF) == true);
    CHECK(GetContext().gpr_get(Reg::RAX) == 0xF0); // unchanged
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::TEST_clears_ZF", "[cpu][phase8]")
{
    auto code = Bytes({0x48, 0x85, 0xC8});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 0xFF);
    GetContext().gpr_set(Reg::RCX, 0xFF);
    Step();
    CHECK(GetContext().flag(Flags::ZF) == false);
    Shutdown();
}

// ── NOT instruction ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::NOT", "[cpu][phase8]")
{
    // NOT rax: 48 F7 D0  (F7 /2, ModRM 11 010 000 = 0xD0)
    auto code = Bytes({0x48, 0xF7, 0xD0});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 0);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == UINT64_MAX);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::NOT_ones_complement", "[cpu][phase8]")
{
    auto code = Bytes({0x48, 0xF7, 0xD0});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 0x1234567890ABCDEFULL);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == ~0x1234567890ABCDEFULL);
    Shutdown();
}

// ── NEG instruction ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::NEG", "[cpu][phase8]")
{
    // NEG rax: 48 F7 D8  (F7 /3, ModRM 11 011 000 = 0xD8)
    auto code = Bytes({0x48, 0xF7, 0xD8});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 5);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == static_cast<uint64_t>(-5));
    CHECK(GetContext().flag(Flags::CF) == true);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::NEG_zero", "[cpu][phase8]")
{
    auto code = Bytes({0x48, 0xF7, 0xD8});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 0);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 0);
    CHECK(GetContext().flag(Flags::CF) == false);
    CHECK(GetContext().flag(Flags::ZF) == true);
    Shutdown();
}

// ── MUL instruction ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::MUL_basic", "[cpu][phase8]")
{
    // MUL rcx: 48 F7 E1  (F7 /4, ModRM 11 100 001 = 0xE1)
    auto code = Bytes({0x48, 0xF7, 0xE1});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 6);
    GetContext().gpr_set(Reg::RCX, 7);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 42);
    CHECK(GetContext().gpr_get(Reg::RDX) == 0);
    CHECK(GetContext().flag(Flags::CF) == false);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::MUL_overflow_sets_CF", "[cpu][phase8]")
{
    auto code = Bytes({0x48, 0xF7, 0xE1});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, UINT64_MAX);
    GetContext().gpr_set(Reg::RCX, 2);
    Step();
    CHECK(GetContext().flag(Flags::CF) == true); // high half non-zero
    CHECK(GetContext().gpr_get(Reg::RDX) != 0);
    Shutdown();
}

// ── IMUL 3-operand  ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::IMUL_imm32", "[cpu][phase8]")
{
    // IMUL rax, rcx, 10: 48 69 C1 0A 00 00 00
    // ModRM: reg=RAX(0), rm=RCX(1) → 11 000 001 = 0xC1
    auto code = Bytes({0x48, 0x69, 0xC1, 0x0A, 0x00, 0x00, 0x00});
    Prep(code);
    GetContext().gpr_set(Reg::RCX, 3);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 30);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::IMUL_negative", "[cpu][phase8]")
{
    auto code = Bytes({0x48, 0x69, 0xC1, 0xFE, 0xFF, 0xFF, 0xFF}); // imm = -2
    Prep(code);
    GetContext().gpr_set(Reg::RCX, 5);
    Step();
    CHECK(static_cast<int64_t>(GetContext().gpr_get(Reg::RAX)) == -10);
    Shutdown();
}

// ── DIV instruction ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::DIV_basic", "[cpu][phase8]")
{
    // DIV rcx: 48 F7 F1  (F7 /6, ModRM 11 110 001 = 0xF1)
    auto code = Bytes({0x48, 0xF7, 0xF1});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 17);
    GetContext().gpr_set(Reg::RDX, 0);
    GetContext().gpr_set(Reg::RCX, 5);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 3);  // quotient
    CHECK(GetContext().gpr_get(Reg::RDX) == 2);  // remainder
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::DIV_by_zero_faults", "[cpu][phase8]")
{
    auto code = Bytes({0x48, 0xF7, 0xF1});
    Prep(code);
    GetContext().gpr_set(Reg::RCX, 0);
    auto r = Step();
    CHECK(r == StepResult::Fault);
    Shutdown();
}

// ── SHL instruction ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::SHL_imm8", "[cpu][phase8]")
{
    // SHL rax, 3: 48 C1 E0 03  (C1 /4, ModRM 11 100 000 = 0xE0, imm8=3)
    auto code = Bytes({0x48, 0xC1, 0xE0, 0x03});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 1);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 8);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::SHR_imm8", "[cpu][phase8]")
{
    // SHR rax, 2: 48 C1 E8 02  (C1 /5, ModRM 11 101 000 = 0xE8, imm8=2)
    auto code = Bytes({0x48, 0xC1, 0xE8, 0x02});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 16);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 4);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::SAR_preserves_sign", "[cpu][phase8]")
{
    // SAR rax, 1: 48 C1 F8 01  (C1 /7, ModRM 11 111 000 = 0xF8, imm8=1)
    auto code = Bytes({0x48, 0xC1, 0xF8, 0x01});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, static_cast<uint64_t>(-4));
    Step();
    CHECK(static_cast<int64_t>(GetContext().gpr_get(Reg::RAX)) == -2);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::SHL_by_CL", "[cpu][phase8]")
{
    // SHL rax, CL: 48 D3 E0  (D3 /4, ModRM = 0xE0)
    auto code = Bytes({0x48, 0xD3, 0xE0});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 1);
    GetContext().gpr_set(Reg::RCX, 4);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 16);
    Shutdown();
}

// ── MOVZX ─────────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::MOVZX_byte", "[cpu][phase8]")
{
    // MOVZX rax, cl: 48 0F B6 C1  (0F B6; ModRM 11 000 001 = C1)
    auto code = Bytes({0x48, 0x0F, 0xB6, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RCX, 0xFFFF00AB);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 0xAB);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::MOVZX_word", "[cpu][phase8]")
{
    // MOVZX rax, cx: 48 0F B7 C1
    auto code = Bytes({0x48, 0x0F, 0xB7, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RCX, 0xFFFF1234);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 0x1234);
    Shutdown();
}

// ── MOVSX ─────────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::MOVSX_byte_positive", "[cpu][phase8]")
{
    // MOVSX rax, cl: 48 0F BE C1
    auto code = Bytes({0x48, 0x0F, 0xBE, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RCX, 0x7F);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 0x7F);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::MOVSX_byte_negative", "[cpu][phase8]")
{
    auto code = Bytes({0x48, 0x0F, 0xBE, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RCX, 0xFF);  // -1 as int8_t
    Step();
    CHECK(static_cast<int64_t>(GetContext().gpr_get(Reg::RAX)) == -1);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::MOVSX_word_negative", "[cpu][phase8]")
{
    // MOVSX rax, cx: 48 0F BF C1
    auto code = Bytes({0x48, 0x0F, 0xBF, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RCX, 0x8000);  // -32768 as int16_t
    Step();
    CHECK(static_cast<int64_t>(GetContext().gpr_get(Reg::RAX)) == -32768);
    Shutdown();
}

// ── CMOV ──────────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::CMOVZ_taken", "[cpu][phase8]")
{
    // CMOVZ rax, rcx: 48 0F 44 C1  (0F 44 = CMOVZ; ModRM 11 000 001)
    auto code = Bytes({0x48, 0x0F, 0x44, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 99);
    GetContext().gpr_set(Reg::RCX, 42);
    GetContext().set_flag(Flags::ZF, true);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 42);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::CMOVZ_not_taken", "[cpu][phase8]")
{
    auto code = Bytes({0x48, 0x0F, 0x44, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 99);
    GetContext().gpr_set(Reg::RCX, 42);
    GetContext().set_flag(Flags::ZF, false);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 99);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::CMOVNZ_taken", "[cpu][phase8]")
{
    // CMOVNZ rax, rcx: 48 0F 45 C1
    auto code = Bytes({0x48, 0x0F, 0x45, 0xC1});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 1);
    GetContext().gpr_set(Reg::RCX, 2);
    GetContext().set_flag(Flags::ZF, false);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 2);
    Shutdown();
}

// ── Two-byte Jcc near ─────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::JE_near_taken", "[cpu][phase8]")
{
    // JE near +10: 0F 84 0A 00 00 00
    auto code = Bytes({0x0F, 0x84, 0x0A, 0x00, 0x00, 0x00});
    Prep(code);
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().set_flag(Flags::ZF, true);
    Step();
    CHECK(GetContext().rip == base + 6 + 10);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::JNE_near_not_taken", "[cpu][phase8]")
{
    // JNE near: 0F 85 0A 00 00 00
    auto code = Bytes({0x0F, 0x85, 0x0A, 0x00, 0x00, 0x00});
    Prep(code);
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().set_flag(Flags::ZF, true);  // ZF=1 means JNE not taken
    Step();
    CHECK(GetContext().rip == base + 6);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::JL_near_taken", "[cpu][phase8]")
{
    // JL near: 0F 8C xx 00 00 00
    auto code = Bytes({0x0F, 0x8C, 0x05, 0x00, 0x00, 0x00});
    Prep(code);
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().set_flag(Flags::SF, true);
    GetContext().set_flag(Flags::OF, false);  // SF != OF → JL taken
    Step();
    CHECK(GetContext().rip == base + 6 + 5);
    Shutdown();
}

// ── RET imm16 ─────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::RET_imm16", "[cpu][phase8]")
{
    // Build a stack with a return address
    alignas(16) std::array<uint64_t, 4> stack{};
    uint64_t retAddr = 0xDEAD'CAFE;
    stack[0] = retAddr;

    // RET 8 (pop 8 extra bytes after ret): C2 08 00
    auto code = Bytes({0xC2, 0x08, 0x00});
    Prep(code);
    GetContext().gpr_set(Reg::RSP, reinterpret_cast<uint64_t>(stack.data()));
    Step();
    CHECK(GetContext().rip == retAddr);
    CHECK(GetContext().gpr_get(Reg::RSP) ==
          reinterpret_cast<uint64_t>(stack.data()) + 8 + 8); // 8 (ret) + 8 (imm16)
    Shutdown();
}

// ── XCHG ─────────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::XCHG_rax_rcx", "[cpu][phase8]")
{
    // XCHG rax, rcx: 48 91  (REX.W + 0x91 = XCHG RAX, RCX)
    auto code = Bytes({0x48, 0x91});
    Prep(code);
    GetContext().gpr_set(Reg::RAX, 0x1111);
    GetContext().gpr_set(Reg::RCX, 0x2222);
    Step();
    CHECK(GetContext().gpr_get(Reg::RAX) == 0x2222);
    CHECK(GetContext().gpr_get(Reg::RCX) == 0x1111);
    Shutdown();
}

// ── LAHF / SAHF ──────────────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::LAHF_captures_flags", "[cpu][phase8]")
{
    auto code = Bytes({0x9F}); // LAHF
    Prep(code);
    GetContext().set_flag(Flags::ZF, true);
    GetContext().set_flag(Flags::SF, false);
    GetContext().set_flag(Flags::CF, true);
    Step();
    uint8_t ah = static_cast<uint8_t>(GetContext().gpr_get(Reg::RAX) >> 8);
    CHECK((ah & 0x40) != 0); // ZF
    CHECK((ah & 0x01) != 0); // CF
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Insn::SAHF_restores_flags", "[cpu][phase8]")
{
    auto code = Bytes({0x9E}); // SAHF
    Prep(code);
    // AH = 0x45 (ZF=1, CF=1, bit1=always1)
    GetContext().gpr_set(Reg::RAX, 0x4500ULL);
    Step();
    CHECK(GetContext().flag(Flags::ZF) == true);
    CHECK(GetContext().flag(Flags::CF) == true);
    Shutdown();
}

// ── LAHF → SAHF round-trip ────────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::LAHF_SAHF_roundtrip", "[cpu][phase8]")
{
    // Set some flags, LAHF, scramble, SAHF, verify restored
    auto lahf = Bytes({0x9F});
    auto sahf = Bytes({0x9E});

    Init();
    GetContext().set_flag(Flags::ZF, true);
    GetContext().set_flag(Flags::CF, true);
    GetContext().set_flag(Flags::SF, false);

    GetContext().rip = reinterpret_cast<uint64_t>(lahf.data());
    Step();
    uint64_t saved_ah = GetContext().gpr_get(Reg::RAX) & 0xFF00;

    // Scramble flags
    GetContext().set_flag(Flags::ZF, false);
    GetContext().set_flag(Flags::CF, false);

    // Restore via SAHF
    GetContext().gpr_set(Reg::RAX, saved_ah);
    GetContext().rip = reinterpret_cast<uint64_t>(sahf.data());
    Step();

    CHECK(GetContext().flag(Flags::ZF) == true);
    CHECK(GetContext().flag(Flags::CF) == true);
    Shutdown();
}

// ── All Jcc conditions (rel8) ─────────────────────────────────────────────

TEST_CASE("Phase8::Cpu::Insn::JCC_all_conditions_rel8", "[cpu][phase8]")
{
    struct JccCase {
        uint8_t opcode;
        const char* name;
        bool cf, zf, sf, of, pf;
        bool expected_taken;
    };
    std::vector<JccCase> cases = {
        {0x74, "JE",   false, true,  false, false, false, true},
        {0x75, "JNE",  false, false, false, false, false, true},
        {0x72, "JB",   true,  false, false, false, false, true},
        {0x73, "JAE",  false, false, false, false, false, true},
        {0x76, "JBE",  true,  false, false, false, false, true},
        {0x77, "JA",   false, false, false, false, false, true},
        {0x78, "JS",   false, false, true,  false, false, true},
        {0x79, "JNS",  false, false, false, false, false, true},
        {0x7C, "JL",   false, false, true,  false, false, true},   // SF!=OF
        {0x7D, "JGE",  false, false, false, false, false, true},   // SF==OF
        {0x7E, "JLE",  false, true,  false, false, false, true},   // ZF=1
        {0x7F, "JG",   false, false, false, false, false, true},   // !ZF && SF==OF
    };
    for (auto& c : cases) {
        auto code = Bytes({c.opcode, 0x05}); // rel8 = +5
        Init();
        uint64_t base = reinterpret_cast<uint64_t>(code.data());
        GetContext().rip = base;
        GetContext().set_flag(Flags::CF, c.cf);
        GetContext().set_flag(Flags::ZF, c.zf);
        GetContext().set_flag(Flags::SF, c.sf);
        GetContext().set_flag(Flags::OF, c.of);
        GetContext().set_flag(Flags::PF, c.pf);
        Step();
        if (c.expected_taken) {
            CHECK(GetContext().rip == base + 2 + 5);
        } else {
            CHECK(GetContext().rip == base + 2);
        }
        Shutdown();
    }
}

// ── Stats tracking for new opcodes ───────────────────────────────────────

TEST_CASE("Phase8::Cpu::Stats::InstructionCount", "[cpu][phase8]")
{
    Init();
    ResetStats();
    auto code = Bytes({0x90, 0x90, 0x90}); // 3x NOP
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    Step(); Step(); Step();
    CHECK(GetStats().instructionsExecuted == 3);
    Shutdown();
}

TEST_CASE("Phase8::Cpu::Stats::FaultTracked", "[cpu][phase8]")
{
    Init();
    ResetStats();
    // DIV by zero
    auto code = Bytes({0x48, 0xF7, 0xF1});
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    GetContext().gpr_set(Reg::RCX, 0);
    Step();
    CHECK(GetStats().faults == 1);
    Shutdown();
}
