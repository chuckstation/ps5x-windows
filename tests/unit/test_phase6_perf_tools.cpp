// ChuckStation5 – Phase 6 PerfTools tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ChuckStation5/PerfTools/PerfTools.h"
#include <thread>
#include <chrono>

using namespace ChuckStation5::PerfTools;

// ── GPU timestamps ─────────────────────────────────────────────────────────

TEST_CASE("Phase6::Perf::GpuTs::BeginEndRecorded", "[perf_tools][phase6]")
{
    Init();
    ResetGpuTimestamps();
    GpuTimestampBegin("shadow_pass");
    std::this_thread::sleep_for(std::chrono::microseconds(500));
    GpuTimestampEnd("shadow_pass");
    auto ts = GetGpuTimestamps();
    REQUIRE(!ts.empty());
    CHECK(ts.back().label      == "shadow_pass");
    CHECK(ts.back().durationMs >= 0.0);
    CHECK(ts.back().endUs      >= ts.back().startUs);
    Shutdown();
}

TEST_CASE("Phase6::Perf::GpuTs::MultipleLabels", "[perf_tools][phase6]")
{
    Init();
    ResetGpuTimestamps();
    GpuTimestampBegin("pass_a");
    GpuTimestampBegin("pass_b");
    GpuTimestampEnd("pass_a");
    GpuTimestampEnd("pass_b");
    auto ts = GetGpuTimestamps();
    CHECK(ts.size() >= 2);
    Shutdown();
}

TEST_CASE("Phase6::Perf::GpuTs::Reset", "[perf_tools][phase6]")
{
    Init();
    GpuTimestampBegin("x");
    GpuTimestampEnd("x");
    ResetGpuTimestamps();
    CHECK(GetGpuTimestamps().empty());
    Shutdown();
}

TEST_CASE("Phase6::Perf::GpuTs::OrphanedBeginDoesNotCrash", "[perf_tools][phase6]")
{
    Init();
    ResetGpuTimestamps();
    GpuTimestampBegin("orphan");
    // No End - just reset
    REQUIRE_NOTHROW(ResetGpuTimestamps());
    Shutdown();
}

// ── Lock contention ────────────────────────────────────────────────────────

TEST_CASE("Phase6::Perf::Lock::RecordUncontended", "[perf_tools][phase6]")
{
    Init();
    ResetLockStats();
    RecordLockAcquire("audio_mutex", 0.0, false);
    RecordLockAcquire("audio_mutex", 0.0, false);
    auto report = GetLockReport();
    REQUIRE(!report.empty());
    auto& s = report[0];
    CHECK(s.label        == "audio_mutex");
    CHECK(s.acquisitions == 2);
    CHECK(s.contentions  == 0);
    Shutdown();
}

TEST_CASE("Phase6::Perf::Lock::RecordContended", "[perf_tools][phase6]")
{
    Init();
    ResetLockStats();
    RecordLockAcquire("gpu_queue", 50.0, true);
    RecordLockAcquire("gpu_queue", 20.0, true);
    RecordLockAcquire("gpu_queue",  0.0, false);
    auto report = GetLockReport();
    REQUIRE(!report.empty());
    auto& s = report[0];
    CHECK(s.contentions   == 2);
    CHECK(s.acquisitions  == 3);
    CHECK(s.totalWaitUs   == Catch::Approx(70.0));
    CHECK(s.avgWaitUs     == Catch::Approx(35.0));
    CHECK(s.maxWaitUs     == Catch::Approx(50.0));
    Shutdown();
}

TEST_CASE("Phase6::Perf::Lock::MultipleLocks", "[perf_tools][phase6]")
{
    Init();
    ResetLockStats();
    RecordLockAcquire("mtx_a", 10.0, true);
    RecordLockAcquire("mtx_b",  5.0, true);
    RecordLockAcquire("mtx_c",  0.0, false);
    auto report = GetLockReport();
    CHECK(report.size() == 3);
    // Sorted by contentions desc
    CHECK(report[0].contentions >= report[1].contentions);
    Shutdown();
}

TEST_CASE("Phase6::Perf::Lock::Reset", "[perf_tools][phase6]")
{
    Init();
    RecordLockAcquire("to_clear", 0.0, false);
    ResetLockStats();
    CHECK(GetLockReport().empty());
    Shutdown();
}

// ── Memory timeline ───────────────────────────────────────────────────────

TEST_CASE("Phase6::Perf::Mem::RecordSnapshot", "[perf_tools][phase6]")
{
    Init();
    ResetMemoryTimeline();
    RecordMemorySnapshot(1024 * 1024, 42, 10);
    auto tl = GetMemoryTimeline();
    REQUIRE(!tl.empty());
    CHECK(tl.back().allocatedBytes == 1024 * 1024);
    CHECK(tl.back().allocCount     == 42);
    CHECK(tl.back().freeCount      == 10);
    CHECK(tl.back().timestampUs    > 0);
    Shutdown();
}

TEST_CASE("Phase6::Perf::Mem::MultipleSnapshots", "[perf_tools][phase6]")
{
    Init();
    ResetMemoryTimeline();
    for (int i = 1; i <= 5; ++i)
        RecordMemorySnapshot(i * 1024, i, i / 2);
    auto tl = GetMemoryTimeline();
    CHECK(tl.size() == 5);
    Shutdown();
}

TEST_CASE("Phase6::Perf::Mem::MaxEntriesRespected", "[perf_tools][phase6]")
{
    Init();
    ResetMemoryTimeline();
    for (int i = 0; i < 10; ++i)
        RecordMemorySnapshot(i * 1000, i, 0);
    auto tl = GetMemoryTimeline(5);
    CHECK(tl.size() <= 5);
    Shutdown();
}

TEST_CASE("Phase6::Perf::Mem::Reset", "[perf_tools][phase6]")
{
    Init();
    RecordMemorySnapshot(100, 1, 0);
    ResetMemoryTimeline();
    CHECK(GetMemoryTimeline().empty());
    Shutdown();
}

// ── CPU timeline ──────────────────────────────────────────────────────────

TEST_CASE("Phase6::Perf::Cpu::RecordSample", "[perf_tools][phase6]")
{
    Init();
    ResetCpuTimeline();
    RecordCpuSample(45.5, 8);
    auto tl = GetCpuTimeline();
    REQUIRE(!tl.empty());
    CHECK(tl.back().usagePercent  == Catch::Approx(45.5));
    CHECK(tl.back().activeThreads == 8);
    CHECK(tl.back().timestampUs   > 0);
    Shutdown();
}

TEST_CASE("Phase6::Perf::Cpu::ClampUsage", "[perf_tools][phase6]")
{
    Init();
    ResetCpuTimeline();
    RecordCpuSample(150.0, 1);  // >100 should clamp
    RecordCpuSample(-10.0, 1);  // <0 should clamp
    auto tl = GetCpuTimeline();
    CHECK(tl[0].usagePercent <= 100.0);
    CHECK(tl[1].usagePercent >= 0.0);
    Shutdown();
}

TEST_CASE("Phase6::Perf::Cpu::Reset", "[perf_tools][phase6]")
{
    Init();
    RecordCpuSample(50.0, 4);
    ResetCpuTimeline();
    CHECK(GetCpuTimeline().empty());
    Shutdown();
}

// ── Frame time history ─────────────────────────────────────────────────────

TEST_CASE("Phase6::Perf::FrameHist::EmptyBeforeFrames", "[perf_tools][phase6]")
{
    Init();
    ResetFrameStats();
    auto hist = GetFrameTimeHistory();
    CHECK(hist.empty());
    Shutdown();
}

TEST_CASE("Phase6::Perf::FrameHist::PopulatedAfterFrames", "[perf_tools][phase6]")
{
    Init();
    ResetFrameStats();
    for (int i = 0; i < 5; ++i) {
        FrameBegin();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        FrameEnd();
    }
    auto hist = GetFrameTimeHistory();
    CHECK(!hist.empty());
    for (auto ms : hist) CHECK(ms >= 0.0);
    Shutdown();
}

// ── Report export ──────────────────────────────────────────────────────────

TEST_CASE("Phase6::Perf::Export::JsonNotEmpty", "[perf_tools][phase6]")
{
    Init();
    auto report = ExportReportJson();
    CHECK(!report.empty());
    CHECK(report.find('{') != std::string::npos);
    CHECK(report.find("frame") != std::string::npos);
    Shutdown();
}

TEST_CASE("Phase6::Perf::Export::CsvHasHeader", "[perf_tools][phase6]")
{
    Init();
    auto csv = ExportReportCsv();
    CHECK(!csv.empty());
    CHECK(csv.find("section") != std::string::npos);
    Shutdown();
}
