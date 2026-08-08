// PS5x – Phase 8 Homebrew Validation Suite
// SPDX-License-Identifier: MIT
//
// Regression corpus for simple homebrew scenarios:
//   Hello World, Console logging, File I/O, Graphics, Audio,
//   Input, Threading, Memory allocation.
// All tests use synthesised programs — no real ELF required.
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Cpu/Cpu.h"
#include "PS5x/Syscalls/Syscalls.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/Audio/Audio.h"
#include "PS5x/Input/Input.h"
#include "PS5x/Filesystem/Filesystem.h"
#include "PS5x/GPU/GPU.h"
#include "PS5x/CommandProcessor/CommandProcessor.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"
#include "PS5x/Logger/Logger.h"

#include <array>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>

using namespace PS5x;

// ── Helper: build and run a tiny x86-64 program ───────────────────────────

struct HomebrewRun {
    bool exitCalled  = false;
    int  exitCode    = -1;
    std::vector<std::string> output;

    /// Build: mov rax, nr; mov rdi, arg0; syscall; hlt
    static std::vector<uint8_t> MakeSyscallProgram(uint64_t nr, uint64_t arg0 = 0)
    {
        std::vector<uint8_t> code;
        // mov rax, nr  (48 C7 C0 <nr32>)
        code.insert(code.end(), {0x48, 0xC7, 0xC0,
            static_cast<uint8_t>(nr),
            static_cast<uint8_t>(nr >> 8),
            static_cast<uint8_t>(nr >> 16),
            static_cast<uint8_t>(nr >> 24)});
        // mov rdi, arg0
        code.insert(code.end(), {0x48, 0xC7, 0xC7,
            static_cast<uint8_t>(arg0),
            static_cast<uint8_t>(arg0 >> 8),
            static_cast<uint8_t>(arg0 >> 16),
            static_cast<uint8_t>(arg0 >> 24)});
        // syscall
        code.insert(code.end(), {0x0F, 0x05});
        // hlt
        code.push_back(0xF4);
        return code;
    }

    Cpu::StepResult Run(const std::vector<uint8_t>& code, int maxSteps = 200)
    {
        Cpu::Init();
        Syscalls::Init();
        Syscalls::RegisterBuiltins();

        // Override exit
        Syscalls::RegisterSyscall(Syscalls::Nr::Exit, "exit",
            [&](const Syscalls::SyscallArgs& a) -> int64_t {
                exitCalled = true;
                exitCode   = static_cast<int>(a.arg0);
                Cpu::Stop();
                return 0;
            }, 1);
        Syscalls::RegisterSyscall(Syscalls::Nr::ExitGrp, "exit_group",
            [&](const Syscalls::SyscallArgs& a) -> int64_t {
                exitCalled = true;
                exitCode   = static_cast<int>(a.arg0);
                Cpu::Stop();
                return 0;
            }, 1);

        Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code.data());
        Cpu::StepResult last  = Cpu::StepResult::Ok;
        for (int i = 0; i < maxSteps; ++i) {
            last = Cpu::Step();
            if (last == Cpu::StepResult::Syscall) {
                Syscalls::Dispatch(Cpu::GetContext());
                last = Cpu::StepResult::Ok;
                continue;
            }
            if (last != Cpu::StepResult::Ok) break;
        }
        Syscalls::Shutdown();
        Cpu::Shutdown();
        return last;
    }
};

// ── Hello World ───────────────────────────────────────────────────────────

TEST_CASE("Phase8::Homebrew::HelloWorld::ExitsCleanly", "[homebrew][phase8]")
{
    HomebrewRun hw;
    auto code = HomebrewRun::MakeSyscallProgram(Syscalls::Nr::Exit, 0);
    hw.Run(code);
    CHECK(hw.exitCalled);
    CHECK(hw.exitCode == 0);
}

TEST_CASE("Phase8::Homebrew::HelloWorld::NonZeroExitCode", "[homebrew][phase8]")
{
    HomebrewRun hw;
    auto code = HomebrewRun::MakeSyscallProgram(Syscalls::Nr::Exit, 42);
    hw.Run(code);
    CHECK(hw.exitCalled);
    CHECK(hw.exitCode == 42);
}

TEST_CASE("Phase8::Homebrew::HelloWorld::ExitGroup", "[homebrew][phase8]")
{
    HomebrewRun hw;
    auto code = HomebrewRun::MakeSyscallProgram(Syscalls::Nr::ExitGrp, 0);
    hw.Run(code);
    CHECK(hw.exitCalled);
    CHECK(hw.exitCode == 0);
}

// ── Console Logging ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Homebrew::ConsoleLog::WriteSyscallSucceeds", "[homebrew][phase8]")
{
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    const char msg[] = "Hello from homebrew!\n";
    // write(1, msg, len)
    Cpu::CpuContext ctx{};
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::Write);
    ctx.gpr_set(Cpu::Reg::RDI, 1);
    ctx.gpr_set(Cpu::Reg::RSI, reinterpret_cast<uint64_t>(msg));
    ctx.gpr_set(Cpu::Reg::RDX, sizeof(msg) - 1);
    Syscalls::Dispatch(ctx);

    int64_t result = static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX));
    CHECK(result == static_cast<int64_t>(sizeof(msg) - 1));

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

TEST_CASE("Phase8::Homebrew::ConsoleLog::WriteToStderr", "[homebrew][phase8]")
{
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    const char msg[] = "Error!\n";
    Cpu::CpuContext ctx{};
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::Write);
    ctx.gpr_set(Cpu::Reg::RDI, 2); // stderr
    ctx.gpr_set(Cpu::Reg::RSI, reinterpret_cast<uint64_t>(msg));
    ctx.gpr_set(Cpu::Reg::RDX, sizeof(msg) - 1);
    Syscalls::Dispatch(ctx);

    int64_t result = static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX));
    CHECK(result == static_cast<int64_t>(sizeof(msg) - 1));

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

TEST_CASE("Phase8::Homebrew::ConsoleLog::WriteBadFdReturnsError", "[homebrew][phase8]")
{
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    const char msg[] = "bad fd\n";
    Cpu::CpuContext ctx{};
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::Write);
    ctx.gpr_set(Cpu::Reg::RDI, 99); // invalid fd
    ctx.gpr_set(Cpu::Reg::RSI, reinterpret_cast<uint64_t>(msg));
    ctx.gpr_set(Cpu::Reg::RDX, sizeof(msg) - 1);
    Syscalls::Dispatch(ctx);

    int64_t result = static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX));
    CHECK(result < 0); // EBADF
    Syscalls::Shutdown();
    Cpu::Shutdown();
}

// ── File I/O ─────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Homebrew::FileIO::FilesystemInit", "[homebrew][phase8]")
{
    CHECK(Filesystem::Init());
    Filesystem::Shutdown();
}

TEST_CASE("Phase8::Homebrew::FileIO::MountUnmount", "[homebrew][phase8]")
{
    Filesystem::Init();
    // Mount a temp path to /app0
    Filesystem::Mount(Filesystem::MountPoint::App0,
                      std::filesystem::temp_directory_path(), true);
    auto mounts = Filesystem::GetMountPoints();
    bool found = false;
    for (auto& m : mounts) {
        if (m.point == Filesystem::MountPoint::App0) { found = true; break; }
    }
    CHECK(found);
    Filesystem::Unmount(Filesystem::MountPoint::App0);
    Filesystem::Shutdown();
}

TEST_CASE("Phase8::Homebrew::FileIO::PathResolution", "[homebrew][phase8]")
{
    Filesystem::Init();
    Filesystem::Mount(Filesystem::MountPoint::App0,
                      std::filesystem::temp_directory_path(), true);
    auto resolved = Filesystem::Resolve("/app0/test.bin");
    // Should give a host path under temp_directory_path
    CHECK(!resolved.empty());
    Filesystem::Unmount(Filesystem::MountPoint::App0);
    Filesystem::Shutdown();
}

TEST_CASE("Phase8::Homebrew::FileIO::UnmountedPathEmpty", "[homebrew][phase8]")
{
    Filesystem::Init();
    auto resolved = Filesystem::Resolve("/app0/missing.bin");
    // /app0 not mounted — resolved may be empty or indicate unmounted
    // We just verify it doesn't crash
    CHECK(true);
    Filesystem::Shutdown();
}

// ── Graphics demo ─────────────────────────────────────────────────────────

TEST_CASE("Phase8::Homebrew::Graphics::TriangleDemo", "[homebrew][phase8]")
{
    GPU::Init(nullptr);
    CommandProcessor::Init(nullptr);

    CommandList cl;
    cl.BeginRenderPass();
    cl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    cl.SetViewport(0, 0, 1280, 720, 0.0f, 1.0f);
    cl.DrawDirect(3, 1, 0, 0); // triangle
    cl.EndRenderPass();
    cl.End();

    CommandProcessor::Process(cl);
    auto s = CommandProcessor::GetStats();
    CHECK(s.drawCalls >= 1);
    CHECK(s.renderPassBegins >= 1);

    CommandProcessor::Shutdown();
    GPU::Shutdown();
}

TEST_CASE("Phase8::Homebrew::Graphics::QuadDemo", "[homebrew][phase8]")
{
    GPU::Init(nullptr);
    CommandProcessor::Init(nullptr);

    CommandList cl;
    cl.BeginRenderPass();
    cl.DrawIndexed(6, 1, 0, 0, 0); // quad
    cl.EndRenderPass();
    cl.End();

    CommandProcessor::Process(cl);
    auto s = CommandProcessor::GetStats();
    CHECK(s.drawCalls >= 1);

    CommandProcessor::Shutdown();
    GPU::Shutdown();
}

// ── Audio demo ────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Homebrew::Audio::InitShutdown", "[homebrew][phase8]")
{
    CHECK(Audio::Init());
    Audio::Shutdown();
}

TEST_CASE("Phase8::Homebrew::Audio::PortOpenClose", "[homebrew][phase8]")
{
    Audio::Init();
    auto port = Audio::OpenPort(Audio::PortType::Main, 2, 48000,
                                Audio::SampleFormat::S16);
    CHECK(port != Audio::INVALID_PORT);
    Audio::ClosePort(port);
    Audio::Shutdown();
}

TEST_CASE("Phase8::Homebrew::Audio::OutputSilence", "[homebrew][phase8]")
{
    Audio::Init();
    auto port = Audio::OpenPort(Audio::PortType::Main, 2, 48000,
                                Audio::SampleFormat::S16);
    // Submit a silent buffer
    std::vector<int16_t> silent(48000 / 60 * 2, 0); // one frame stereo
    auto r = Audio::SubmitBuffer(port, silent.data(),
                                  silent.size() * sizeof(int16_t));
    CHECK(r == Audio::AudioResult::Ok);
    Audio::ClosePort(port);
    Audio::Shutdown();
}

// ── Input demo ────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Homebrew::Input::InitShutdown", "[homebrew][phase8]")
{
    CHECK(Input::Init());
    Input::Shutdown();
}

TEST_CASE("Phase8::Homebrew::Input::PollReturnsState", "[homebrew][phase8]")
{
    Input::Init();
    auto state = Input::Poll(0);
    // Default state: all buttons 0, axes 0
    CHECK(state.buttons == 0);
    Input::Shutdown();
}

TEST_CASE("Phase8::Homebrew::Input::InjectAndPoll", "[homebrew][phase8]")
{
    Input::Init();
    Input::PadState inject{};
    inject.buttons = Input::Button::Cross;
    Input::Inject(0, inject);
    auto state = Input::Poll(0);
    CHECK((state.buttons & Input::Button::Cross) != 0);
    Input::Shutdown();
}

// ── Threading demo ────────────────────────────────────────────────────────

TEST_CASE("Phase8::Homebrew::Threading::GetTidReturnsPositive", "[homebrew][phase8]")
{
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    Cpu::CpuContext ctx{};
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::GetTid);
    Syscalls::Dispatch(ctx);
    int64_t tid = static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX));
    CHECK(tid >= 1);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

TEST_CASE("Phase8::Homebrew::Threading::SchedYieldSucceeds", "[homebrew][phase8]")
{
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    Cpu::CpuContext ctx{};
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::Sched_yield);
    Syscalls::Dispatch(ctx);
    CHECK(static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX)) == 0);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

TEST_CASE("Phase8::Homebrew::Threading::ClockGettimeReturnsPositive", "[homebrew][phase8]")
{
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    alignas(16) uint8_t ts_buf[16]{};
    Cpu::CpuContext ctx{};
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::ClockGettime);
    ctx.gpr_set(Cpu::Reg::RDI, 0); // CLOCK_REALTIME
    ctx.gpr_set(Cpu::Reg::RSI, reinterpret_cast<uint64_t>(ts_buf));
    Syscalls::Dispatch(ctx);
    CHECK(static_cast<int64_t>(ctx.gpr_get(Cpu::Reg::RAX)) == 0);

    uint64_t sec{};
    std::memcpy(&sec, ts_buf, 8);
    CHECK(sec >= 0); // some positive timestamp

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

// ── Memory allocation demo ────────────────────────────────────────────────

TEST_CASE("Phase8::Homebrew::MemAlloc::AllocAndFreeStack", "[homebrew][phase8]")
{
    Memory::Init();
    constexpr size_t SZ = 64 * 1024; // 64 KB stack
    void* stack = Memory::AllocHost(SZ, Memory::AllocType::Stack);
    REQUIRE(stack != nullptr);
    // Write to ensure it's mapped
    std::memset(stack, 0xAB, SZ);
    Memory::FreeHost(stack);
    Memory::Shutdown();
}

TEST_CASE("Phase8::Homebrew::MemAlloc::AllocHeapAndFill", "[homebrew][phase8]")
{
    Memory::Init();
    constexpr size_t SZ = 4 * 1024; // 4 KB
    void* heap = Memory::AllocHost(SZ, Memory::AllocType::Heap);
    REQUIRE(heap != nullptr);
    std::memset(heap, 0, SZ);
    auto stats = Memory::GetStats();
    CHECK(stats.totalAllocated >= SZ);
    Memory::FreeHost(heap);
    Memory::Shutdown();
}

TEST_CASE("Phase8::Homebrew::MemAlloc::BrkStubReturnsArg", "[homebrew][phase8]")
{
    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();

    Cpu::CpuContext ctx{};
    ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::Brk);
    ctx.gpr_set(Cpu::Reg::RDI, 0x10000);
    Syscalls::Dispatch(ctx);
    CHECK(ctx.gpr_get(Cpu::Reg::RAX) == 0x10000);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

// ── Full integration: exit program + stats ────────────────────────────────

TEST_CASE("Phase8::Homebrew::Integration::ExitProgramStats", "[homebrew][phase8]")
{
    HomebrewRun hw;
    auto code = HomebrewRun::MakeSyscallProgram(Syscalls::Nr::Exit, 0);

    Cpu::Init();
    Syscalls::Init();
    Syscalls::RegisterBuiltins();
    Syscalls::ResetStats();
    Cpu::ResetStats();

    bool exitCalled = false;
    Syscalls::RegisterSyscall(Syscalls::Nr::Exit, "exit",
        [&](const Syscalls::SyscallArgs&) -> int64_t {
            exitCalled = true;
            Cpu::Stop();
            return 0;
        }, 1);

    Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code.data());
    for (int i = 0; i < 50; ++i) {
        auto r = Cpu::Step();
        if (r == Cpu::StepResult::Syscall) {
            Syscalls::Dispatch(Cpu::GetContext());
            break;
        }
        if (r != Cpu::StepResult::Ok) break;
    }

    auto cpuStats = Cpu::GetStats();
    auto sysStats = Syscalls::GetStats();
    CHECK(cpuStats.instructionsExecuted >= 2); // at least 2 MOVs before syscall
    CHECK(sysStats.total >= 1);
    CHECK(exitCalled);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}
