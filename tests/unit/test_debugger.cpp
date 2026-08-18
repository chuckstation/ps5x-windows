// ChuckStation5 – Debugger unit tests (Phase 2)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/Debugger/Debugger.h"

#include <filesystem>
#include <string>

static void Setup()
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();
    ChuckStation5::Debugger::Init();
}
static void Teardown()
{
    ChuckStation5::Debugger::Shutdown();
    ChuckStation5::Memory::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Debugger – Init / Shutdown cycle", "[dbg]")
{
    Setup();
    REQUIRE(!ChuckStation5::Debugger::IsPaused());
    Teardown();
}

TEST_CASE("Debugger – Pause / Continue", "[dbg]")
{
    Setup();
    ChuckStation5::Debugger::Pause();
    REQUIRE(ChuckStation5::Debugger::IsPaused());
    ChuckStation5::Debugger::Continue();
    REQUIRE(!ChuckStation5::Debugger::IsPaused());
    Teardown();
}

TEST_CASE("Debugger – AddBreakpoint / RemoveBreakpoint (soft)", "[dbg]")
{
    Setup();
    // Soft BP: address not yet mapped
    uint32_t id = ChuckStation5::Debugger::AddBreakpoint(0xDEADBEEF, "test-bp");
    REQUIRE(id > 0);
    REQUIRE(ChuckStation5::Debugger::EnableBreakpoint(id, false));
    REQUIRE(ChuckStation5::Debugger::EnableBreakpoint(id, true));
    REQUIRE(ChuckStation5::Debugger::RemoveBreakpoint(id));
    REQUIRE(!ChuckStation5::Debugger::RemoveBreakpoint(id)); // already removed
    Teardown();
}

TEST_CASE("Debugger – ClearBreakpoints removes all", "[dbg]")
{
    Setup();
    ChuckStation5::Debugger::AddBreakpoint(0x1000, "a");
    ChuckStation5::Debugger::AddBreakpoint(0x2000, "b");
    ChuckStation5::Debugger::AddBreakpoint(0x3000, "c");
    ChuckStation5::Debugger::ClearBreakpoints();
    // No crash, and a new BP gets a valid id
    uint32_t id = ChuckStation5::Debugger::AddBreakpoint(0x4000, "d");
    REQUIRE(id > 0);
    Teardown();
}

TEST_CASE("Debugger – Watchpoint add / clear", "[dbg]")
{
    Setup();
    uint32_t id = ChuckStation5::Debugger::AddWatchpoint(0x8000, 8, false, true, "wp-write");
    REQUIRE(id == 0); // first watchpoint is index 0
    ChuckStation5::Debugger::ClearWatchpoints();
    Teardown();
}

TEST_CASE("Debugger – GetCpuState returns nullopt initially", "[dbg]")
{
    Setup();
    auto s = ChuckStation5::Debugger::GetCpuState();
    REQUIRE(!s.has_value());
    Teardown();
}

TEST_CASE("Debugger – OnBreakpointHit fires callback and sets CPU state", "[dbg]")
{
    Setup();
    uint32_t bpId = ChuckStation5::Debugger::AddBreakpoint(0x5555, "hitme");

    bool cbFired = false;
    ChuckStation5::Debugger::RegisterBreakpointCallback(
        [&](const ChuckStation5::Debugger::Breakpoint& bp,
            const ChuckStation5::Debugger::CpuState& cs)
        {
            cbFired = true;
            REQUIRE(bp.address == 0x5555);
            REQUIRE(cs.rip     == 0x5555);
        });

    ChuckStation5::Debugger::CpuState state{};
    state.rip = 0x5555;
    state.rsp = 0x7FFF0000;
    ChuckStation5::Debugger::OnBreakpointHit(bpId, state);

    REQUIRE(cbFired);
    REQUIRE(ChuckStation5::Debugger::IsPaused());

    auto cs = ChuckStation5::Debugger::GetCpuState();
    REQUIRE(cs.has_value());
    REQUIRE(cs->rip == 0x5555);

    Teardown();
}

TEST_CASE("Debugger – ReadMemory / WriteMemory on mapped region", "[dbg]")
{
    Setup();
    uintptr_t base = ChuckStation5::Memory::Map(0, ChuckStation5::Memory::PAGE_SIZE,
                                        ChuckStation5::Memory::Prot::RW,
                                        ChuckStation5::Memory::AllocType::Data, "dbg-mem");
    REQUIRE(base != 0);

    uint64_t val = 0xCAFEBABE12345678ULL;
    REQUIRE(ChuckStation5::Debugger::WriteMemory(base, &val, sizeof(val)));

    uint64_t read = 0;
    REQUIRE(ChuckStation5::Debugger::ReadMemory(base, &read, sizeof(read)));
    REQUIRE(read == val);

    ChuckStation5::Memory::Unmap(base, ChuckStation5::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("Debugger – HexDumpRegion on mapped memory", "[dbg]")
{
    Setup();
    uintptr_t base = ChuckStation5::Memory::Map(0, ChuckStation5::Memory::PAGE_SIZE,
                                        ChuckStation5::Memory::Prot::RW,
                                        ChuckStation5::Memory::AllocType::Data, "hex-test");
    REQUIRE(base != 0);
    std::memset(reinterpret_cast<void*>(base), 0x41, 32);

    std::string dump = ChuckStation5::Debugger::HexDumpRegion(base, 32);
    REQUIRE(!dump.empty());
    REQUIRE(dump.find("41") != std::string::npos);

    ChuckStation5::Memory::Unmap(base, ChuckStation5::Memory::PAGE_SIZE);
    Teardown();
}

TEST_CASE("Debugger – WriteCrashDump creates file", "[dbg]")
{
    Setup();
    auto dumpDir = (std::filesystem::temp_directory_path() / "chuckstation5_crash_test").string();
    REQUIRE(ChuckStation5::Debugger::WriteCrashDump(dumpDir));

    // At least one file should exist
    int fileCount = 0;
    for (const auto& e : std::filesystem::directory_iterator(dumpDir))
        if (e.path().extension() == ".txt") ++fileCount;
    REQUIRE(fileCount >= 1);

    std::filesystem::remove_all(dumpDir);
    Teardown();
}

TEST_CASE("Debugger – GetStackTrace returns empty or one frame", "[dbg]")
{
    Setup();
    auto frames = ChuckStation5::Debugger::GetStackTrace();
    // Without a frozen state, zero frames
    REQUIRE(frames.empty());

    // After a simulated BP hit, one frame
    uint32_t id = ChuckStation5::Debugger::AddBreakpoint(0xAABB, "frame-test");
    ChuckStation5::Debugger::CpuState cs{}; cs.rip = 0xAABB; cs.rsp = 0x7000; cs.rbp = 0x7010;
    ChuckStation5::Debugger::OnBreakpointHit(id, cs);
    frames = ChuckStation5::Debugger::GetStackTrace();
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].rip == 0xAABB);
    Teardown();
}
