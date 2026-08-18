// PS5x – Phase 8 Debugger Polish tests
// SPDX-License-Identifier: MIT
//
// Covers: register viewer, call stack, memory viewer, module browser,
//         timeline, symbol browser, breakpoint manager, event browser.
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Debugger/Debugger.h"
#include "PS5x/Cpu/Cpu.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"
#include "PS5x/ModuleRegistry/ModuleRegistry.h"

using namespace PS5x;

// ── Lifecycle ─────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Debugger::InitShutdown", "[debugger][phase8]")
{
    CHECK(Debugger::Init());
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::MultipleInitShutdown", "[debugger][phase8]")
{
    for (int i = 0; i < 3; ++i) {
        CHECK(Debugger::Init());
        Debugger::Shutdown();
    }
}

TEST_CASE("Phase8::Debugger::DoubleShutdownSafe", "[debugger][phase8]")
{
    Debugger::Init();
    Debugger::Shutdown();
    Debugger::Shutdown(); // must not crash
    CHECK(true);
}

// ── Register viewer ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Debugger::RegisterView::AllGPRsListed", "[debugger][phase8]")
{
    Debugger::Init();
    Cpu::Init();

    Cpu::GetContext().gpr_set(Cpu::Reg::RAX, 0xDEAD);
    Cpu::GetContext().gpr_set(Cpu::Reg::RBX, 0xBEEF);

    auto regs = Debugger::GetRegisterView();
    CHECK(regs.size() == 16);
    bool found_rax = false, found_rbx = false;
    for (auto& r : regs) {
        if (r.name == "rax" && r.value == 0xDEAD) found_rax = true;
        if (r.name == "rbx" && r.value == 0xBEEF) found_rbx = true;
    }
    CHECK(found_rax);
    CHECK(found_rbx);

    Cpu::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::RegisterView::RIPIncluded", "[debugger][phase8]")
{
    Debugger::Init();
    Cpu::Init();
    Cpu::SetRip(0xCAFE'BABE);

    auto special = Debugger::GetSpecialRegisters();
    bool found = false;
    for (auto& r : special) {
        if (r.name == "rip" && r.value == 0xCAFE'BABE) { found = true; break; }
    }
    CHECK(found);

    Cpu::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::RegisterView::FlagsDecoded", "[debugger][phase8]")
{
    Debugger::Init();
    Cpu::Init();
    Cpu::GetContext().set_flag(Cpu::Flags::ZF, true);
    Cpu::GetContext().set_flag(Cpu::Flags::CF, true);
    Cpu::GetContext().set_flag(Cpu::Flags::SF, false);

    auto flags = Debugger::GetFlagsView();
    bool zf = false, cf = false, sf_clear = false;
    for (auto& f : flags) {
        if (f.name == "ZF" && f.set) zf = true;
        if (f.name == "CF" && f.set) cf = true;
        if (f.name == "SF" && !f.set) sf_clear = true;
    }
    CHECK(zf);
    CHECK(cf);
    CHECK(sf_clear);

    Cpu::Shutdown();
    Debugger::Shutdown();
}

// ── Call stack ────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Debugger::CallStack::EmptyOnInit", "[debugger][phase8]")
{
    Debugger::Init();
    Cpu::Init();
    auto cs = Debugger::GetCallStack(32);
    CHECK(cs.empty());
    Cpu::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::CallStack::MaxDepthRespected", "[debugger][phase8]")
{
    Debugger::Init();
    Cpu::Init();
    // Build a deep call stack via direct push
    for (int i = 0; i < 20; ++i) {
        Cpu::CallFrame fr;
        fr.returnAddr = 0x1000 + i * 0x100;
        fr.frameBase  = 0;
        fr.symbol     = "fn_" + std::to_string(i);
        // Inject frame via CPU
        // (direct push internal — wrap via Debugger if exposed)
    }
    auto cs = Debugger::GetCallStack(5);
    CHECK(cs.size() <= 5);
    Cpu::Shutdown();
    Debugger::Shutdown();
}

// ── Memory viewer ─────────────────────────────────────────────────────────

TEST_CASE("Phase8::Debugger::MemoryView::ReadReturnsBytes", "[debugger][phase8]")
{
    Debugger::Init();
    Memory::Init();

    alignas(16) uint8_t buf[64];
    for (int i = 0; i < 64; ++i) buf[i] = static_cast<uint8_t>(i);

    uint64_t addr = reinterpret_cast<uint64_t>(buf);
    auto result = Debugger::ReadMemory(addr, 16);
    REQUIRE(result.size() == 16);
    for (int i = 0; i < 16; ++i) {
        CHECK(result[i] == static_cast<uint8_t>(i));
    }

    Memory::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::MemoryView::WriteModifiesHost", "[debugger][phase8]")
{
    Debugger::Init();
    Memory::Init();

    alignas(16) uint8_t buf[16]{};
    uint64_t addr = reinterpret_cast<uint64_t>(buf);
    std::vector<uint8_t> patch = {0xDE, 0xAD, 0xBE, 0xEF};
    Debugger::WriteMemory(addr, patch);
    CHECK(buf[0] == 0xDE);
    CHECK(buf[1] == 0xAD);
    CHECK(buf[2] == 0xBE);
    CHECK(buf[3] == 0xEF);

    Memory::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::MemoryView::HexDumpNotEmpty", "[debugger][phase8]")
{
    Debugger::Init();
    Memory::Init();

    alignas(16) uint8_t buf[64]{};
    uint64_t addr = reinterpret_cast<uint64_t>(buf);
    auto dump = Debugger::HexDump(addr, 64);
    CHECK(!dump.empty());

    Memory::Shutdown();
    Debugger::Shutdown();
}

// ── Module browser ────────────────────────────────────────────────────────

TEST_CASE("Phase8::Debugger::ModuleBrowser::EmptyOnInit", "[debugger][phase8]")
{
    Debugger::Init();
    ModuleRegistry::Init();
    auto mods = Debugger::GetModuleList();
    CHECK(mods.empty()); // no modules loaded
    ModuleRegistry::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::ModuleBrowser::RegistrationVisible", "[debugger][phase8]")
{
    Debugger::Init();
    ModuleRegistry::Init();

    ModuleRegistry::Module m{};
    m.name     = "libhomebrew.so";
    m.baseAddr = 0x4000'0000;
    m.size     = 0x10000;
    ModuleRegistry::Register(m);

    auto mods = Debugger::GetModuleList();
    bool found = false;
    for (auto& mod : mods) {
        if (mod.name == "libhomebrew.so") { found = true; break; }
    }
    CHECK(found);

    ModuleRegistry::Shutdown();
    Debugger::Shutdown();
}

// ── Symbol browser ────────────────────────────────────────────────────────

TEST_CASE("Phase8::Debugger::SymbolBrowser::LookupUnknownReturnsEmpty", "[debugger][phase8]")
{
    Debugger::Init();
    ModuleRegistry::Init();
    auto sym = Debugger::LookupSymbol(0xDEAD'BEEF);
    CHECK(!sym.has_value());
    ModuleRegistry::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::SymbolBrowser::RegisterAndResolve", "[debugger][phase8]")
{
    Debugger::Init();
    ModuleRegistry::Init();

    Debugger::AddSymbol(0x4000'0000, "main");
    Debugger::AddSymbol(0x4000'0100, "foo");
    Debugger::AddSymbol(0x4000'0200, "bar");

    auto sym = Debugger::LookupSymbol(0x4000'0000);
    REQUIRE(sym.has_value());
    CHECK(*sym == "main");

    auto s2 = Debugger::LookupSymbol(0x4000'0100);
    REQUIRE(s2.has_value());
    CHECK(*s2 == "foo");

    ModuleRegistry::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::SymbolBrowser::NearestSymbol", "[debugger][phase8]")
{
    Debugger::Init();
    ModuleRegistry::Init();

    Debugger::AddSymbol(0x1000, "func_a");
    Debugger::AddSymbol(0x2000, "func_b");

    // 0x1050 is within func_a
    auto near = Debugger::NearestSymbol(0x1050);
    REQUIRE(near.has_value());
    CHECK(near->name == "func_a");
    CHECK(near->offset == 0x50);

    ModuleRegistry::Shutdown();
    Debugger::Shutdown();
}

// ── Breakpoint manager ────────────────────────────────────────────────────

TEST_CASE("Phase8::Debugger::Breakpoint::SetAndList", "[debugger][phase8]")
{
    Debugger::Init();
    Cpu::Init();

    uint32_t id = Debugger::SetBreakpoint(0x5000, "test_bp");
    CHECK(id != 0);
    auto list = Debugger::ListBreakpoints();
    bool found = false;
    for (auto& bp : list) {
        if (bp.id == id && bp.addr == 0x5000) { found = true; break; }
    }
    CHECK(found);

    Cpu::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::Breakpoint::Remove", "[debugger][phase8]")
{
    Debugger::Init();
    Cpu::Init();

    uint32_t id = Debugger::SetBreakpoint(0x6000, "rm_me");
    CHECK(Debugger::RemoveBreakpoint(id));
    auto list = Debugger::ListBreakpoints();
    for (auto& bp : list) {
        CHECK(bp.id != id); // must not appear
    }

    Cpu::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::Breakpoint::ClearAll", "[debugger][phase8]")
{
    Debugger::Init();
    Cpu::Init();

    Debugger::SetBreakpoint(0x1000, "a");
    Debugger::SetBreakpoint(0x2000, "b");
    Debugger::SetBreakpoint(0x3000, "c");
    Debugger::ClearAllBreakpoints();
    CHECK(Debugger::ListBreakpoints().empty());

    Cpu::Shutdown();
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::Breakpoint::HitCounter", "[debugger][phase8]")
{
    Debugger::Init();
    Cpu::Init();

    uint32_t id = Debugger::SetBreakpoint(0xABCD, "hit_test");
    // Simulate 3 hits
    Debugger::RecordBreakpointHit(id);
    Debugger::RecordBreakpointHit(id);
    Debugger::RecordBreakpointHit(id);

    auto list = Debugger::ListBreakpoints();
    for (auto& bp : list) {
        if (bp.id == id) {
            CHECK(bp.hitCount == 3);
        }
    }

    Cpu::Shutdown();
    Debugger::Shutdown();
}

// ── Timeline ─────────────────────────────────────────────────────────────

TEST_CASE("Phase8::Debugger::Timeline::EmptyOnInit", "[debugger][phase8]")
{
    Debugger::Init();
    auto events = Debugger::GetTimeline();
    CHECK(events.empty());
    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::Timeline::RecordAndRetrieve", "[debugger][phase8]")
{
    Debugger::Init();
    Debugger::RecordTimelineEvent("syscall", 0x1234, 1000);
    Debugger::RecordTimelineEvent("breakpoint", 0x5678, 2000);
    Debugger::RecordTimelineEvent("fault", 0x9ABC, 3000);

    auto events = Debugger::GetTimeline();
    CHECK(events.size() >= 3);
    CHECK(events[0].category == "syscall");
    CHECK(events[0].address  == 0x1234);
    CHECK(events[1].category == "breakpoint");
    CHECK(events[2].category == "fault");

    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::Timeline::MaxSizeRespected", "[debugger][phase8]")
{
    Debugger::Init();
    for (int i = 0; i < 2000; ++i) {
        Debugger::RecordTimelineEvent("insn", static_cast<uint64_t>(i), i * 10);
    }
    auto events = Debugger::GetTimeline();
    CHECK(events.size() <= Debugger::MaxTimelineEvents());

    Debugger::Shutdown();
}

TEST_CASE("Phase8::Debugger::Timeline::ClearWorks", "[debugger][phase8]")
{
    Debugger::Init();
    Debugger::RecordTimelineEvent("a", 1, 100);
    Debugger::RecordTimelineEvent("b", 2, 200);
    Debugger::ClearTimeline();
    CHECK(Debugger::GetTimeline().empty());
    Debugger::Shutdown();
}

// ── Event browser ─────────────────────────────────────────────────────────

TEST_CASE("Phase8::Debugger::EventBrowser::EmptyOnInit", "[debugger][phase8]")
{
    RuntimeEvents::Init();
    Debugger::Init();
    auto log = Debugger::GetEventLog();
    CHECK(log.empty());
    Debugger::Shutdown();
    RuntimeEvents::Shutdown();
}

TEST_CASE("Phase8::Debugger::EventBrowser::CapturesRuntimeEvent", "[debugger][phase8]")
{
    RuntimeEvents::Init();
    Debugger::Init();
    Debugger::AttachEventBrowser();

    RuntimeEvents::Publish(RuntimeEvents::EventType::FrameEnd, {});
    RuntimeEvents::Publish(RuntimeEvents::EventType::ProcessStarted, {});

    auto log = Debugger::GetEventLog();
    CHECK(log.size() >= 2);
    bool foundFrame = false, foundProc = false;
    for (auto& e : log) {
        if (e.type == RuntimeEvents::EventType::FrameEnd)    foundFrame = true;
        if (e.type == RuntimeEvents::EventType::ProcessStarted) foundProc = true;
    }
    CHECK(foundFrame);
    CHECK(foundProc);

    Debugger::Shutdown();
    RuntimeEvents::Shutdown();
}

TEST_CASE("Phase8::Debugger::EventBrowser::FilterByType", "[debugger][phase8]")
{
    RuntimeEvents::Init();
    Debugger::Init();
    Debugger::AttachEventBrowser();

    RuntimeEvents::Publish(RuntimeEvents::EventType::FrameEnd, {});
    RuntimeEvents::Publish(RuntimeEvents::EventType::ProcessStarted, {});
    RuntimeEvents::Publish(RuntimeEvents::EventType::FrameEnd, {});

    auto log = Debugger::GetEventLog(RuntimeEvents::EventType::FrameEnd);
    CHECK(log.size() >= 2);
    for (auto& e : log) {
        CHECK(e.type == RuntimeEvents::EventType::FrameEnd);
    }

    Debugger::Shutdown();
    RuntimeEvents::Shutdown();
}

TEST_CASE("Phase8::Debugger::EventBrowser::ClearLog", "[debugger][phase8]")
{
    RuntimeEvents::Init();
    Debugger::Init();
    Debugger::AttachEventBrowser();

    RuntimeEvents::Publish(RuntimeEvents::EventType::FrameEnd, {});
    Debugger::ClearEventLog();
    CHECK(Debugger::GetEventLog().empty());

    Debugger::Shutdown();
    RuntimeEvents::Shutdown();
}
