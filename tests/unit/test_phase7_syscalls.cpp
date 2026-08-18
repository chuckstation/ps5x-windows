// ChuckStation5 – Phase 7 Syscall Dispatcher tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Syscalls/Syscalls.h"
#include "ChuckStation5/Cpu/Cpu.h"

using namespace ChuckStation5::Syscalls;
namespace Cpu = ChuckStation5::Cpu;

// ── Helpers ───────────────────────────────────────────────────────────────

static Cpu::CpuContext MakeCtx(uint64_t nr,
                                uint64_t a0=0, uint64_t a1=0, uint64_t a2=0)
{
    Cpu::CpuContext ctx;
    ctx.gpr_set(Cpu::Reg::RAX, nr);
    ctx.gpr_set(Cpu::Reg::RDI, a0);
    ctx.gpr_set(Cpu::Reg::RSI, a1);
    ctx.gpr_set(Cpu::Reg::RDX, a2);
    return ctx;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Syscall::Lifecycle::InitShutdown", "[syscalls][phase7]")
{
    CHECK(Init());
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Lifecycle::DoubleInit", "[syscalls][phase7]")
{
    CHECK(Init());
    CHECK(Init());   // idempotent
    Shutdown();
}

// ── Registration ──────────────────────────────────────────────────────────

TEST_CASE("Phase7::Syscall::Register::BasicLookup", "[syscalls][phase7]")
{
    Init();
    bool called = false;
    RegisterSyscall(9999, "test_syscall",
        [&](const SyscallArgs&) -> int64_t { called = true; return 42; }, 0);
    auto desc = Lookup(9999);
    REQUIRE(desc.has_value());
    CHECK(desc->name   == "test_syscall");
    CHECK(desc->number == 9999);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Register::LookupMissing", "[syscalls][phase7]")
{
    Init();
    CHECK_FALSE(Lookup(0xDEAD).has_value());
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Register::Overwrite", "[syscalls][phase7]")
{
    Init();
    RegisterSyscall(7777, "first",  [](const SyscallArgs&) -> int64_t { return 1; });
    RegisterSyscall(7777, "second", [](const SyscallArgs&) -> int64_t { return 2; });
    auto d = Lookup(7777);
    REQUIRE(d.has_value());
    CHECK(d->name == "second");
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Register::SyscallDescForm", "[syscalls][phase7]")
{
    Init();
    SyscallDesc desc;
    desc.number   = 8888;
    desc.name     = "via_desc";
    desc.handler  = [](const SyscallArgs&) -> int64_t { return 0; };
    desc.argCount = 1;
    RegisterSyscall(desc);
    CHECK(Lookup(8888).has_value());
    Shutdown();
}

// ── SyscallName ───────────────────────────────────────────────────────────

TEST_CASE("Phase7::Syscall::Name::KnownReturnsName", "[syscalls][phase7]")
{
    Init();
    RegisterSyscall(1234, "my_named_call", [](const SyscallArgs&) -> int64_t { return 0; });
    CHECK(std::string(SyscallName(1234)) == "my_named_call");
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Name::UnknownReturnsUnknown", "[syscalls][phase7]")
{
    Init();
    std::string name = SyscallName(0xFFFF'FFFF);
    CHECK(name == "unknown");
    Shutdown();
}

// ── Dispatch ──────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Syscall::Dispatch::KnownHandler", "[syscalls][phase7]")
{
    Init();
    bool fired = false;
    RegisterSyscall(5555, "test_dispatch",
        [&](const SyscallArgs& a) -> int64_t { fired = true; return 99; });
    auto ctx = MakeCtx(5555);
    CHECK(Dispatch(ctx));
    CHECK(fired);
    CHECK(ctx.gpr_get(Cpu::Reg::RAX) == 99);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Dispatch::UnknownSyscallSafe", "[syscalls][phase7]")
{
    Init();
    auto ctx = MakeCtx(0xABCDABCD);
    // Must not crash; returns ENOSYS (-38) in RAX
    CHECK(Dispatch(ctx));
    int64_t result = static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX));
    CHECK(result < 0);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Dispatch::ArgsPassed", "[syscalls][phase7]")
{
    Init();
    uint64_t captured_a0 = 0, captured_a1 = 0;
    RegisterSyscall(6666, "arg_check",
        [&](const SyscallArgs& a) -> int64_t {
            captured_a0 = a.arg0;
            captured_a1 = a.arg1;
            return 0;
        }, 2);
    auto ctx = MakeCtx(6666, 0xAA, 0xBB);
    Dispatch(ctx);
    CHECK(captured_a0 == 0xAA);
    CHECK(captured_a1 == 0xBB);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Dispatch::ResultInRAX", "[syscalls][phase7]")
{
    Init();
    RegisterSyscall(3333, "ret_val",
        [](const SyscallArgs&) -> int64_t { return 1234; });
    auto ctx = MakeCtx(3333);
    Dispatch(ctx);
    CHECK(ctx.gpr_get(Cpu::Reg::RAX) == 1234);
    Shutdown();
}

// ── Built-ins ─────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Syscall::Builtins::GetPid", "[syscalls][phase7]")
{
    Init();
    RegisterBuiltins();
    auto ctx = MakeCtx(Nr::GetPid);
    CHECK(Dispatch(ctx));
    int64_t pid = static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX));
    CHECK(pid > 0);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Builtins::GetTid", "[syscalls][phase7]")
{
    Init();
    RegisterBuiltins();
    auto ctx = MakeCtx(Nr::GetTid);
    CHECK(Dispatch(ctx));
    CHECK(static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX)) > 0);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Builtins::SchedYield", "[syscalls][phase7]")
{
    Init();
    RegisterBuiltins();
    auto ctx = MakeCtx(Nr::Sched_yield);
    CHECK(Dispatch(ctx));
    CHECK(ctx.gpr_get(Cpu::Reg::RAX) == 0);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Builtins::Brk", "[syscalls][phase7]")
{
    Init();
    RegisterBuiltins();
    auto ctx = MakeCtx(Nr::Brk, 0x8000);
    CHECK(Dispatch(ctx));
    CHECK(ctx.gpr_get(Cpu::Reg::RAX) == 0x8000);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Builtins::Munmap", "[syscalls][phase7]")
{
    Init();
    RegisterBuiltins();
    auto ctx = MakeCtx(Nr::Munmap, 0x1000, 4096);
    CHECK(Dispatch(ctx));
    CHECK(ctx.gpr_get(Cpu::Reg::RAX) == 0);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Builtins::ClockGettime", "[syscalls][phase7]")
{
    Init();
    RegisterBuiltins();
    // timespec: 16 bytes on stack dummy — pass nullptr (arg1=0)
    auto ctx = MakeCtx(Nr::ClockGettime, 0, 0);
    CHECK(Dispatch(ctx));
    // With null buf it should still return 0
    CHECK(ctx.gpr_get(Cpu::Reg::RAX) == 0);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Builtins::WriteToInvalidFd", "[syscalls][phase7]")
{
    Init();
    RegisterBuiltins();
    // fd=99 — not stdout/stderr
    auto ctx = MakeCtx(Nr::Write, 99, 0, 0);
    Dispatch(ctx);
    int64_t r = static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX));
    CHECK(r < 0);  // EBADF
    Shutdown();
}

// ── ExtractArgs ───────────────────────────────────────────────────────────

TEST_CASE("Phase7::Syscall::ExtractArgs::Correct", "[syscalls][phase7]")
{
    Cpu::CpuContext ctx;
    ctx.gpr_set(Cpu::Reg::RAX, 99);
    ctx.gpr_set(Cpu::Reg::RDI, 1);
    ctx.gpr_set(Cpu::Reg::RSI, 2);
    ctx.gpr_set(Cpu::Reg::RDX, 3);
    ctx.gpr_set(Cpu::Reg::R10, 4);
    ctx.gpr_set(Cpu::Reg::R8,  5);
    ctx.gpr_set(Cpu::Reg::R9,  6);
    auto args = ExtractArgs(ctx);
    CHECK(args.number == 99);
    CHECK(args.arg0   == 1);
    CHECK(args.arg1   == 2);
    CHECK(args.arg2   == 3);
    CHECK(args.arg3   == 4);
    CHECK(args.arg4   == 5);
    CHECK(args.arg5   == 6);
}

// ── Statistics ────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Syscall::Stats::TotalCounted", "[syscalls][phase7]")
{
    Init();
    ResetStats();
    RegisterSyscall(1111, "s", [](const SyscallArgs&) -> int64_t { return 0; });
    // Dispatch modifies ctx.RAX with the return value, so use fresh ctx each time
    { auto c = MakeCtx(1111); Dispatch(c); }
    { auto c = MakeCtx(1111); Dispatch(c); }
    { auto c = MakeCtx(1111); Dispatch(c); }
    auto s = GetStats();
    CHECK(s.total >= 3);
    CHECK(s.known >= 3);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Stats::UnknownCounted", "[syscalls][phase7]")
{
    Init();
    ResetStats();
    auto ctx = MakeCtx(0xFEFEFEFE);
    Dispatch(ctx);
    auto s = GetStats();
    CHECK(s.unknown >= 1);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Stats::ErrorCounted", "[syscalls][phase7]")
{
    Init();
    ResetStats();
    RegisterSyscall(2222, "fail", [](const SyscallArgs&) -> int64_t { return -1; });
    auto ctx = MakeCtx(2222);
    Dispatch(ctx);
    CHECK(GetStats().errors >= 1);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Stats::Reset", "[syscalls][phase7]")
{
    Init();
    auto ctx = MakeCtx(0x9999);
    Dispatch(ctx);
    ResetStats();
    CHECK(GetStats().total == 0);
    Shutdown();
}

// ── Recent log ────────────────────────────────────────────────────────────

TEST_CASE("Phase7::Syscall::Log::RecordedAfterDispatch", "[syscalls][phase7]")
{
    Init();
    RegisterSyscall(4444, "logged_call",
        [](const SyscallArgs&) -> int64_t { return 77; });
    auto ctx = MakeCtx(4444, 0xAA);
    Dispatch(ctx);
    auto log = GetRecentLog(10);
    REQUIRE(!log.empty());
    CHECK(log.back().number == 4444);
    CHECK(log.back().result == 77);
    CHECK(log.back().args.arg0 == 0xAA);
    CHECK(log.back().timestampUs > 0);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Log::MaxEntriesRespected", "[syscalls][phase7]")
{
    Init();
    RegisterSyscall(5000, "bulk", [](const SyscallArgs&) -> int64_t { return 0; });
    for (int i = 0; i < 20; ++i) {
        auto c = MakeCtx(5000);
        Dispatch(c);
    }
    auto log = GetRecentLog(5);
    CHECK(log.size() <= 5);
    Shutdown();
}

TEST_CASE("Phase7::Syscall::Log::NameRecorded", "[syscalls][phase7]")
{
    Init();
    RegisterSyscall(6000, "named_log",
        [](const SyscallArgs&) -> int64_t { return 0; });
    auto ctx = MakeCtx(6000);
    Dispatch(ctx);
    auto log = GetRecentLog();
    bool found = false;
    for (auto& r : log)
        if (r.name == "named_log") found = true;
    CHECK(found);
    Shutdown();
}
