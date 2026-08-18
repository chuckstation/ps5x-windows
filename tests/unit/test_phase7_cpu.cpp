// ChuckStation5 – CPU Interpreter tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Cpu/Cpu.h"

using namespace ChuckStation5::Cpu;

// ── Helpers ───────────────────────────────────────────────────────────────

static std::vector<uint8_t> Encode(std::initializer_list<uint8_t> bytes)
{
    return std::vector<uint8_t>(bytes);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

TEST_CASE("Cpu::Lifecycle::InitShutdown", "[cpu]")
{
    CHECK(Init());
    CHECK(GetContext().rip == 0);
    Shutdown();
}

TEST_CASE("Cpu::Lifecycle::Reset", "[cpu]")
{
    Init();
    SetRip(0xDEAD);
    Reset();
    CHECK(GetContext().rip == 0);
    Shutdown();
}

TEST_CASE("Cpu::Lifecycle::SetRip", "[cpu]")
{
    Init();
    SetRip(0x1234'5678ULL);
    CHECK(GetContext().rip == 0x1234'5678ULL);
    Shutdown();
}

TEST_CASE("Cpu::Lifecycle::SetRsp", "[cpu]")
{
    Init();
    SetRsp(0x7FFF'0000ULL);
    CHECK(GetContext().gpr_get(Reg::RSP) == 0x7FFF'0000ULL);
    Shutdown();
}

// ── Register names ─────────────────────────────────────────────────────────

TEST_CASE("Cpu::RegNames::AllNames", "[cpu]")
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

TEST_CASE("Cpu::StepResultName::AllValues", "[cpu]")
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

TEST_CASE("Cpu::Insn::NOP", "[cpu]")
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

TEST_CASE("Cpu::Insn::HLT", "[cpu]")
{
    Init();
    auto code = Encode({0xF4});  // HLT
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    auto r = Step();
    CHECK(r == StepResult::Halt);
    Shutdown();
}

// ── SYSCALL instruction ────────────────────────────────────────────────────

TEST_CASE("Cpu::Insn::SYSCALL", "[cpu]")
{
    Init();
    auto code = Encode({0x0F, 0x05});  // SYSCALL
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    auto r = Step();
    CHECK(r == StepResult::Syscall);
    Shutdown();
}

// ── INT 3 breakpoint ───────────────────────────────────────────────────────

TEST_CASE("Cpu::Insn::INT3", "[cpu]")
{
    Init();
    auto code = Encode({0xCD, 0x03});  // INT 3
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    auto r = Step();
    CHECK(r == StepResult::Breakpoint);
    Shutdown();
}

// ── XOR reg, reg (self-zero) ───────────────────────────────────────────────

TEST_CASE("Cpu::Insn::XOR_RegReg", "[cpu]")
{
    Init();
    GetContext().gpr_set(Reg::RAX, 0xDEADBEEF);
    auto code = Encode({0x48, 0x33, 0xC0});
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    auto r = Step();
    CHECK(r == StepResult::Ok);
    CHECK(GetContext().gpr_get(Reg::RAX) == 0);
    CHECK(GetContext().flag(Flags::ZF));
    Shutdown();
}

// ── JMP short ─────────────────────────────────────────────────────────────

TEST_CASE("Cpu::Insn::JMP_Short", "[cpu]")
{
    Init();
    auto code = Encode({0xEB, 0x02, 0x90, 0x90, 0xF4});
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().rip = base;
    auto r = Step();
    CHECK(r == StepResult::Ok);
    CHECK(GetContext().rip == base + 4);
    Shutdown();
}

// ── JE / JNE ──────────────────────────────────────────────────────────────

TEST_CASE("Cpu::Insn::JE_Taken", "[cpu]")
{
    Init();
    auto code = Encode({0x74, 0x02, 0x90, 0x90, 0xF4});
    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    GetContext().rip = base;
    GetContext().rflags |= Flags::ZF;
    Step();
    CHECK(GetContext().rip == base + 4);
    Shutdown();
}

TEST_CASE("Cpu::Insn::JE_NotTaken", "[cpu]")
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

TEST_CASE("Cpu::Breakpoints::AddRemove", "[cpu]")
{
    Init();
    uint32_t id = AddBreakpoint(0xABCD, "test");
    CHECK(id != 0);
    CHECK(IsBreakpoint(0xABCD));
    CHECK(RemoveBreakpoint(id));
    CHECK_FALSE(IsBreakpoint(0xABCD));
    Shutdown();
}

TEST_CASE("Cpu::Breakpoints::Clear", "[cpu]")
{
    Init();
    AddBreakpoint(0x1000, "a");
    AddBreakpoint(0x2000, "b");
    ClearBreakpoints();
    CHECK_FALSE(IsBreakpoint(0x1000));
    CHECK_FALSE(IsBreakpoint(0x2000));
    Shutdown();
}

TEST_CASE("Cpu::Breakpoints::HitOnStep", "[cpu]")
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

TEST_CASE("Cpu::Breakpoints::RemoveInvalid", "[cpu]")
{
    Init();
    CHECK_FALSE(RemoveBreakpoint(99999));
    Shutdown();
}

// ── Statistics ─────────────────────────────────────────────────────────────

TEST_CASE("Cpu::Stats::Reset", "[cpu]")
{
    Init();
    auto code = Encode({0x90});
    GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    Step();
    ResetStats();
    CHECK(GetStats().instructionsExecuted == 0);
    Shutdown();
}

TEST_CASE("Cpu::Stats::IncrementOnStep", "[cpu]")
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

TEST_CASE("Cpu::Decode::NOP", "[cpu]")
{
    Init();
    uint8_t buf[] = {0x90};
    auto d = Decode(buf, 1);
    REQUIRE(d.has_value());
    CHECK(d->opcode == 0x90);
    CHECK(d->mnemonic.find("nop") != std::string::npos);
    Shutdown();
}

TEST_CASE("Cpu::Decode::NullReturnsNullopt", "[cpu]")
{
    Init();
    auto d = Decode(nullptr, 0);
    CHECK_FALSE(d.has_value());
    Shutdown();
}

TEST_CASE("Cpu::Disassemble::ReturnsCount", "[cpu]")
{
    Init();
    auto code = Encode({0x90, 0x90, 0x90});
    auto dis = Disassemble(reinterpret_cast<uint64_t>(code.data()), 3);
    CHECK(dis.size() == 3);
    for (auto& d : dis) CHECK(d.opcode == 0x90);
    Shutdown();
}

// ── SetContext / GetContext ────────────────────────────────────────────────

TEST_CASE("Cpu::Context::SetAndGet", "[cpu]")
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

TEST_CASE("Cpu::Callback::InsnFired", "[cpu]")
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

TEST_CASE("Cpu::CallStack::EmptyOnInit", "[cpu]")
{
    Init();
    CHECK(GetCallStack().empty());
    Shutdown();
}

TEST_CASE("Cpu::CallStack::MaxDepthRespected", "[cpu]")
{
    Init();
    auto cs = GetCallStack(0);
    CHECK(cs.empty());
    Shutdown();
}
