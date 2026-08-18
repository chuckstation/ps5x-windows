// ChuckStation5 – Integration tests
// Simulates a minimal homebrew execution: init → load → run → syscall → exit
// No real ELF is loaded — we synthesize a tiny x86-64 program in memory.
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Cpu/Cpu.h"
#include "ChuckStation5/Syscalls/Syscalls.h"
#include "ChuckStation5/CommandProcessor/CommandProcessor.h"
#include "ChuckStation5/RuntimeEvents/RuntimeEvents.h"
#include "ChuckStation5/GPU/GPU.h"

#include <array>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>

using namespace ChuckStation5;

// ── Helper: build a tiny "hello world" x86-64 program ────────────────────
static std::vector<uint8_t> MakeExitProgram(int exitCode = 0)
{
    std::vector<uint8_t> code;
    code.reserve(16);
    // mov rax, 60
    code.push_back(0x48);
    code.push_back(0xC7);
    code.push_back(0xC0);
    code.push_back(60);
    code.push_back(0);
    code.push_back(0);
    code.push_back(0);
    // mov rdi, exitCode
    code.push_back(0x48);
    code.push_back(0xC7);
    code.push_back(0xC7);
    code.push_back(static_cast<uint8_t>(exitCode));
    code.push_back(0);
    code.push_back(0);
    code.push_back(0);
    // SYSCALL
    code.push_back(0x0F);
    code.push_back(0x05);
    // HLT
    code.push_back(0xF4);
    return code;
}

// ── Helper: stepped execution loop with syscall handling ──────────────────
static Cpu::StepResult RunWithSyscalls(int maxSteps = 100)
{
    Cpu::StepResult last = Cpu::StepResult::Ok;
    for (int i = 0; i < maxSteps; ++i) {
        last = Cpu::Step();
        if (last == Cpu::StepResult::Syscall) {
            Syscalls::Dispatch(Cpu::GetContext());
            if (!Cpu::GetContextConst().gpr_get(Cpu::Reg::RAX) ||
                Cpu::GetContext().rip == 0) {
                last = Cpu::StepResult::Exit;
                break;
            }
            last = Cpu::StepResult::Ok;
            continue;
        }
        if (last != Cpu::StepResult::Ok) break;
    }
    return last;
}

// ── Tests ────────────────────────────────(uint64_t) ─────────────────

TEST_CASE("Integration::MinimalExitProgram", "[integration]")
{
    // Setup
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    // Override exit to just stop the CPU
    bool exitCalled = false;
    Syscalls::RegisterSyscall(60, "exit",
        [&](const Syscalls::SyscallArgs& a) -> int64_t {
            exitCalled = true;
            Cpu::Stop();
            return 0;
        }, 1);

    // Load program
    auto code = MakeExitProgram(0);
    Cpu::SetRip(reinterpret_cast<uint64_t>(code.data()));

    // Run
    RunWithSyscalls(20);

    CHECK(exitCalled);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

TEST_CASE("Integration::ExitCodePropagated", "[integration]")
{
    Cpu::Init();
    Syscalls::Init();

    int capturedCode = -1;
    Syscalls::RegisterSyscall(60, "exit",
        [&](const Syscalls::SyscallArgs& a) -> int64_t {
            capturedCode = static_cast<int>(a.arg0);
            Cpu::Stop();
            return 0;
        }, 1);

    auto code = MakeExitProgram(42);
    Cpu::SetRip(reinterpret_cast<uint64_t>(code.data()));
    RunWithSyscalls(20);

    CHECK(capturedCode == 42);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

TEST_CASE("Integration::SyscallStatsAfterRun", "[integration]")
{
    Cpu::Init();
    Syscalls::Init();
    Syscalls::ResetStats();
    Syscalls::RegisterBuiltins();

    bool stopped = false;
    Syscalls::RegisterSyscall(60, "exit",
        [&](const Syscalls::SyscallArgs&) -> int64_t {
            stopped = true; Cpu::Stop(); return 0;
        }, 1);

    auto code = MakeExitProgram(0);
    Cpu::SetRip(reinterpret_cast<uint64_t>(code.data()));
    RunWithSyscalls(20);

    auto stats = Syscalls::GetStats();
    CHECK(stats.total >= 1);
    CHECK(stats.known >= 1);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

TEST_CASE("Integration::CpuStatsAfterRun", "[integration]")
{
    Cpu::Init();
    Cpu::ResetStats();
    Syscalls::Init();

    Syscalls::RegisterSyscall(60, "exit",
        [](const Syscalls::SyscallArgs&) -> int64_t {
            Cpu::Stop(); return 0;
        }, 1);

    auto code = MakeExitProgram(0);
    Cpu::SetRip(reinterpret_cast<uint64_t>(code.data()));
    RunWithSyscalls(20);

    auto stats = Cpu::GetStats();
    // Should have executed at least 2 MOV instructions before SYSCALL
    CHECK(stats.instructionsExecuted >= 2);
    CHECK(stats.syscallsDispatched   >= 1);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

TEST_CASE("Integration::NopSlide", "[integration]")
{
    Cpu::Init();
    Cpu::ResetStats();

    // 10 NOPs then HLT
    std::vector<uint8_t> code(10, 0x90);
    code.push_back(0xF4);

    Cpu::SetRip(reinterpret_cast<uint64_t>(code.data()));

    Cpu::StepResult r = Cpu::StepResult::Ok;
    for (int i = 0; i < 12; ++i) {
        r = Cpu::Step();
        if (r != Cpu::StepResult::Ok) break;
    }
    CHECK(r == Cpu::StepResult::Halt);
    CHECK(Cpu::GetStats().instructionsExecuted >= 10);

    Cpu::Shutdown();
}

TEST_CASE("Integration::BreakpointInterruptsRun", "[integration]")
{
    Cpu::Init();
    Syscalls::Init();

    // 5 NOPs then SYSCALL then HLT
    std::vector<uint8_t> code = {0x90,0x90,0x90,0x90,0x90, 0x0F,0x05, 0xF4};
    uint64_t base = reinterpret_cast<uint64_t>(code.data());

    // Set breakpoint at 3rd NOP
    Cpu::AddBreakpoint(base + 2, "mid_nop");
    Cpu::SetRip(base);

    int steps = 0;
    Cpu::StepResult r = Cpu::StepResult::Ok;
    while (r == Cpu::StepResult::Ok && steps < 20) {
        r = Cpu::Step();
        ++steps;
    }
    CHECK(r == Cpu::StepResult::Breakpoint);
    CHECK(steps == 3);   // NOP, NOP, then BP on 3rd

    Cpu::Shutdown();
    Syscalls::Shutdown();
}

TEST_CASE("Integration::CommandListAndGpu", "[integration]")
{
    CommandProcessor::Init(nullptr);
    CommandProcessor::ResetStats();

    // Build a typical frame command list
    CommandProcessor::CommandList cl;
    cl.BeginRenderPass("frame");
    cl.ClearColor(0, 0.f, 0.f, 0.f, 1.f);
    cl.ClearDepth(1.f);
    CommandProcessor::Viewport vp;
    vp.width  = 1920.f;
    vp.height = 1080.f;
    cl.SetViewport(vp);
    cl.DrawDirect(3, 1);
    cl.EndRenderPass();
    cl.End();

    int32_t n = CommandProcessor::Process(cl);
    CHECK(n >= 5);

    auto s = CommandProcessor::GetStats();
    CHECK(s.drawCalls        >= 1);
    CHECK(s.renderPassBegins >= 1);
    CHECK(s.totalProcessMs   >= 0.0);

    CommandProcessor::Shutdown();
}

TEST_CASE("Integration::RuntimeEventsFromSyscall", "[integration]")
{
    RuntimeEvents::Init();
    RuntimeEvents::Reset();

    Cpu::Init();
    Syscalls::Init();

    int eventCount = 0;
    auto subId = RuntimeEvents::Subscribe([&](const RuntimeEvents::RuntimeEvent& ev){
        if (ev.type == RuntimeEvents::EventType::Custom) ++eventCount;
    });

    Syscalls::RegisterSyscall(39, "getpid",
        [](const Syscalls::SyscallArgs&) -> int64_t { return 1; });

    auto ctx = Cpu::CpuContext{};
    ctx.gpr_set(Cpu::Reg::RAX, 39);
    Cpu::SetContext(ctx);
    Syscalls::Dispatch(Cpu::GetContext());

    // Give event bus a moment to deliver
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(eventCount >= 1);

    RuntimeEvents::Unsubscribe(subId);
    Syscalls::Shutdown();
    Cpu::Shutdown();
    RuntimeEvents::Shutdown();
}

TEST_CASE("Integration::CallStackTracking", "[integration]")
{
    Cpu::Init();

    std::vector<uint8_t> code = {
        0xE8, 0x03, 0x00, 0x00, 0x00,   // CALL rel32 = +3 → lands at offset 8
        0xF4,                             // HLT (after return)
        0x90, 0x90,                       // padding
        0x90,                             // NOP in callee
        0xC3                              // RET
    };

    // Allocate a small stack buffer for CALL/RET operations
    std::array<uint8_t, 256> stackBuf{};
    uint64_t stackTop = reinterpret_cast<uint64_t>(stackBuf.data()) + 240;
    Cpu::SetRsp(stackTop);

    uint64_t base = reinterpret_cast<uint64_t>(code.data());
    Cpu::SetRip(base);

    // Execute CALL
    auto r = Cpu::Step();
    CHECK(r == Cpu::StepResult::Ok);
    CHECK(Cpu::GetContextConst().rip == base + 8);

    // Call stack should have one frame
    auto cs = Cpu::GetCallStack();
    CHECK(cs.size() == 1);
    CHECK(cs[0].returnAddr == base + 5);  // return address = after CALL

    // Execute NOP inside callee
    Cpu::Step();
    CHECK(Cpu::GetStats().instructionsExecuted >= 2);

    // Execute RET
    r = Cpu::Step();
    CHECK(r == Cpu::StepResult::Ok);
    CHECK(Cpu::GetContextConst().rip == base + 5);  // back to HLT

    // Call stack should be empty again
    CHECK(Cpu::GetCallStack().empty());

    // Execute HLT
    r = Cpu::Step();
    CHECK(r == Cpu::StepResult::Halt);

    Cpu::Shutdown();
}
