// PS5x – Debugger unit tests (Phase 2)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/Debugger/Debugger.h"

#include <filesystem>
#include <string>

static void Setup()
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Memory::Init();
    PS5x::Debugger::Init();
}
static void Teardown()
{
    PS5x::Debugger::Shutdown();
    PS5x::Memory::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Debugger – Init / Shutdown cycle", "[dbg]")
{
    Setup();
    REQUIRE(!PS5x::Debugger::IsPaused());
    Teardown();
}

TEST_CASE("Debugger – Pause / Continue", "[dbg]")
{
    Setup();
    PS5x::Debugger::Pause();
    REQUIRE(PS5x::Debugger::IsPaused());
    PS5x::Debugger::Continue();
    REQUIRE(!PS5x::Debugger::IsPaused());
    Teardown();
}

TEST_CASE("Debugger – AddBreakpoint / RemoveBreakpoint (soft)", "[dbg]")
{
    Setup();
    // Soft BP: address not yet mapped
    uint32_t id = PS5x::Debugger::AddBreakpoint(0xDEADBEEF, "test-bp");
    REQUIRE(id > 0);
    REQUIRE(PS5x::Debugger::EnableBreakpoint(id, false));
    REQUIRE(PS5x::Debugger::EnableBreakpoint(id, true));
    REQUIRE(PS5x::Debugger::RemoveBreakpoint(id));
    REQUIRE(!PS5x::Debugger::RemoveBreakpoint(id)); // already removed
    Teardown();
}

TEST_CASE("Debugger – ClearBreakpoints removes all", "[dbg]")
{
    Setup();
    PS5x::Debugger::AddBreakpoint(0x1000, "a");
    PS5x::Debugger::AddBreakpoint(0x2000, "b");
    PS5x::Debugger::AddBreakpoint(0x3000, "c");
    PS5x::Debugger::ClearBreakpoints();
    // No crash, and a new BP gets a valid id
    uint32_t id = PS5x::Debugger::AddBreakpoint(0x4000, "d");
    REQUIRE(id > 0);
    Teardown();
}

TEST_CASE("Debugger – Watchpoint add / clear", "[dbg]")
{
    Setup();
    uint32_t id = PS5x::Debugger::AddWatchpoint(0x8000, 8, false, true, "wp-write");
    REQUIRE(id == 0); // first watchpoint is index 0
    PS5x::Debugger::ClearWatchpoints();
    Teardown();
}

TEST_CASE("Debugger – GetCpuState returns nullopt initially", "[dbg]")
{
    Setup();
    auto s = PS5x::Debugger::GetCpuState();
    REQUIRE(!s.has_value());
    Teardown();
}

TEST_CASE("Debugger – OnBreakpointHit fires callback and sets CPU state", "[dbg]")
{
    Setup();
    uint32_t bpId = PS5x::Debugger::AddBreakpoint(0x5555, "hitme");

    bool cbFired = false;
    PS5x::Debugger::RegisterBreakpointCallback(
        [&](const PS5x::Debugger::Breakpoint& bp,
            const PS5x::Debugger::CpuState& cs)
        {
            cbFired = true;
            REQUIRE(bp.address == 0x5555);
            REQUIRE(cs.rip     == 0x5555);
        });

    PS5x::Debugger::CpuState state{};
    state.rip = 0x5555;
    state.rsp = 0x7FFF0000;
    PS5x::Debugger::OnBreakpointHit(bpId, state);

    REQUIRE(cbFired);
    REQUIRE(PS5x::Debugger::IsPaused());

    auto cs = PS5x::Debugger::GetCpuState();
    REQUIRE(cs.has_value());
    REQUIRE(cs->rip == 0x5555);

    Teardown();
}

TEST_CASE("Debugger – ReadMemory / WriteMemory on mapped region", "[dbg]")
{
    Setup();
    uintptr_t base = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                        PS5x::Memory::Prot::RW,
                                        PS5x::Memory::AllocType::Data, "dbg-mem");
    REQUIRE(base != 0);

    uint64_t val = 0xCAFEBABE12345678ULL;
    REQUIRE(PS5x::Debugger::WriteMemory(base, &val, sizeof(val)));

    uint64_t read = 0;
    REQUIRE(PS5x::Debugger::ReadMemory(base, &read, sizeof(read)));
    REQUIRE(read == val);

    PS5x::Memory::Unmap(base, PS5x::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("Debugger – HexDumpRegion on mapped memory", "[dbg]")
{
    Setup();
    uintptr_t base = PS5x::Memory::Map(0, PS5x::Memory::PAGE_SIZE,
                                        PS5x::Memory::Prot::RW,
                                        PS5x::Memory::AllocType::Data, "hex-test");
    REQUIRE(base != 0);
    std::memset(reinterpret_cast<void*>(base), 0x41, 32);

    std::string dump = PS5x::Debugger::HexDumpRegion(base, 32);
    REQUIRE(!dump.empty());
    REQUIRE(dump.find("41") != std::string::npos);

    PS5x::Memory::Unmap(base, PS5x::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("Debugger – WriteCrashDump creates file", "[dbg]")
{
    Setup();
    auto dumpDir = (std::filesystem::temp_directory_path() / "ps5x_crash_test").string();
    REQUIRE(PS5x::Debugger::WriteCrashDump(dumpDir));

    // At least one file should exist
    int fileCount = 0;
    for (const auto& e : std::filesystem::directory_iterator(dumpDir)) {
        auto ext = e.path().extension();
        if (ext == ".txt" || ext == ".dmp") ++fileCount;
    }
    REQUIRE(fileCount >= 1);

    std::filesystem::remove_all(dumpDir);
    Teardown();
}

TEST_CASE("Debugger – GetStackTrace returns empty or one frame", "[dbg]")
{
    Setup();
    auto frames = PS5x::Debugger::GetStackTrace();
    // Without a frozen state, zero frames
    REQUIRE(frames.empty());

    // After a simulated BP hit, one frame
    uint32_t id = PS5x::Debugger::AddBreakpoint(0xAABB, "frame-test");
    PS5x::Debugger::CpuState cs{}; cs.rip = 0xAABB; cs.rsp = 0x7000; cs.rbp = 0x7010;
    PS5x::Debugger::OnBreakpointHit(id, cs);
    frames = PS5x::Debugger::GetStackTrace();
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].rip == 0xAABB);
    Teardown();
}
