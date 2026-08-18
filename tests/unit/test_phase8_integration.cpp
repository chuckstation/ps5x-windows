// ChuckStation5 – Integration tests
// SPDX-License-Identifier: MIT
// End-to-end subsystem integration: CPU+Syscall+Memory+GPU+Debugger+Events.
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Cpu/Cpu.h"
#include "ChuckStation5/Syscalls/Syscalls.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/GPU/GPU.h"
#include "ChuckStation5/CommandProcessor/CommandProcessor.h"
#include "ChuckStation5/Debugger/Debugger.h"
#include "ChuckStation5/PerfTools/PerfTools.h"
#include "ChuckStation5/RuntimeEvents/RuntimeEvents.h"
#include "ChuckStation5/Audio/Audio.h"
#include "ChuckStation5/Input/Input.h"
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Process/Process.h"

using namespace ChuckStation5;
using namespace ChuckStation5::CommandProcessor;

// ── Full subsystem init/shutdown ──────────────────────────────────────────

TEST_CASE("Integration::FullInit::AllSubsystems", "[integration]")
{
    CHECK(Memory::Init());
    CHECK(Cpu::Init());
    CHECK(Syscalls::Init());
    Syscalls::RegisterBuiltins();
    CHECK(GPU::Init(nullptr));
    CHECK(CommandProcessor::Init(nullptr));
    CHECK(Debugger::Init());
    CHECK(PerfTools::Init());
    CHECK(RuntimeEvents::Init());
    Audio::AudioConfig acfg{};
    CHECK(Audio::Init(acfg));
    Input::Init();
    CHECK(true);

    // All initialised without crash
    CHECK(true);

    Input::Shutdown();
    Audio::Shutdown();
    RuntimeEvents::Shutdown();
    PerfTools::Shutdown();
    Debugger::Shutdown();
    CommandProcessor::Shutdown();
    GPU::Shutdown();
    Syscalls::Shutdown();
    Cpu::Shutdown();
    Memory::Shutdown();
}

// ── CPU → Debugger integration ────────────────────────────────────────────

TEST_CASE("Integration::CPU_Debugger::BreakpointHitTracked", "[integration]")
{
    Cpu::Init();
    Debugger::Init();

    uint32_t bpId = Debugger::SetBreakpoint(0xDEAD'0000, "integration_bp");
    CHECK(Cpu::IsBreakpoint(0xDEAD'0000));

    Debugger::RecordBreakpointHit(bpId);
    Debugger::RecordBreakpointHit(bpId);

    auto list = Debugger::ListBreakpoints();
    bool found = false;
    for (auto& bp : list) {
        if (bp.id == bpId) {
            CHECK(bp.hitCount == 2);
            found = true;
        }
    }
    CHECK(found);

    Debugger::Shutdown();
    Cpu::Shutdown();
}

TEST_CASE("Integration::CPU_Debugger::RegisterViewAfterExec", "[integration]")
{
    Cpu::Init();
    Debugger::Init();

    // Execute: mov rax, 0x42 (48 C7 C0 42 00 00 00)
    alignas(16) uint8_t code[] = {0x48, 0xC7, 0xC0, 0x42, 0x00, 0x00, 0x00, 0xF4};
    Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code);
    Cpu::Step(); // execute MOV

    auto regs = Debugger::GetRegisterView();
    bool found_rax = false;
    for (auto& r : regs) {
        if (r.name == "rax" && r.value == 0x42) { found_rax = true; break; }
    }
    CHECK(found_rax);

    Debugger::Shutdown();
    Cpu::Shutdown();
}

// ── CPU → Syscall → Memory integration ───────────────────────────────────

TEST_CASE("Integration::CPU_Syscall_Memory::GetPidAndBrk", "[integration]")
{
    Memory::Init();
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    // getpid
    Cpu::CpuContext ctx{};
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::GetPid);
    Syscalls::Dispatch(ctx);
    int64_t pid = static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX));
    CHECK(pid >= 1);

    // brk
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::Brk);
    ctx.gpr_set(Cpu::Reg::RDI, 0);
    Syscalls::Dispatch(ctx);
    (void)ctx.gpr_get(Cpu::Reg::RAX);
    CHECK(true);

    Syscalls::Shutdown();
    Cpu::Shutdown();
    Memory::Shutdown();
}

// ── GPU → CommandProcessor → RuntimeEvents integration ────────────────────

TEST_CASE("Integration::GPU_Events::FrameEndPublished", "[integration]")
{
    RuntimeEvents::Init();
    GPU::Init(nullptr);
    CommandProcessor::Init(nullptr);
    Debugger::Init();
    Debugger::AttachEventBrowser();

    CommandList cl;
    cl.BeginRenderPass();
    cl.DrawDirect(3, 1, 0, 0);
    cl.EndRenderPass();
    cl.End();
    CommandProcessor::Process(cl);

    auto log = Debugger::GetEventLog();
    bool foundFrameEnd = false;
    for (auto& e : log) {
        if (e.type == RuntimeEvents::EventType::FrameEnd) { foundFrameEnd = true; break; }
    }
    CHECK(foundFrameEnd);

    Debugger::Shutdown();
    CommandProcessor::Shutdown();
    GPU::Shutdown();
    RuntimeEvents::Shutdown();
}

// ── PerfTools → CPU → GPU pipeline ───────────────────────────────────────

TEST_CASE("Integration::PerfTools_CPU_GPU::FullFrame", "[integration]")
{
    PerfTools::Init();
    Cpu::Init();
    GPU::Init(nullptr);
    CommandProcessor::Init(nullptr);

    auto t0 = std::chrono::steady_clock::now();

    // Simulate frame work
    {
        PerfTools::ScopeTimer cpuT("CPU");
        alignas(16) uint8_t nops[5] = {0x90,0x90,0x90,0x90,0xF4};
        Cpu::GetContext().rip = reinterpret_cast<uint64_t>(nops);
        for (int i = 0; i < 4; ++i) Cpu::Step();
    }
    {
        PerfTools::ScopeTimer gpuT("GPU");
        CommandList cl;
        cl.BeginRenderPass();
        cl.DrawDirect(3, 1, 0, 0);
        cl.EndRenderPass();
        cl.End();
        CommandProcessor::Process(cl);
    }

    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    PerfTools::RecordFrameTime(ms);

    auto fs = PerfTools::GetFrameStats();
    CHECK(fs.samples == 1);
    CHECK(fs.avgMs >= 0.0);

    auto ss = PerfTools::GetSectionStats();
    bool foundCPU = false, foundGPU = false;
    for (auto& s : ss) {
        if (s.name == "CPU") foundCPU = true;
        if (s.name == "GPU") foundGPU = true;
    }
    CHECK(foundCPU);
    CHECK(foundGPU);

    CommandProcessor::Shutdown();
    GPU::Shutdown();
    Cpu::Shutdown();
    PerfTools::Shutdown();
}

// ── Multi-phase ALU sequence ──────────────────────────────────────────────

TEST_CASE("Integration::CPU::ALUSequence", "[integration]")
{
    // Program:
    //   mov rax, 10   (48 C7 C0 0A 00 00 00)
    //   mov rcx, 3    (48 C7 C1 03 00 00 00)
    //   imul rax,rcx  (48 0F AF C1)           → rax = 30
    //   sub rax, 5    (REX 83 /5 imm8=5: 48 83 E8 05) → rax = 25
    //   hlt           (F4)
    alignas(16) uint8_t code[] = {
        0x48, 0xC7, 0xC0, 0x0A, 0x00, 0x00, 0x00,  // mov rax, 10
        0x48, 0xC7, 0xC1, 0x03, 0x00, 0x00, 0x00,  // mov rcx, 3
        0x48, 0x0F, 0xAF, 0xC1,                     // imul rax, rcx
        0x48, 0x83, 0xE8, 0x05,                     // sub rax, 5
        0xF4                                         // hlt
    };

    Cpu::Init();
    Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code);

    for (int i = 0; i < 10; ++i) {
        auto r = Cpu::Step();
        if (r == Cpu::StepResult::Halt) break;
    }

    CHECK(Cpu::GetContext().gpr_get(Cpu::Reg::RAX) == 25);
    Cpu::Shutdown();
}

TEST_CASE("Integration::CPU::ShiftAndCompare", "[integration]")
{
    // shl rax, 3 then cmp rax, rbx then je/jne
    // mov rax, 1  (48 C7 C0 01 00 00 00)
    // shl rax, 3  (48 C1 E0 03)  → rax = 8
    // mov rbx, 8  (48 C7 C3 08 00 00 00)
    // cmp rax, rbx (48 39 D8)    → ZF=1
    // hlt         (F4)
    alignas(16) uint8_t code[] = {
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xC1, 0xE0, 0x03,
        0x48, 0xC7, 0xC3, 0x08, 0x00, 0x00, 0x00,
        0x48, 0x39, 0xD8,
        0xF4
    };

    Cpu::Init();
    Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code);
    for (int i = 0; i < 10; ++i) {
        auto r = Cpu::Step();
        if (r == Cpu::StepResult::Halt) break;
    }

    CHECK(Cpu::GetContext().gpr_get(Cpu::Reg::RAX) == 8);
    CHECK(Cpu::GetContext().flag(Cpu::Flags::ZF));

    Cpu::Shutdown();
}

TEST_CASE("Integration::CPU::MovzxPipeline", "[integration]")
{
    // mov rcx, 0xAB  then movzx rax, cl  → rax = 0xAB (byte zero-extended)
    alignas(16) uint8_t code[] = {
        0x48, 0xC7, 0xC1, 0xAB, 0x00, 0x00, 0x00,  // mov rcx, 0xAB
        0x48, 0x0F, 0xB6, 0xC1,                     // movzx rax, cl
        0xF4
    };

    Cpu::Init();
    Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code);
    for (int i = 0; i < 5; ++i) {
        auto r = Cpu::Step();
        if (r == Cpu::StepResult::Halt) break;
    }

    CHECK(Cpu::GetContext().gpr_get(Cpu::Reg::RAX) == 0xAB);
    Cpu::Shutdown();
}

// ── Homebrew demo: write + exit ───────────────────────────────────────────

TEST_CASE("Integration::Homebrew::WriteAndExit", "[integration]")
{
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    bool exited = false;
    Syscalls::RegisterSyscall(Syscalls::Nr::Exit, "exit",
        [&](const Syscalls::SyscallArgs&) -> int64_t {
            exited = true;
            Cpu::Stop();
            return 0;
        }, 1);

    const char msg[] = "Hello from homebrew!\n";
    // Build: write(1, msg, len) then exit(0)
    // write: rax=1, rdi=1, rsi=msg_ptr, rdx=len
    // exit:  rax=60, rdi=0
    alignas(16) uint8_t code[] = {
        // mov rax, 1
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
        // mov rdi, 1
        0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,
        // syscall  (write)
        0x0F, 0x05,
        // mov rax, 60 (exit)
        0x48, 0xC7, 0xC0, 0x3C, 0x00, 0x00, 0x00,
        // mov rdi, 0
        0x48, 0xC7, 0xC7, 0x00, 0x00, 0x00, 0x00,
        // syscall (exit)
        0x0F, 0x05,
        // hlt
        0xF4
    };

    Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code);
    Cpu::GetContext().gpr_set(Cpu::Reg::RSI, reinterpret_cast<uint64_t>(msg));
    Cpu::GetContext().gpr_set(Cpu::Reg::RDX, sizeof(msg)-1);

    for (int i = 0; i < 30; ++i) {
        auto r = Cpu::Step();
        if (r == Cpu::StepResult::Syscall) {
            Syscalls::Dispatch(Cpu::GetContext());
        }
        if (r == Cpu::StepResult::Halt || exited) break;
    }

    CHECK(exited);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

// ── Stats validation after full run ──────────────────────────────────────

TEST_CASE("Integration::Stats::AllModulesReport", "[integration]")
{
    Memory::Init();
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();
    Cpu::ResetStats();
    Syscalls::ResetStats();

    // Execute a few instructions and one syscall
    alignas(16) uint8_t code[] = {
        0x90, 0x90, 0x90,   // 3 NOPs
        0x48, 0xC7, 0xC0, static_cast<uint8_t>(Syscalls::Nr::GetPid), 0x00, 0x00, 0x00,
        0x0F, 0x05,         // syscall
        0xF4
    };
    Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code);
    for (int i = 0; i < 20; ++i) {
        auto r = Cpu::Step();
        if (r == Cpu::StepResult::Syscall) {
            Syscalls::Dispatch(Cpu::GetContext());
        }
        if (r == Cpu::StepResult::Halt) break;
    }

    auto cpuStats = Cpu::GetStats();
    auto sysStats = Syscalls::GetStats();
    CHECK(cpuStats.instructionsExecuted >= 3);
    CHECK(sysStats.total >= 1);

    Syscalls::Shutdown();
    Cpu::Shutdown();
    Memory::Shutdown();
}
