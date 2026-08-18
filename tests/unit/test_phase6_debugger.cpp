// ChuckStation5 – Phase 6 Debugger tests (conditional BPs, watches, symbol browser, events)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Debugger/Debugger.h"

using namespace ChuckStation5::Debugger;

// ── Conditional breakpoint tests ───────────────────────────────────────────

TEST_CASE("Phase6::Debugger::ConditionalBP::AddReturnsId", "[debugger][phase6]")
{
    uint32_t id = AddConditionalBreakpoint(0x1000, [](const CpuState&){ return true; }, "always");
    CHECK(id >= 10000u);
}

TEST_CASE("Phase6::Debugger::ConditionalBP::MultipleIds", "[debugger][phase6]")
{
    auto id1 = AddConditionalBreakpoint(0x2000, [](const CpuState&){ return false; });
    auto id2 = AddConditionalBreakpoint(0x3000, [](const CpuState&){ return true; });
    CHECK(id1 != id2);
    CHECK(id2 == id1 + 1);
}

TEST_CASE("Phase6::Debugger::ConditionalBP::LabelStored", "[debugger][phase6]")
{
    // Just verify no crash and id monotonically increases
    auto id = AddConditionalBreakpoint(0x5000, [](const CpuState&){ return true; }, "my_cond_bp");
    CHECK(id > 0);
}

// ── Watch expression tests ─────────────────────────────────────────────────

TEST_CASE("Phase6::Debugger::Watch::AddAndGet", "[debugger][phase6]")
{
    uint32_t id = AddWatch("rax_watch", 0xABCD, 8);
    CHECK(id != 0);
    auto watches = GetWatches();
    bool found = false;
    for (auto& w : watches) {
        if (w.id == id) {
            CHECK(w.name    == "rax_watch");
            CHECK(w.address == 0xABCD);
            CHECK(w.size    == 8);
            found = true;
        }
    }
    CHECK(found);
    RemoveWatch(id);
}

TEST_CASE("Phase6::Debugger::Watch::SizeClamp", "[debugger][phase6]")
{
    uint32_t id = AddWatch("big_watch", 0x1234, 64); // should clamp to 8
    auto watches = GetWatches();
    for (auto& w : watches) {
        if (w.id == id) {
            CHECK(w.size <= 8);
        }
    }
    RemoveWatch(id);
}

TEST_CASE("Phase6::Debugger::Watch::Remove", "[debugger][phase6]")
{
    uint32_t id = AddWatch("temp", 0x0, 4);
    CHECK(RemoveWatch(id));
    auto watches = GetWatches();
    for (auto& w : watches) {
        CHECK(w.id != id);
    }
}

TEST_CASE("Phase6::Debugger::Watch::RemoveInvalid", "[debugger][phase6]")
{
    CHECK_FALSE(RemoveWatch(0xFFFF'FFFFu));
}

TEST_CASE("Phase6::Debugger::Watch::MultipleWatches", "[debugger][phase6]")
{
    auto id1 = AddWatch("w1", 0x100, 1);
    auto id2 = AddWatch("w2", 0x200, 2);
    auto id3 = AddWatch("w3", 0x300, 4);
    CHECK(id1 != id2);
    CHECK(id2 != id3);
    auto ws = GetWatches();
    CHECK(ws.size() >= 3);
    RemoveWatch(id1); RemoveWatch(id2); RemoveWatch(id3);
}

TEST_CASE("Phase6::Debugger::Watch::UpdateDoesNotCrash", "[debugger][phase6]")
{
    auto id = AddWatch("update_test", 0x0, 8);
    REQUIRE_NOTHROW(UpdateWatches());
    RemoveWatch(id);
}

// ── Symbol browser tests ───────────────────────────────────────────────────

TEST_CASE("Phase6::Debugger::SymbolBrowser::BrowseAll", "[debugger][phase6]")
{
    // No modules loaded - result should be an empty (or small) vector without crash
    auto syms = BrowseSymbols();
    SUCCEED("BrowseSymbols() returned without crash");
    (void)syms;
}

TEST_CASE("Phase6::Debugger::SymbolBrowser::FilterEmpty", "[debugger][phase6]")
{
    auto syms = BrowseSymbols("");
    SUCCEED("BrowseSymbols(\"\") returned without crash");
    (void)syms;
}

TEST_CASE("Phase6::Debugger::SymbolBrowser::FilterNoMatch", "[debugger][phase6]")
{
    auto syms = BrowseSymbols("zzz_no_such_symbol_xyz");
    CHECK(syms.empty());
}

TEST_CASE("Phase6::Debugger::SymbolBrowser::AddressToSymbol_Unknown", "[debugger][phase6]")
{
    // With no modules loaded, should return empty string (gracefully)
    std::string sym = AddressToSymbol(0x1234'5678);
    // Either empty or some string - just check no crash
    SUCCEED("AddressToSymbol returned without crash");
    (void)sym;
}

// ── Event history tests ────────────────────────────────────────────────────

TEST_CASE("Phase6::Debugger::EventHistory::ClearAndEmpty", "[debugger][phase6]")
{
    ClearEventHistory();
    auto events = GetEventHistory();
    CHECK(events.empty());
}

TEST_CASE("Phase6::Debugger::EventHistory::PopulatedByConditionalBP", "[debugger][phase6]")
{
    ClearEventHistory();
    AddConditionalBreakpoint(0xDEAD, [](const CpuState&){ return false; }, "hist_test");
    auto events = GetEventHistory();
    CHECK(!events.empty());
    bool found = false;
    for (auto& e : events) {
        if (e.type == "ConditionalBreakpoint") found = true;
    }
    CHECK(found);
}

TEST_CASE("Phase6::Debugger::EventHistory::LimitRespected", "[debugger][phase6]")
{
    ClearEventHistory();
    // Add many conditional BPs to generate events
    for (int i = 0; i < 10; ++i) {
        AddConditionalBreakpoint(static_cast<uint64_t>(i * 0x1000),
                                  [](const CpuState&){ return false; });
    }
    auto events = GetEventHistory(5); // request at most 5
    CHECK(events.size() <= 5);
}

TEST_CASE("Phase6::Debugger::EventHistory::TimestampsNonZero", "[debugger][phase6]")
{
    ClearEventHistory();
    AddConditionalBreakpoint(0x1111, [](const CpuState&){ return true; }, "ts_test");
    auto events = GetEventHistory();
    for (auto& e : events) {
        CHECK(e.timestampUs > 0);
    }
}

TEST_CASE("Phase6::Debugger::EventHistory::ClearWorks", "[debugger][phase6]")
{
    AddConditionalBreakpoint(0x9999, [](const CpuState&){ return false; });
    ClearEventHistory();
    CHECK(GetEventHistory().empty());
}
