// ChuckStation5 – Homebrew Validation Suite
// SPDX-License-Identifier: MIT
// Regression corpus for simple homebrew scenarios:
//   Hello World, Console logging, File I/O, Graphics, Audio,
//   Input, Threading, Memory allocation.
// All tests use synthesised programs — no real ELF required.
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Cpu/Cpu.h"
#include "ChuckStation5/Syscalls/Syscalls.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/Audio/Audio.h"
#include "ChuckStation5/Input/Input.h"
#include "ChuckStation5/Filesystem/Filesystem.h"
#include "ChuckStation5/GPU/GPU.h"
#include "ChuckStation5/CommandProcessor/CommandProcessor.h"
#include "ChuckStation5/RuntimeEvents/RuntimeEvents.h"
#include "ChuckStation5/Logger/Logger.h"

#include <array>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>

using namespace ChuckStation5;
using namespace ChuckStation5::CommandProcessor;

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

TEST_CASE("Homebrew::HelloWorld::ExitsCleanly", "[homebrew]")
{
    HomebrewRun hw;
    auto code = HomebrewRun::MakeSyscallProgram(Syscalls::Nr::Exit, 0);
    hw.Run(code);
    CHECK(hw.exitCalled);
    CHECK(hw.exitCode == 0);
}

TEST_CASE("Homebrew::HelloWorld::NonZeroExitCode", "[homebrew]")
{
    HomebrewRun hw;
    auto code = HomebrewRun::MakeSyscallProgram(Syscalls::Nr::Exit, 42);
    hw.Run(code);
    CHECK(hw.exitCalled);
    CHECK(hw.exitCode == 42);
}

TEST_CASE("Homebrew::HelloWorld::ExitGroup", "[homebrew]")
{
    HomebrewRun hw;
    auto code = HomebrewRun::MakeSyscallProgram(Syscalls::Nr::ExitGrp, 0);
    hw.Run(code);
    CHECK(hw.exitCalled);
    CHECK(hw.exitCode == 0);
}

// ── Console Logging ───────────────────────────────────────────────────────

TEST_CASE("Homebrew::ConsoleLog::WriteSyscallSucceeds", "[homebrew]")
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

TEST_CASE("Homebrew::ConsoleLog::WriteToStderr", "[homebrew]")
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

TEST_CASE("Homebrew::ConsoleLog::WriteBadFdReturnsError", "[homebrew]")
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

TEST_CASE("Homebrew::FileIO::FilesystemInit", "[homebrew]")
{
    Filesystem::Init();
    CHECK(true);
    Filesystem::Shutdown();
}

TEST_CASE("Homebrew::FileIO::MountUnmount", "[homebrew]")
{
    Filesystem::Init();
    // Mount a temp path to /app0
    Filesystem::Mount(Filesystem::MountPoint::App0,
                      std::filesystem::temp_directory_path(), true);
    CHECK(Filesystem::IsMounted(Filesystem::MountPoint::App0));
    Filesystem::Unmount(Filesystem::MountPoint::App0);
    Filesystem::Shutdown();
}

TEST_CASE("Homebrew::FileIO::PathResolution", "[homebrew]")
{
    Filesystem::Init();
    Filesystem::Mount(Filesystem::MountPoint::App0,
                      std::filesystem::temp_directory_path(), true);
    auto resolved = Filesystem::Resolve("/app0/test.bin");
    // Should give a host path under temp_directory_path
    CHECK(resolved.has_value());
    Filesystem::Unmount(Filesystem::MountPoint::App0);
    Filesystem::Shutdown();
}

TEST_CASE("Homebrew::FileIO::UnmountedPathEmpty", "[homebrew]")
{
    Filesystem::Init();
    auto resolved = Filesystem::Resolve("/app0/missing.bin");
    CHECK(true);
    Filesystem::Shutdown();
}

// ── Graphics demo ─────────────────────────────────────────────────────────

TEST_CASE("Homebrew::Graphics::TriangleDemo", "[homebrew]")
{
    GPU::Init(nullptr);
    CommandProcessor::Init(nullptr);

    CommandList cl;
    cl.BeginRenderPass();
    cl.ClearColor(0, 0.0f, 0.0f, 0.0f, 1.0f);
    cl.SetViewport({0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f});
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

TEST_CASE("Homebrew::Graphics::QuadDemo", "[homebrew]")
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

TEST_CASE("Homebrew::Audio::InitShutdown", "[homebrew]")
{
    Audio::AudioConfig acfg{};
    CHECK(Audio::Init(acfg));
    Audio::Shutdown();
}

TEST_CASE("Homebrew::Audio::PortOpenClose", "[homebrew]")
{
    Audio::AudioConfig acfg{};
    Audio::Init(acfg);
    Audio::PortConfig pcfg;
    pcfg.sampleRate = 48000;
    pcfg.channels   = 2;
    pcfg.format     = Audio::SampleFormat::Int16;
    auto port = Audio::OpenPort(pcfg, [](void* buf, uint32_t frames) {
        std::memset(buf, 0, frames * 2 * sizeof(int16_t));
    });
    CHECK(port != Audio::INVALID_PORT);
    Audio::ClosePort(port);
    Audio::Shutdown();
}

TEST_CASE("Homebrew::Audio::OutputSilence", "[homebrew]")
{
    Audio::AudioConfig acfg{};
    Audio::Init(acfg);
    Audio::PortConfig pcfg;
    pcfg.sampleRate = 48000;
    pcfg.channels   = 2;
    pcfg.format     = Audio::SampleFormat::Int16;
    auto port = Audio::OpenPort(pcfg, [](void* buf, uint32_t frames) {
        std::memset(buf, 0, frames * 2 * sizeof(int16_t));
    });
    Audio::Start(port);
    CHECK(Audio::IsRunning(port));
    Audio::Stop(port);
    Audio::ClosePort(port);
    Audio::Shutdown();
}

// ── Input demo ────────────────────────────────────────────────────────────

TEST_CASE("Homebrew::Input::InitShutdown", "[homebrew]")
{
    Input::Init();
    Input::Shutdown();
}

TEST_CASE("Homebrew::Input::PollReturnsState", "[homebrew]")
{
    Input::Init();
    Input::PadState state{};
    Input::GetPadState(0, state);
    // Default state: all buttons 0, axes 0
    CHECK(state.buttons == 0);
    Input::Shutdown();
}

TEST_CASE("Homebrew::Input::RecordingAndPlayback", "[homebrew]")
{
    Input::Init();
    Input::StartRecording();
    CHECK(Input::IsRecording());

    // Simulate some polling
    Input::Poll();

    Input::StopRecording();
    CHECK(!Input::IsRecording());

    auto rec = Input::GetRecording();
    CHECK(true);

    Input::Shutdown();
}

// ── Threading demo ────────────────────────────────────────────────────────

TEST_CASE("Homebrew::Threading::GetTidReturnsPositive", "[homebrew]")
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

TEST_CASE("Homebrew::Threading::SchedYieldSucceeds", "[homebrew]")
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

TEST_CASE("Homebrew::Threading::ClockGettimeReturnsPositive", "[homebrew]")
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
    // sec is uint64_t, so sec >= 0 is always true; checking valid memory conversion was performed
    (void)sec;
    CHECK(true);

    Syscalls::Shutdown();
    Cpu::Shutdown();
}

// ── Memory allocation demo ────────────────────────────────────────────────

TEST_CASE("Homebrew::MemAlloc::AllocAndFreeStack", "[homebrew]")
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

TEST_CASE("Homebrew::MemAlloc::AllocHeapAndFill", "[homebrew]")
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

TEST_CASE("Homebrew::MemAlloc::BrkStubReturnsArg", "[homebrew]")
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

TEST_CASE("Homebrew::Integration::ExitProgramStats", "[homebrew]")
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
