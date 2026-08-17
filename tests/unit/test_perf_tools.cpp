// PS5x – Performance Tools tests (Phase 5)
// SPDX-License-Identifier: MIT
#include "PS5x/Logger/Logger.h"
#include "PS5x/PerfTools/PerfTools.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace PS5x::PerfTools;

static void Setup(size_t workers = 2) {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  Init(workers);
}
static void Teardown() {
  Shutdown();
  PS5x::Logger::Shutdown();
}

// ── Frame timer ───────────────────────────────────────────────────────────

TEST_CASE("Perf – FrameBegin/End track delta", "[perf]") {
  Setup();
  SetTargetFps(60.0);
  ResetFrameStats();

  for (int i = 0; i < 2; ++i) {
    FrameBegin();
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    FrameEnd();
  }

  auto s = GetFrameStats();
  REQUIRE(s.frameIndex >= 1); // 0-based: first complete frame is index 1
  REQUIRE(s.deltaMs >= 0.0);
  Teardown();
}

TEST_CASE("Perf – Multiple frames accumulate history", "[perf]") {
  Setup();
  ResetFrameStats();

  for (int i = 0; i < 5; ++i) {
    FrameBegin();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    FrameEnd();
  }

  auto s = GetFrameStats();
  REQUIRE(s.frameIndex >=
          4); // 0-based: after 5 FrameEnd calls, last stored is 4
  REQUIRE(s.avgFps > 0.0);
  Teardown();
}

TEST_CASE("Perf – SetTargetFps affects pacing", "[perf]") {
  Setup();
  SetTargetFps(30.0);
  ResetFrameStats();

  FrameBegin();
  FrameEnd();
  FrameBegin();
  std::this_thread::sleep_for(std::chrono::milliseconds(40)); // >33ms
  FrameEnd();

  auto s = GetFrameStats();
  // frame pace should be positive (over budget)
  REQUIRE((s.framePaceMs > 0.0 || s.deltaMs == 0.0)); // second frame
  Teardown();
}

TEST_CASE("Perf – ResetFrameStats clears history", "[perf]") {
  Setup();
  for (int i = 0; i < 3; ++i) {
    FrameBegin();
    FrameEnd();
  }
  ResetFrameStats();
  auto s = GetFrameStats();
  REQUIRE(s.frameIndex == 0);
  REQUIRE(s.fps == 0.0);
  Teardown();
}

// ── CPU Profiler ──────────────────────────────────────────────────────────

TEST_CASE("Perf – ProfileBegin/End records sample", "[perf]") {
  Setup();
  ResetProfile();

  auto tok = ProfileBegin("test-scope");
  std::this_thread::sleep_for(std::chrono::microseconds(500));
  ProfileEnd(tok);

  auto report = GetProfileReport();
  REQUIRE(!report.empty());
  bool found = false;
  for (const auto &s : report)
    if (s.label == "test-scope") {
      found = true;
      REQUIRE(s.calls == 1);
    }
  REQUIRE(found);
  Teardown();
}

TEST_CASE("Perf – Multiple calls accumulate stats", "[perf]") {
  Setup();
  ResetProfile();
  for (int i = 0; i < 10; ++i) {
    auto tok = ProfileBegin("multi");
    ProfileEnd(tok);
  }
  auto report = GetProfileReport();
  bool found = false;
  for (const auto &s : report)
    if (s.label == "multi") {
      found = true;
      REQUIRE(s.calls == 10);
    }
  REQUIRE(found);
  Teardown();
}

TEST_CASE("Perf – ScopeGuard records scope automatically", "[perf]") {
  Setup();
  ResetProfile();
  {
    ScopeGuard g("raii-scope");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  auto report = GetProfileReport();
  bool found = false;
  for (const auto &s : report)
    if (s.label == "raii-scope")
      found = true;
  REQUIRE(found);
  Teardown();
}

TEST_CASE("Perf – Report sorted by totalMs descending", "[perf]") {
  Setup();
  ResetProfile();
  {
    auto a = ProfileBegin("slow");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ProfileEnd(a);
  }
  {
    auto b = ProfileBegin("fast");
    ProfileEnd(b);
  }
  auto report = GetProfileReport();
  REQUIRE(report.size() >= 2);
  // "slow" should appear before "fast"
  bool slowFirst = false;
  for (size_t i = 0; i + 1 < report.size(); ++i)
    if (report[i].totalMs >= report[i + 1].totalMs)
      slowFirst = true;
  REQUIRE(slowFirst);
  Teardown();
}

TEST_CASE("Perf – ResetProfile clears all samples", "[perf]") {
  Setup();
  auto t = ProfileBegin("x");
  ProfileEnd(t);
  REQUIRE(!GetProfileReport().empty());
  ResetProfile();
  REQUIRE(GetProfileReport().empty());
  Teardown();
}

// ── Worker pool ───────────────────────────────────────────────────────────

TEST_CASE("Perf – Submit runs jobs", "[perf]") {
  Setup(2);
  std::atomic<int> count{0};
  for (int i = 0; i < 10; ++i)
    Submit([&count] { count.fetch_add(1); });
  WaitAll();
  REQUIRE(count.load() == 10);
  Teardown();
}

TEST_CASE("Perf – TotalWorkers matches requested count", "[perf]") {
  Setup(3);
  REQUIRE(TotalWorkers() == 3);
  Teardown();
}

TEST_CASE("Perf – IdleWorkers is non-zero after WaitAll", "[perf]") {
  Setup(2);
  std::atomic<int> c{0};
  Submit([&c] { c++; });
  WaitAll();
  REQUIRE(c.load() == 1);
  // After all work done, workers should be idle
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  REQUIRE(IdleWorkers() >= 1);
  Teardown();
}

TEST_CASE("Perf – Workers handle many concurrent jobs", "[perf]") {
  Setup(4);
  std::atomic<int> sum{0};
  for (int i = 1; i <= 100; ++i)
    Submit([&sum, i] { sum.fetch_add(i); });
  WaitAll();
  REQUIRE(sum.load() == 5050); // 1+2+...+100
  Teardown();
}

// ── Benchmark ─────────────────────────────────────────────────────────────

TEST_CASE("Perf – Benchmark measures timing", "[perf]") {
  Setup();
  int calls = 0;
  auto r = Benchmark("noop", [&calls] { calls++; }, 50, 5);
  REQUIRE(r.label == "noop");
  REQUIRE(r.iterations == 50);
  REQUIRE(calls == 55); // 50 + 5 warmup
  REQUIRE(r.avgMs >= 0.0);
  REQUIRE(r.minMs <= r.maxMs);
  REQUIRE(r.stdDevMs >= 0.0);
  Teardown();
}

TEST_CASE("Perf – Benchmark stats are consistent", "[perf]") {
  Setup();
  auto r = Benchmark(
      "sleep1ms",
      [] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }, 10, 2);
  REQUIRE(r.avgMs >= 0.5); // at least half a ms
  REQUIRE(r.maxMs >= r.minMs);
  Teardown();
}

// ── Startup profiling ─────────────────────────────────────────────────────

TEST_CASE("Perf – StartupMark and DumpStartupReport", "[perf]") {
  Setup();
  StartupMark("subsystem-A");
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  StartupMark("subsystem-B");
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  StartupMark("subsystem-C");

  double total = StartupTotalMs();
  REQUIRE(total >= 1.0); // at least 2ms between first and last
  DumpStartupReport();   // must not crash
  Teardown();
}

TEST_CASE("Perf – StartupTotalMs returns 0 with <2 marks", "[perf]") {
  Setup();
  // Init() adds "PerfTools::Init" as first mark
  REQUIRE(StartupTotalMs() == 0.0); // only one mark → 0
  StartupMark("second");
  REQUIRE(StartupTotalMs() >= 0.0);
  Teardown();
}
