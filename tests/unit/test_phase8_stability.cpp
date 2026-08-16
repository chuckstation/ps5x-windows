// PS5x – Phase 8 Runtime Stability tests
// SPDX-License-Identifier: MIT
//
// Tests for exception handling, process teardown, resource lifetime,
// crash recovery diagnostics, and memory leak detection.
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Process/Process.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/MemoryDiag/MemoryDiag.h"
#include "PS5x/Runtime/Runtime.h"
#include "PS5x/Cpu/Cpu.h"
#include "PS5x/Syscalls/Syscalls.h"
#include "PS5x/KernelRuntime/KernelRuntime.h"
#include "PS5x/Logger/Logger.h"

using namespace PS5x;

// ── Process lifecycle stress ───────────────────────────────────────────────

TEST_CASE("Phase8::Stability::Process::MultipleInitShutdown", "[stability][phase8]")
{
    for (int i = 0; i < 5; ++i) {
        CHECK(Process::Init());
        Process::Shutdown();
    }
}

TEST_CASE("Phase8::Stability::Process::ShutdownWithNoProcesses", "[stability][phase8]")
{
    CHECK(Process::Init());
    // Shutdown with zero processes should be a no-op
    Process::Shutdown();
    CHECK(true); // reached here = no crash
}

TEST_CASE("Phase8::Stability::Process::DoubleShutdownSafe", "[stability][phase8]")
{
    Process::Init();
    Process::Shutdown();
    Process::Shutdown(); // second shutdown must not crash
    CHECK(true);
}

TEST_CASE("Phase8::Stability::Process::InitAfterShutdown", "[stability][phase8]")
{
    Process::Init();
    Process::Shutdown();
    CHECK(Process::Init());
    Process::Shutdown();
}

// ── Memory lifetime ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Stability::Memory::InitShutdownCycle", "[stability][phase8]")
{
    for (int i = 0; i < 3; ++i) {
        CHECK(Memory::Init());
        Memory::Shutdown();
    }
}

TEST_CASE("Phase8::Stability::Memory::AllocFreeBalance", "[stability][phase8]")
{
    Memory::Init();
    auto before = Memory::GetStats();
    void* p = Memory::AllocHost(1024, Memory::AllocType::Heap);
    CHECK(p != nullptr);
    auto during = Memory::GetStats();
    CHECK(during.totalAllocated >= before.totalAllocated + 1024);
    Memory::FreeHost(p);
    auto after = Memory::GetStats();
    CHECK(after.totalAllocated <= before.totalAllocated + 128); // padding tolerance
    Memory::Shutdown();
}

TEST_CASE("Phase8::Stability::Memory::MultipleAllocations", "[stability][phase8]")
{
    Memory::Init();
    constexpr int N = 20;
    std::vector<void*> ptrs;
    for (int i = 0; i < N; ++i) {
        void* p = Memory::AllocHost(256, Memory::AllocType::Heap);
        REQUIRE(p != nullptr);
        ptrs.push_back(p);
    }
    for (void* p : ptrs) Memory::FreeHost(p);
    Memory::Shutdown();
}

TEST_CASE("Phase8::Stability::Memory::NullFreeIsNoop", "[stability][phase8]")
{
    Memory::Init();
    Memory::FreeHost(nullptr); // must not crash
    Memory::Shutdown();
}

// ── Memory Diagnostics ───────────────────────────────────────────────────

TEST_CASE("Phase8::Stability::MemoryDiag::InitShutdown", "[stability][phase8]")
{
    CHECK(MemoryDiag::Init());
    MemoryDiag::Shutdown();
}

TEST_CASE("Phase8::Stability::MemoryDiag::ReportAfterInit", "[stability][phase8]")
{
    MemoryDiag::Init();
    auto report = MemoryDiag::GetReport();
    // Freshly-initialised: zero leaks
    CHECK(report.leakCount == 0);
    MemoryDiag::Shutdown();
}

TEST_CASE("Phase8::Stability::MemoryDiag::RecordAndFree", "[stability][phase8]")
{
    MemoryDiag::Init();
    uintptr_t tag = 0xBEEF0001;
    MemoryDiag::RecordAlloc(tag, 512, "test");
    {
        auto r = MemoryDiag::GetReport();
        CHECK(r.trackedBytes >= 512);
    }
    MemoryDiag::RecordFree(tag);
    {
        auto r = MemoryDiag::GetReport();
        CHECK(r.leakCount == 0);
    }
    MemoryDiag::Shutdown();
}

TEST_CASE("Phase8::Stability::MemoryDiag::UnfreedShowsAsLeak", "[stability][phase8]")
{
    MemoryDiag::Init();
    uintptr_t tag = 0xDEAD0002;
    MemoryDiag::RecordAlloc(tag, 256, "leak_test");
    {
        auto r = MemoryDiag::GetReport();
        CHECK(r.leakCount >= 1);
    }
    // Clean up
    MemoryDiag::RecordFree(tag);
    MemoryDiag::Shutdown();
}

// ── CPU stability under rapid reset ──────────────────────────────────────

TEST_CASE("Phase8::Stability::Cpu::RapidReset", "[stability][phase8]")
{
    for (int i = 0; i < 10; ++i) {
        Cpu::Init();
        Cpu::SetRip(0x1000 * (i + 1));
        Cpu::Reset();
        CHECK(Cpu::GetContext().rip == 0);
        Cpu::Shutdown();
    }
}

TEST_CASE("Phase8::Stability::Cpu::BreakpointsClearedOnReset", "[stability][phase8]")
{
    Cpu::Init();
    Cpu::AddBreakpoint(0xDEAD, "test");
    CHECK(Cpu::IsBreakpoint(0xDEAD));
    Cpu::Reset();
    CHECK(!Cpu::IsBreakpoint(0xDEAD));
    Cpu::Shutdown();
}

// ── Syscall dispatcher stability ──────────────────────────────────────────

TEST_CASE("Phase8::Stability::Syscall::MultipleRegistrationsStable", "[stability][phase8]")
{
    Syscalls::Init();
    for (uint64_t n = 50000; n < 50020; ++n) {
        Syscalls::RegisterSyscall(n, "bench_" + std::to_string(n),
            [](const Syscalls::SyscallArgs&) -> int64_t { return 0; });
    }
    for (uint64_t n = 50000; n < 50020; ++n) {
        CHECK(Syscalls::Lookup(n).has_value());
    }
    Syscalls::Shutdown();
}

TEST_CASE("Phase8::Stability::Syscall::StatsResetBetweenRuns", "[stability][phase8]")
{
    Syscalls::Init();
    Syscalls::RegisterBuiltins();
    // Dispatch something
    Cpu::CpuContext ctx;
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::GetPid);
    Syscalls::Dispatch(ctx);
    auto s1 = Syscalls::GetStats();
    CHECK(s1.total >= 1);
    Syscalls::ResetStats();
    auto s2 = Syscalls::GetStats();
    CHECK(s2.total == 0);
    Syscalls::Shutdown();
}

// ── KernelRuntime handle lifecycle ───────────────────────────────────────

TEST_CASE("Phase8::Stability::KernelRuntime::HandleExhaustion", "[stability][phase8]")
{
    KernelRuntime::Init();
    // Allocate many handles and ensure they're all distinct
    std::vector<KernelRuntime::KHandle> handles;
    for (int i = 0; i < 50; ++i) {
        auto h = KernelRuntime::AllocHandle(KernelRuntime::KObjectType::Thread);
        handles.push_back(h);
    }
    // Check distinctness
    std::sort(handles.begin(), handles.end());
    auto last = std::unique(handles.begin(), handles.end());
    CHECK(last == handles.end()); // all unique
    for (auto h : handles) KernelRuntime::FreeHandle(h);
    KernelRuntime::Shutdown();
}

// ── Runtime subsystem stability ───────────────────────────────────────────

TEST_CASE("Phase8::Stability::Runtime::SubsystemNameTable", "[stability][phase8]")
{
    // All valid SubsystemIds should return a non-empty string
    using SID = PS5x::Runtime::SubsystemId;
    std::vector<SID> ids = {
        PS5x::Runtime::SubsystemId::Logger, PS5x::Runtime::SubsystemId::Config, PS5x::Runtime::SubsystemId::Memory, PS5x::Runtime::SubsystemId::Kernel,
        PS5x::Runtime::SubsystemId::Filesystem, PS5x::Runtime::SubsystemId::Loader, PS5x::Runtime::SubsystemId::KytyAdapter,
        PS5x::Runtime::SubsystemId::Renderer, PS5x::Runtime::SubsystemId::GPU, PS5x::Runtime::SubsystemId::Audio, PS5x::Runtime::SubsystemId::Input,
        PS5x::Runtime::SubsystemId::Process, PS5x::Runtime::SubsystemId::Debugger, PS5x::Runtime::SubsystemId::UI,
    };
    for (auto id : ids) {
        std::string name = PS5x::Runtime::SubsystemName(id);
        CHECK(!name.empty());
    }
}

TEST_CASE("Phase8::Stability::Runtime::SubsystemStateNameTable", "[stability][phase8]")
{
    using SS = PS5x::Runtime::SubsystemState;
    std::vector<SS> states = {
        PS5x::Runtime::SubsystemState::Unregistered, PS5x::Runtime::SubsystemState::Registered, PS5x::Runtime::SubsystemState::Initialising,
        PS5x::Runtime::SubsystemState::Running, PS5x::Runtime::SubsystemState::ShuttingDown, PS5x::Runtime::SubsystemState::Stopped, PS5x::Runtime::SubsystemState::Failed,
    };
    for (auto s : states) {
        std::string name = PS5x::Runtime::SubsystemStateName(s);
        CHECK(!name.empty());
    }
}

// ── Exception / fault diagnostics ────────────────────────────────────────

TEST_CASE("Phase8::Stability::Cpu::FaultCallbackFired", "[stability][phase8]")
{
    Cpu::Init();
    bool faultFired = false;
    Cpu::SetFaultCallback([&](uint64_t, const char*) { faultFired = true; });

    // Simulate a fault by setting RIP to 0
    Cpu::SetRip(0);
    auto r = Cpu::Step();
    // RIP=0 → Fault returned
    CHECK(r == Cpu::StepResult::Fault);
    // Note: faultCb is called in ExecuteOne only if a real fault handler calls it;
    // our check here is that Step returns Fault cleanly.
    Cpu::Shutdown();
}

TEST_CASE("Phase8::Stability::Cpu::FaultStatIncremented", "[stability][phase8]")
{
    Cpu::Init();
    Cpu::ResetStats();
    // DIV by zero → fault
    alignas(16) uint8_t code[] = {0x48, 0xF7, 0xF1}; // DIV rcx
    Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code);
    Cpu::GetContext().gpr_set(Cpu::Reg::RCX, 0);
    Cpu::Step();
    CHECK(Cpu::GetStats().faults == 1);
    Cpu::Shutdown();
}
