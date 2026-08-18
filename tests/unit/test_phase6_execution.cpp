// ChuckStation5 – Phase 6 Execution engine tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Execution/Execution.h"

using namespace ChuckStation5::Execution;

TEST_CASE("Phase6::Execution::ExitReasonNames", "[execution][phase6]")
{
    CHECK(std::string(ExitReasonName(ExitReason::Normal))     == "Normal");
    CHECK(std::string(ExitReasonName(ExitReason::GuestPanic)) == "GuestPanic");
    CHECK(std::string(ExitReasonName(ExitReason::Fault))      == "Fault");
    CHECK(std::string(ExitReasonName(ExitReason::Timeout))    == "Timeout");
    CHECK(std::string(ExitReasonName(ExitReason::Requested))  == "Requested");
    CHECK(std::string(ExitReasonName(ExitReason::Unknown))    == "Unknown");
}

TEST_CASE("Phase6::Execution::ReportFault", "[execution][phase6]")
{
    Init();
    REQUIRE(GetState() != ExecState::Running);

    ReportFault(0xDEAD'BEEF, "test page fault");
    ExitInfo info = GetExitInfo();

    CHECK(info.reason    == ExitReason::Fault);
    CHECK(info.faultAddr == 0xDEAD'BEEF);
    CHECK(info.message   == "test page fault");
    CHECK(GetState()     == ExecState::Faulted);
    Shutdown();
}

TEST_CASE("Phase6::Execution::ReportGuestPanic", "[execution][phase6]")
{
    Init();
    ReportGuestPanic("kernel panic: out of memory");
    ExitInfo info = GetExitInfo();

    CHECK(info.reason  == ExitReason::GuestPanic);
    CHECK(info.message == "kernel panic: out of memory");
    CHECK(GetState()   == ExecState::Faulted);
    Shutdown();
}

TEST_CASE("Phase6::Execution::GuestLoop::InjectTrap", "[execution][phase6]")
{
    Init();
    // InjectTrap should not crash; it publishes an event and logs
    REQUIRE_NOTHROW(GuestLoop::InjectTrap(0x80));
    REQUIRE_NOTHROW(GuestLoop::InjectTrap(0x03));
    Shutdown();
}

TEST_CASE("Phase6::Execution::GuestLoop::DispatchException", "[execution][phase6]")
{
    Init();
    // #PF with error code
    REQUIRE_NOTHROW(GuestLoop::DispatchException(14, 0x0003));
    CHECK(GetState() == ExecState::Faulted);
    Shutdown();
}

TEST_CASE("Phase6::Execution::GetExitInfo_default", "[execution][phase6]")
{
    Init();
    ExitInfo info = GetExitInfo();
    // Fresh init - no fault yet
    CHECK(info.reason == ExitReason::Unknown);
    Shutdown();
}

TEST_CASE("Phase6::Execution::Statistics_after_fault", "[execution][phase6]")
{
    Init();
    ReportFault(0x1234, "stat test");
    ExitInfo info = GetExitInfo();
    CHECK(info.faultAddr == 0x1234);
    CHECK(!info.message.empty());
    Shutdown();
}
