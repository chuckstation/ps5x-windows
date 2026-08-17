// PS5x – Phase 8 Performance tests
// SPDX-License-Identifier: MIT
//
// Validates: hot-path benchmarks, interpreter throughput,
//            memory allocation overhead, command processing speed,
//            logging overhead, startup time measurement.
#include "PS5x/CommandProcessor/CommandProcessor.h"
#include "PS5x/Cpu/Cpu.h"
#include "PS5x/GPU/GPU.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/PerfTools/PerfTools.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <vector>

using namespace PS5x;
using namespace PS5x::CommandProcessor;
using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;

// ── PerfTools lifecycle ───────────────────────────────────────────────────

TEST_CASE("Phase8::Perf::PerfTools::InitShutdown", "[perf][phase8]") {
  CHECK(PerfTools::Init());
  PerfTools::Shutdown();
}

TEST_CASE("Phase8::Perf::PerfTools::MultipleInitShutdown", "[perf][phase8]") {
  for (int i = 0; i < 3; ++i) {
    CHECK(PerfTools::Init());
    PerfTools::Shutdown();
  }
}

// ── Frame time recording ─────────────────────────────────────────────────

TEST_CASE("Phase8::Perf::FrameTime::RecordAndQuery", "[perf][phase8]") {
  PerfTools::Init();
  PerfTools::RecordFrameTime(16.667);
  PerfTools::RecordFrameTime(17.0);
  PerfTools::RecordFrameTime(16.0);

  auto stats = PerfTools::GetFrameStats();
  CHECK(stats.samples >= 3);
  CHECK(stats.avgMs > 0.0);
  CHECK(stats.minMs > 0.0);
  CHECK(stats.maxMs >= stats.minMs);
  PerfTools::Shutdown();
}

TEST_CASE("Phase8::Perf::FrameTime::FPSCalculation", "[perf][phase8]") {
  PerfTools::Init();
  // 16.667 ms ≈ 60 fps
  for (int i = 0; i < 10; ++i)
    PerfTools::RecordFrameTime(16.667);
  double fps = PerfTools::GetAverageFPS();
  CHECK(fps > 55.0);
  CHECK(fps < 65.0);
  PerfTools::Shutdown();
}

TEST_CASE("Phase8::Perf::FrameTime::RollingWindow", "[perf][phase8]") {
  PerfTools::Init();
  // Fill beyond the rolling window
  for (int i = 0; i < 500; ++i)
    PerfTools::RecordFrameTime(16.667);
  auto stats = PerfTools::GetFrameStats();
  CHECK(stats.samples <= PerfTools::FrameWindowSize());
  PerfTools::Shutdown();
}

// ── Scope timer ───────────────────────────────────────────────────────────

TEST_CASE("Phase8::Perf::ScopeTimer::MeasuresPositiveDuration",
          "[perf][phase8]") {
  PerfTools::Init();
  {
    PerfTools::ScopeTimer t("TestSection");
    volatile int x = 0;
    for (int i = 0; i < 1000; ++i)
      x += i;
    (void)x;
  }
  auto sections = PerfTools::GetSectionStats();
  bool found = false;
  for (auto &s : sections) {
    if (s.name == "TestSection") {
      CHECK(s.totalMs >= 0.0);
      CHECK(s.callCount >= 1);
      found = true;
    }
  }
  CHECK(found);
  PerfTools::Shutdown();
}

TEST_CASE("Phase8::Perf::ScopeTimer::MultipleSections", "[perf][phase8]") {
  PerfTools::Init();
  for (int i = 0; i < 5; ++i) {
    PerfTools::ScopeTimer t("A");
  }
  for (int i = 0; i < 3; ++i) {
    PerfTools::ScopeTimer t("B");
  }
  auto sections = PerfTools::GetSectionStats();
  bool foundA = false, foundB = false;
  for (auto &s : sections) {
    if (s.name == "A") {
      CHECK(s.callCount == 5);
      foundA = true;
    }
    if (s.name == "B") {
      CHECK(s.callCount == 3);
      foundB = true;
    }
  }
  CHECK(foundA);
  CHECK(foundB);
  PerfTools::Shutdown();
}

// ── Interpreter hot-path throughput ──────────────────────────────────────

TEST_CASE("Phase8::Perf::Interpreter::NOPThroughput", "[perf][phase8]") {
  // Build a 1000-NOP program ending with HLT
  std::vector<uint8_t> code(1000, 0x90);
  code.push_back(0xF4); // HLT

  Cpu::Init();
  Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code.data());
  Cpu::ResetStats();

  auto t0 = Clock::now();
  for (int i = 0; i < 1001; ++i) {
    auto r = Cpu::Step();
    if (r == Cpu::StepResult::Halt)
      break;
  }
  auto t1 = Clock::now();
  double ms = Ms(t1 - t0).count();

  auto stats = Cpu::GetStats();
  CHECK(stats.instructionsExecuted == 1000);
  // Must complete in under 500 ms (generous for debug builds)
  CHECK(ms < 500.0);

  Cpu::Shutdown();
}

TEST_CASE("Phase8::Perf::Interpreter::ALUThroughput", "[perf][phase8]") {
  // 500x (ADD rax, rcx) then HLT
  // ADD rax,rcx encoding: 48 03 C1  (REX.W + 03 /r, ModRM=11 000 001)
  std::vector<uint8_t> code;
  for (int i = 0; i < 500; ++i) {
    code.insert(code.end(), {0x48, 0x03, 0xC1});
  }
  code.push_back(0xF4); // HLT

  Cpu::Init();
  Cpu::GetContext().gpr_set(Cpu::Reg::RAX, 0);
  Cpu::GetContext().gpr_set(Cpu::Reg::RCX, 1);
  Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code.data());
  Cpu::ResetStats();

  auto t0 = Clock::now();
  for (int i = 0; i <= 501; ++i) {
    auto r = Cpu::Step();
    if (r == Cpu::StepResult::Halt)
      break;
  }
  auto t1 = Clock::now();
  double ms = Ms(t1 - t0).count();

  CHECK(Cpu::GetContext().gpr_get(Cpu::Reg::RAX) == 500);
  CHECK(ms < 1000.0);

  Cpu::Shutdown();
}

// ── Memory allocation overhead ────────────────────────────────────────────

TEST_CASE("Phase8::Perf::Memory::AllocFree100", "[perf][phase8]") {
  Memory::Init();
  constexpr int N = 100;
  std::vector<void *> ptrs;
  ptrs.reserve(N);

  auto t0 = Clock::now();
  for (int i = 0; i < N; ++i) {
    ptrs.push_back(Memory::AllocHost(4096, Memory::AllocType::Heap));
  }
  for (void *p : ptrs)
    Memory::FreeHost(p);
  auto t1 = Clock::now();

  double ms = Ms(t1 - t0).count();
  CHECK(ms < 2000.0); // 100 alloc+free in under 2s
  Memory::Shutdown();
}

TEST_CASE("Phase8::Perf::Memory::StatsQueryOverhead", "[perf][phase8]") {
  Memory::Init();
  auto t0 = Clock::now();
  for (int i = 0; i < 1000; ++i) {
    auto s = Memory::GetStats();
    (void)s;
  }
  auto t1 = Clock::now();
  double ms = Ms(t1 - t0).count();
  CHECK(ms < 500.0);
  Memory::Shutdown();
}

// ── Command processing throughput ─────────────────────────────────────────

TEST_CASE("Phase8::Perf::CommandProcessor::100CommandLists", "[perf][phase8]") {
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);
  CommandProcessor::ResetStats();

  auto t0 = Clock::now();
  for (int i = 0; i < 100; ++i) {
    CommandList cl;
    cl.BeginRenderPass();
    cl.ClearColor(0, 0.0f, 0.0f, 0.0f, 1.0f);
    cl.DrawDirect(3, 1, 0, 0);
    cl.EndRenderPass();
    cl.End();
    CommandProcessor::Process(cl);
  }
  auto t1 = Clock::now();
  double ms = Ms(t1 - t0).count();

  auto s = CommandProcessor::GetStats();
  CHECK(s.commandListsProcessed >= 100);
  CHECK(ms < 2000.0);

  CommandProcessor::Shutdown();
  GPU::Shutdown();
}

TEST_CASE("Phase8::Perf::CommandProcessor::EmptyListOverhead",
          "[perf][phase8]") {
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);

  auto t0 = Clock::now();
  for (int i = 0; i < 1000; ++i) {
    CommandList cl;
    cl.End();
    CommandProcessor::Process(cl);
  }
  auto t1 = Clock::now();
  double ms = Ms(t1 - t0).count();
  CHECK(ms < 2000.0);

  CommandProcessor::Shutdown();
  GPU::Shutdown();
}

// ── Logger overhead ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Perf::Logger::1000LinesOverhead", "[perf][phase8]") {
  auto t0 = Clock::now();
  for (int i = 0; i < 1000; ++i) {
    PS5X_DEBUG("[Perf] Line %d", i);
  }
  auto t1 = Clock::now();
  double ms = Ms(t1 - t0).count();
  CHECK(ms < 2000.0);
}

TEST_CASE("Phase8::Perf::Logger::FilteredLinesNoOverhead", "[perf][phase8]") {
  Logger::SetLevel(Logger::Level::Error); // suppress Debug/Info/Warn
  auto t0 = Clock::now();
  for (int i = 0; i < 5000; ++i) {
    PS5X_DEBUG("[Perf] Suppressed line %d", i);
  }
  auto t1 = Clock::now();
  double ms = Ms(t1 - t0).count();
  CHECK(ms < 500.0); // suppressed = near-zero overhead
  Logger::SetLevel(Logger::Level::Debug);
}

// ── Startup time ──────────────────────────────────────────────────────────

TEST_CASE("Phase8::Perf::Startup::FullSubsystemInit", "[perf][phase8]") {
  auto t0 = Clock::now();
  Memory::Init();
  Cpu::Init();
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);
  PerfTools::Init();
  auto t1 = Clock::now();
  double ms = Ms(t1 - t0).count();

  CHECK(ms < 2000.0); // full init under 2s
  // Shutdown
  PerfTools::Shutdown();
  CommandProcessor::Shutdown();
  GPU::Shutdown();
  Cpu::Shutdown();
  Memory::Shutdown();
}

// ── Benchmark annotation ──────────────────────────────────────────────────

TEST_CASE("Phase8::Perf::Annotation::BeforeAfterMacro", "[perf][phase8]") {
  PerfTools::Init();
  PerfTools::BeginBenchmark("MyBench");
  volatile int x = 0;
  for (int i = 0; i < 10000; ++i)
    x += i;
  (void)x;
  double ms = PerfTools::EndBenchmark("MyBench");
  CHECK(ms >= 0.0);
  CHECK(ms < 1000.0);
  PerfTools::Shutdown();
}

TEST_CASE("Phase8::Perf::Annotation::MultipleNamedBenchmarks",
          "[perf][phase8]") {
  PerfTools::Init();
  std::vector<std::string> names = {"Alpha", "Beta", "Gamma", "Delta"};
  for (auto &n : names) {
    PerfTools::BeginBenchmark(n);
    volatile int x = 0;
    for (int i = 0; i < 100; i++)
      x += i;
    (void)x;
    double ms = PerfTools::EndBenchmark(n);
    CHECK(ms >= 0.0);
  }
  auto results = PerfTools::GetBenchmarkResults();
  for (auto &n : names) {
    bool found = false;
    for (auto &r : results)
      if (r.name == n) {
        found = true;
        break;
      }
    CHECK(found);
  }
  PerfTools::Shutdown();
}

// ── Stats reset ───────────────────────────────────────────────────────────

TEST_CASE("Phase8::Perf::Stats::ResetClearsAll", "[perf][phase8]") {
  PerfTools::Init();
  PerfTools::RecordFrameTime(16.0);
  { PerfTools::ScopeTimer t("Section"); }
  PerfTools::ResetAll();
  auto fs = PerfTools::GetFrameStats();
  CHECK(fs.samples == 0);
  auto ss = PerfTools::GetSectionStats();
  CHECK(ss.empty());
  PerfTools::Shutdown();
}
