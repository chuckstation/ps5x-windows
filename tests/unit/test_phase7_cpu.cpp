// PS5x – Phase 7 CPU Interpreter tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Cpu/Cpu.h"

using namespace PS5x::Cpu;

// ── Helpers ───────────────────────────────────────────────────────────────

/// Write bytes into a small aligned buffer and return pointer.
/// Used to build micro-programs for the interpreter tests.
static std::vector<uint8_t> Encode(std::initializer_list<uint8_t> bytes)
{
    return std::vector<uint8_t>(bytes);
}

static void LoadAndStep(const std::vector<uint8_t>& code, int steps = 1)
{
    auto& ctx = GetContext();
    // Point RIP at our code buffer (host address = guest address for tests)
    ctx.rip = reinterpret_cast<uint64_t>(code.data());
    for (int i = 0; i < steps; ++i) Step();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Lifecycle::InitShutdown", "[cpu][phase7]")
{
    CHECK(Init());
    CHECK(GetContext().rip == 0);
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Lifecycle::Reset", "[cpu][phase7]")
{
    Init();
    SetRip(0xDEAD);
    Reset();
    CHECK(GetContext().rip == 0);
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Lifecycle::SetRip", "[cpu][phase7]")
{
    Init();
    SetRip(0x1234'5678ULL);
    CHECK(GetContext().rip == 0x1234'5678ULL);
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Lifecycle::SetRsp", "[cpu][phase7]")
{
    Init();
    SetRsp(0x7FFF'0000ULL);
    CHECK(GetContext().gpr_get(Reg::RSP) == 0x7FFF'0000ULL);
    Shutdown();
}

// ── Register names ─────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::RegNames::AllNames", "[cpu][phase7]")
{
    CHECK(std::string(RegName(Reg::RAX)) == "rax");
    CHECK(std::string(RegName(Reg::RBX)) == "rbx");
    CHECK(std::string(RegName(Reg::RCX)) == "rcx");
    CHECK(std::string(RegName(Reg::RDX)) == "rdx");
    CHECK(std::string(RegName(Reg::RSP)) == "rsp");
    CHECK(std::string(RegName(Reg::RBP)) == "rbp");
    CHECK(std::string(RegName(Reg::RSI)) == "rsi");
    CHECK(std::string(RegName(Reg::RDI)) == "rdi");
    CHECK(std::string(RegName(Reg::R8))  == "r8");
    CHECK(std::string(RegName(Reg::R15)) == "r15");
}

// ── StepResultName ─────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::StepResultName::AllValues", "[cpu][phase7]")
{
    CHECK(std::string(StepResultName(StepResult::Ok))            == "Ok");
    CHECK(std::string(StepResultName(StepResult::Breakpoint))    == "Breakpoint");
    CHECK(std::string(StepResultName(StepResult::Syscall))       == "Syscall");
    CHECK(std::string(StepResultName(StepResult::Fault))         == "Fault");
    CHECK(std::string(StepResultName(StepResult::Halt))          == "Halt");
    CHECK(std::string(StepResultName(StepResult::Unimplemented)) == "Unimplemented");
    CHECK(std::string(StepResultName(StepResult::Exit))          == "Exit");
}

// ── NOP instruction ────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Insn::NOP", "[cpu][phase7]")
{
    Init();
    auto code = Encode({0x90});  // NOP
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().rip = base;
    auto r = Step();
    CHECK(r == StepResult::Ok);
    CHECK(GetContext().rip == base + 1);
    Shutdown();
}

// ── HLT instruction ────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Insn::HLT", "[cpu][phase7]")
{
    Init();
    auto code = Encode({0xF4});  // HLT
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    auto r = Step();
    CHECK(r == StepResult::Halt);
    Shutdown();
}

// ── SYSCALL instruction ────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Insn::SYSCALL", "[cpu][phase7]")
{
    Init();
    auto code = Encode({0x0F, 0x05});  // SYSCALL
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    auto r = Step();
    CHECK(r == StepResult::Syscall);
    Shutdown();
}

// ── INT 3 breakpoint ───────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Insn::INT3", "[cpu][phase7]")
{
    Init();
    auto code = Encode({0xCD, 0x03});  // INT 3
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    auto r = Step();
    CHECK(r == StepResult::Breakpoint);
    Shutdown();
}

// ── XOR reg, reg (self-zero) ───────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Insn::XOR_RegReg", "[cpu][phase7]")
{
    Init();
    GetContext().gpr_set(Reg::RAX, 0xDEADBEEF);
    // REX.W + XOR rax, rax = 48 33 C0
    auto code = Encode({0x48, 0x33, 0xC0});
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    auto r = Step();
    CHECK(r == StepResult::Ok);
    CHECK(GetContext().gpr_get(Reg::RAX) == 0);
    CHECK(GetContext().flag(Flags::ZF));
    Shutdown();
}

// ── JMP short ─────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Insn::JMP_Short", "[cpu][phase7]")
{
    Init();
    // EB 02 = JMP +2 (skip 2 bytes past end of insn)
    auto code = Encode({0xEB, 0x02, 0x90, 0x90, 0xF4});
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().rip = base;
    auto r = Step();
    CHECK(r == StepResult::Ok);
    // RIP should be base + 2 (insn len) + 2 (rel8) = base + 4
    CHECK(GetContext().rip == base + 4);
    Shutdown();
}

// ── JE / JNE ──────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Insn::JE_Taken", "[cpu][phase7]")
{
    Init();
    // 74 02 = JE +2  (taken when ZF=1)
    auto code = Encode({0x74, 0x02, 0x90, 0x90, 0xF4});
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().rip = base;
    GetContext().rflags |= Flags::ZF;
    Step();
    CHECK(GetContext().rip == base + 4);
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Insn::JE_NotTaken", "[cpu][phase7]")
{
    Init();
    auto code = Encode({0x74, 0x02, 0x90, 0x90, 0xF4});
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().rip = base;
    GetContext().rflags &= ~Flags::ZF;
    Step();
    CHECK(GetContext().rip == base + 2);
    Shutdown();
}

// ── Breakpoints ────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Breakpoints::AddRemove", "[cpu][phase7]")
{
    Init();
    uint32_t id = AddBreakpoint(0xABCD, "test");
    CHECK(id != 0);
    CHECK(IsBreakpoint(0xABCD));
    CHECK(RemoveBreakpoint(id));
    CHECK_FALSE(IsBreakpoint(0xABCD));
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Breakpoints::Clear", "[cpu][phase7]")
{
    Init();
    AddBreakpoint(0x1000, "a");
    AddBreakpoint(0x2000, "b");
    ClearBreakpoints();
    CHECK_FALSE(IsBreakpoint(0x1000));
    CHECK_FALSE(IsBreakpoint(0x2000));
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Breakpoints::HitOnStep", "[cpu][phase7]")
{
    Init();
    auto code = Encode({0x90, 0x90, 0xF4});
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    AddBreakpoint(base, "entry");
    GetContext().rip = base;
    auto r = Step();
    CHECK(r == StepResult::Breakpoint);
    CHECK(GetStats().breakpointsHit >= 1);
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Breakpoints::RemoveInvalid", "[cpu][phase7]")
{
    Init();
    CHECK_FALSE(RemoveBreakpoint(99999));
    Shutdown();
}

// ── Statistics ─────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Stats::Reset", "[cpu][phase7]")
{
    Init();
    auto code = Encode({0x90});
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    Step();
    ResetStats();
    CHECK(GetStats().instructionsExecuted == 0);
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Stats::IncrementOnStep", "[cpu][phase7]")
{
    Init();
    auto code = Encode({0x90, 0x90, 0x90});
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().rip = base;
    Step(); Step(); Step();
    CHECK(GetStats().instructionsExecuted >= 3);
    Shutdown();
}

// ── Decode / Disassemble ───────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Decode::NOP", "[cpu][phase7]")
{
    Init();
    uint8_t buf[] = {0x90};
    auto d = Decode(buf, 1);
    REQUIRE(d.has_value());
    CHECK(d->opcode == 0x90);
    CHECK(d->mnemonic.find("nop") != std::string::npos);
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Decode::NullReturnsNullopt", "[cpu][phase7]")
{
    Init();
    auto d = Decode(nullptr, 0);
    CHECK_FALSE(d.has_value());
    Shutdown();
}

TEST_CASE("Phase7::Cpu::Disassemble::ReturnsCount", "[cpu][phase7]")
{
    Init();
    // Three NOPs
    auto code = Encode({0x90, 0x90, 0x90});
    auto dis = Disassemble(reinterpret_cast<uint64_t>(code.data()), 3);
    CHECK(dis.size() == 3);
    for (auto& d : dis) CHECK(d.opcode == 0x90);
    Shutdown();
}

// ── SetContext / GetContext ────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Context::SetAndGet", "[cpu][phase7]")
{
    Init();
    CpuContext ctx;
    ctx.rip = 0x4000;
    ctx.gpr_set(Reg::RBX, 0x1234);
    ctx.rflags = Flags::ZF | Flags::CF;
    SetContext(ctx);
    auto got = GetContextConst();
    CHECK(got.rip == 0x4000);
    CHECK(got.gpr_get(Reg::RBX) == 0x1234);
    CHECK(got.flag(Flags::ZF));
    CHECK(got.flag(Flags::CF));
    Shutdown();
}

// ── Instruction callback ───────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::Callback::InsnFired", "[cpu][phase7]")
{
    Init();
    int fired = 0;
    SetInsnCallback([&](uint64_t, const DecodedInsn&){ ++fired; });
    auto code = Encode({0x90, 0x90});
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().rip = base;
    Step(); Step();
    CHECK(fired == 2);
    SetInsnCallback(nullptr);
    Shutdown();
}

// ── Call stack ─────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Cpu::CallStack::EmptyOnInit", "[cpu][phase7]")
{
    Init();
    CHECK(GetCallStack().empty());
    Shutdown();
}

TEST_CASE("Phase7::Cpu::CallStack::MaxDepthRespected", "[cpu][phase7]")
{
    Init();
    auto cs = GetCallStack(0);
    CHECK(cs.empty());
    Shutdown();
}
