// PS5x – Kernel Services tests (Phase 4)
// SPDX-License-Identifier: MIT
#include "PS5x/KernelRuntime/KernelRuntime.h"
#include "PS5x/KernelServices/KernelServices.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstring>
#include <thread>

using namespace PS5x::KernelServices;

static void Setup() {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  PS5x::Memory::Init();
  PS5x::KernelRuntime::Init();
  Init();
}
static void Teardown() {
  Shutdown();
  PS5x::KernelRuntime::Shutdown();
  PS5x::Memory::Shutdown();
  PS5x::Logger::Shutdown();
}

// ── Shared memory ──────────────────────────────────────────────────────────

TEST_CASE("KSvc – CreateShm / MapShm / CloseShm", "[ksvc]") {
  Setup();
  auto h = CreateShm("test-shm", 4096);
  REQUIRE(h != INVALID_SHM);

  auto *ptr = static_cast<uint8_t *>(MapShm(h));
  REQUIRE(ptr != nullptr);

  std::memset(ptr, 0xAB, 4096);
  REQUIRE(ptr[0] == 0xAB);
  REQUIRE(ptr[4095] == 0xAB);

  auto desc = GetShmDesc(h);
  REQUIRE(desc.has_value());
  REQUIRE(desc->size == 4096);
  REQUIRE(desc->refCount == 1);

  REQUIRE(CloseShm(h));
  Teardown();
}

TEST_CASE("KSvc – OpenShm shares same memory", "[ksvc]") {
  Setup();
  auto h1 = CreateShm("shared", 512);
  REQUIRE(h1 != INVALID_SHM);

  auto h2 = OpenShm("shared");
  REQUIRE(h2 == h1); // same handle

  auto desc = GetShmDesc(h1);
  REQUIRE(desc->refCount == 2);

  REQUIRE(CloseShm(h2));
  desc = GetShmDesc(h1);
  REQUIRE(desc->refCount == 1);

  REQUIRE(CloseShm(h1));
  REQUIRE(!GetShmDesc(h1).has_value());
  Teardown();
}

// ── Message queue ──────────────────────────────────────────────────────────

TEST_CASE("KSvc – CreateMq / SendMsg / RecvMsg", "[ksvc]") {
  Setup();
  MqAttr attr;
  attr.maxMessages = 8;
  attr.maxMsgSize = 64;
  auto h = CreateMq("test-mq", attr);
  REQUIRE(h != INVALID_MQ);
  REQUIRE(MqDepth(h) == 0);

  const char *msg = "Hello PS5x";
  REQUIRE(SendMsg(h, msg, std::strlen(msg) + 1, 0, 0));
  REQUIRE(MqDepth(h) == 1);

  char buf[64]{};
  size_t got = 0;
  REQUIRE(RecvMsg(h, buf, sizeof(buf), &got, nullptr, 0));
  REQUIRE(got == std::strlen(msg) + 1);
  REQUIRE(std::string(buf) == "Hello PS5x");
  REQUIRE(MqDepth(h) == 0);

  REQUIRE(CloseMq(h));
  Teardown();
}

TEST_CASE("KSvc – MQ priority ordering", "[ksvc]") {
  Setup();
  MqAttr attr;
  attr.maxMessages = 4;
  attr.maxMsgSize = 8;
  auto h = CreateMq("prio-mq", attr);

  SendMsg(h, "low", 4, 1, 0);
  SendMsg(h, "high", 5, 9, 0); // higher priority
  SendMsg(h, "mid", 4, 5, 0);

  char buf[8]{};
  uint32_t prio = 0;
  // Highest priority (9) should come first
  RecvMsg(h, buf, sizeof(buf), nullptr, &prio, 0);
  REQUIRE(prio == 9);
  REQUIRE(std::string(buf, 4) == "high");

  CloseMq(h);
  Teardown();
}

TEST_CASE("KSvc – RecvMsg timeout on empty queue", "[ksvc]") {
  Setup();
  auto h = CreateMq("empty-mq", {});
  char buf[16]{};
  REQUIRE(!RecvMsg(h, buf, sizeof(buf), nullptr, nullptr, 1)); // 1us timeout
  CloseMq(h);
  Teardown();
}

// ── Spinlock ──────────────────────────────────────────────────────────────

TEST_CASE("KSvc – Spinlock lock/unlock", "[ksvc]") {
  Setup();
  Spinlock sl;
  sl.lock();
  REQUIRE(!sl.try_lock()); // already locked
  sl.unlock();
  REQUIRE(sl.try_lock());
  sl.unlock();
  Teardown();
}

TEST_CASE("KSvc – Spinlock protects shared counter", "[ksvc]") {
  Setup();
  Spinlock sl;
  int counter = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 100; ++j) {
        sl.lock();
        ++counter;
        sl.unlock();
      }
    });
  }
  for (auto &t : threads)
    t.join();
  REQUIRE(counter == 400);
  Teardown();
}

// ── RW Lock ───────────────────────────────────────────────────────────────

TEST_CASE("KSvc – RwLock read/write", "[ksvc]") {
  Setup();
  auto h = CreateRwLock("test-rw");
  REQUIRE(h != INVALID_RW);

  REQUIRE(RdLock(h));
  REQUIRE(TryRdLock(h)); // multiple readers OK
  REQUIRE(RdUnlock(h));
  REQUIRE(RdUnlock(h));

  REQUIRE(WrLock(h));
  REQUIRE(!TryRdLock(h)); // writer holds, reader should fail
  REQUIRE(WrUnlock(h));

  REQUIRE(TryWrLock(h));
  REQUIRE(WrUnlock(h));

  REQUIRE(DestroyRwLock(h));
  Teardown();
}

// ── Condition variable ─────────────────────────────────────────────────────

TEST_CASE("KSvc – CondVar signal wakes waiter", "[ksvc]") {
  Setup();
  auto cv = CreateCondVar("test-cv");
  REQUIRE(cv != INVALID_CV);

  std::atomic<bool> ready{false};
  std::atomic<bool> woken{false};

  auto mx = PS5x::KernelRuntime::CreateMutex({}, "cv-mtx");

  std::thread waiter([&]() {
    ready.store(true);
    // WaitCondVar with 2s timeout
    WaitCondVar(cv, mx, 2'000'000);
    woken.store(true);
  });

  // Wait for waiter to start
  while (!ready.load())
    std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  SignalCondVar(cv);
  waiter.join();
  REQUIRE(woken.load());

  REQUIRE(DestroyCondVar(cv));
  PS5x::KernelRuntime::CloseHandle(mx);
  Teardown();
}

TEST_CASE("KSvc – CondVar timeout returns false", "[ksvc]") {
  Setup();
  auto cv = CreateCondVar("timeout-cv");
  auto mx = PS5x::KernelRuntime::CreateMutex({}, "to-mtx");
  // 1us timeout – should not be signalled
  REQUIRE(!WaitCondVar(cv, mx, 1));
  DestroyCondVar(cv);
  PS5x::KernelRuntime::CloseHandle(mx);
  Teardown();
}

// ── Scheduler ─────────────────────────────────────────────────────────────

TEST_CASE("KSvc – SchedYield does not crash", "[ksvc]") {
  Setup();
  SchedYield();
  Teardown();
}

TEST_CASE("KSvc – Sleep microseconds", "[ksvc]") {
  Setup();
  auto t0 = std::chrono::steady_clock::now();
  Sleep(10'000); // 10ms
  auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
  REQUIRE(dt >= 8); // allow some OS timer slop
  Teardown();
}

// ── Stats ─────────────────────────────────────────────────────────────────

TEST_CASE("KSvc – GetStats reflects active objects", "[ksvc]") {
  Setup();
  auto s0 = GetStats();
  REQUIRE(s0.shmCount == 0);
  REQUIRE(s0.mqCount == 0);

  auto shm = CreateShm("stat-shm", 1024);
  auto mq = CreateMq("stat-mq", {});
  auto rw = CreateRwLock("stat-rw");
  auto cv = CreateCondVar("stat-cv");

  auto s1 = GetStats();
  REQUIRE(s1.shmCount == 1);
  REQUIRE(s1.mqCount == 1);
  REQUIRE(s1.rwCount == 1);
  REQUIRE(s1.cvCount == 1);

  CloseShm(shm);
  CloseMq(mq);
  DestroyRwLock(rw);
  DestroyCondVar(cv);

  auto s2 = GetStats();
  REQUIRE(s2.shmCount == 0);
  REQUIRE(s2.mqCount == 0);
  Teardown();
}
