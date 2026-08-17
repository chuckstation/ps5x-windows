// PS5x – Phase 8 Concurrency / Thread-safety tests
// SPDX-License-Identifier: MIT
//
// Tests that critical subsystems are safe under concurrent access.
#include "PS5x/Cpu/Cpu.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/PerfTools/PerfTools.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"
#include "PS5x/Syscalls/Syscalls.h"
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace PS5x;

// ── Logger thread-safety ──────────────────────────────────────────────────

TEST_CASE("Phase8::Concurrency::Logger::ConcurrentWrites",
          "[concurrency][phase8]") {
  constexpr int THREADS = 4;
  constexpr int PER_THREAD = 100;
  std::atomic<int> done{0};

  auto worker = [&](int id) {
    for (int i = 0; i < PER_THREAD; ++i) {
      PS5X_DEBUG("[Thread %d] Line %d", id, i);
      PS5X_INFO("[Thread %d] Info %d", id, i);
    }
    ++done;
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < THREADS; ++i)
    threads.emplace_back(worker, i);
  for (auto &t : threads)
    t.join();

  CHECK(done.load() == THREADS); // all finished without crash
}

// ── RuntimeEvents thread-safety ───────────────────────────────────────────

TEST_CASE("Phase8::Concurrency::RuntimeEvents::ConcurrentPublish",
          "[concurrency][phase8]") {
  RuntimeEvents::Init();
  std::atomic<int> received{0};

  RuntimeEvents::Subscribe(
      [&](const RuntimeEvents::RuntimeEvent &) { ++received; },
      RuntimeEvents::EventType::FrameEnd);

  constexpr int THREADS = 4;
  constexpr int PER_THREAD = 50;

  auto publisher = [&]() {
    for (int i = 0; i < PER_THREAD; ++i) {
      RuntimeEvents::Publish(RuntimeEvents::EventType::FrameEnd, {});
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < THREADS; ++i)
    threads.emplace_back(publisher);
  for (auto &t : threads)
    t.join();

  CHECK(received.load() == THREADS * PER_THREAD);
  RuntimeEvents::Shutdown();
}

TEST_CASE("Phase8::Concurrency::RuntimeEvents::ConcurrentSubscribePublish",
          "[concurrency][phase8]") {
  RuntimeEvents::Init();
  std::atomic<bool> crashed{false};

  auto publisher = [&]() {
    for (int i = 0; i < 50; ++i) {
      try {
        RuntimeEvents::Publish(RuntimeEvents::EventType::FrameEnd, {});
      } catch (...) {
        crashed = true;
      }
    }
  };

  auto subscriber = [&]() {
    for (int i = 0; i < 10; ++i) {
      try {
        RuntimeEvents::Subscribe([](const RuntimeEvents::RuntimeEvent &) {},
                                 RuntimeEvents::EventType::ProcessStarted);
      } catch (...) {
        crashed = true;
      }
    }
  };

  std::thread t1(publisher), t2(subscriber), t3(publisher);
  t1.join();
  t2.join();
  t3.join();

  CHECK(!crashed.load());
  RuntimeEvents::Shutdown();
}

// ── Memory thread-safety ──────────────────────────────────────────────────

TEST_CASE("Phase8::Concurrency::Memory::ConcurrentAllocFree",
          "[concurrency][phase8]") {
  Memory::Init();
  std::atomic<bool> crashed{false};

  auto worker = [&](int id) {
    try {
      std::vector<void *> ptrs;
      for (int i = 0; i < 20; ++i) {
        void *p = Memory::AllocHost(256 * (id + 1), Memory::AllocType::Heap);
        if (p)
          ptrs.push_back(p);
      }
      for (void *p : ptrs)
        Memory::FreeHost(p);
    } catch (...) {
      crashed = true;
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
    threads.emplace_back(worker, i);
  for (auto &t : threads)
    t.join();

  CHECK(!crashed.load());
  Memory::Shutdown();
}

// ── CPU stats thread-safety ────────────────────────────────────────────────

TEST_CASE("Phase8::Concurrency::Cpu::StatsQueryWhileRunning",
          "[concurrency][phase8]") {
  Cpu::Init();
  std::atomic<bool> done{false};
  std::atomic<bool> crashed{false};

  // Reader thread polls stats
  auto reader = [&]() {
    while (!done.load()) {
      try {
        auto s = Cpu::GetStats();
        (void)s;
        std::this_thread::yield();
      } catch (...) {
        crashed = true;
      }
    }
  };

  // Writer thread executes NOPs
  alignas(16) uint8_t nops[10];
  std::fill(std::begin(nops), std::end(nops), 0x90);
  nops[9] = 0xF4; // HLT

  std::thread rt(reader);
  Cpu::GetContext().rip = reinterpret_cast<uint64_t>(nops);
  for (int i = 0; i < 9; ++i) {
    Cpu::Step();
    std::this_thread::yield();
  }
  done = true;
  rt.join();

  CHECK(!crashed.load());
  Cpu::Shutdown();
}

// ── Breakpoint thread-safety ───────────────────────────────────────────────

TEST_CASE("Phase8::Concurrency::Cpu::ConcurrentBreakpointAddRemove",
          "[concurrency][phase8]") {
  Cpu::Init();
  std::atomic<bool> crashed{false};

  auto adder = [&]() {
    try {
      for (uint64_t addr = 0x1000; addr < 0x1020; ++addr) {
        Cpu::AddBreakpoint(addr, "concurrent");
      }
    } catch (...) {
      crashed = true;
    }
  };

  auto remover = [&]() {
    try {
      for (uint32_t id = 1; id < 20; ++id) {
        Cpu::RemoveBreakpoint(id);
      }
    } catch (...) {
      crashed = true;
    }
  };

  std::thread t1(adder), t2(remover);
  t1.join();
  t2.join();

  CHECK(!crashed.load());
  Cpu::Shutdown();
}

// ── PerfTools thread-safety ───────────────────────────────────────────────

TEST_CASE("Phase8::Concurrency::PerfTools::ConcurrentFrameTime",
          "[concurrency][phase8]") {
  PerfTools::Init();
  std::atomic<bool> crashed{false};

  auto worker = [&]() {
    try {
      for (int i = 0; i < 100; ++i) {
        PerfTools::RecordFrameTime(16.667 + i * 0.001);
      }
    } catch (...) {
      crashed = true;
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
    threads.emplace_back(worker);
  for (auto &t : threads)
    t.join();

  CHECK(!crashed.load());
  auto stats = PerfTools::GetFrameStats();
  CHECK(stats.samples > 0);

  PerfTools::Shutdown();
}

TEST_CASE("Phase8::Concurrency::PerfTools::ConcurrentScopeTimers",
          "[concurrency][phase8]") {
  PerfTools::Init();
  std::atomic<bool> crashed{false};

  auto worker = [&](int id) {
    try {
      for (int i = 0; i < 50; ++i) {
        PerfTools::ScopeTimer t("Section_" + std::to_string(id % 3));
        volatile int x = 0;
        for (int j = 0; j < 10; ++j)
          x += j;
        (void)x;
      }
    } catch (...) {
      crashed = true;
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
    threads.emplace_back(worker, i);
  for (auto &t : threads)
    t.join();

  CHECK(!crashed.load());
  PerfTools::Shutdown();
}

// ── Syscall dispatcher thread-safety ──────────────────────────────────────

TEST_CASE("Phase8::Concurrency::Syscall::ConcurrentDispatch",
          "[concurrency][phase8]") {
  Syscalls::Init();
  Syscalls::RegisterBuiltins();
  std::atomic<bool> crashed{false};

  auto worker = [&]() {
    try {
      for (int i = 0; i < 50; ++i) {
        Cpu::CpuContext ctx{};
        ctx.gpr_set(Cpu::Reg::RAX, Syscalls::Nr::GetPid);
        Syscalls::Dispatch(ctx);
      }
    } catch (...) {
      crashed = true;
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
    threads.emplace_back(worker);
  for (auto &t : threads)
    t.join();

  CHECK(!crashed.load());
  Syscalls::Shutdown();
}
