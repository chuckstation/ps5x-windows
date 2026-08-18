// ChuckStation5 – KernelRuntime unit tests (Phase 3)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/KernelRuntime/KernelRuntime.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace ChuckStation5::KernelRuntime;

static void Setup()
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();
    Init();
}
static void Teardown()
{
    Shutdown();
    ChuckStation5::Memory::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

// ── Handle table ──────────────────────────────────────────────────────────

TEST_CASE("KernelRuntime – INVALID_HANDLE yields Unknown type", "[kr]")
{
    Setup();
    REQUIRE(GetHandleType(INVALID_HANDLE) == KObjectType::Unknown);
    Teardown();
}

TEST_CASE("KernelRuntime – CloseHandle on nonexistent returns false", "[kr]")
{
    Setup();
    REQUIRE(!CloseHandle(999));
    Teardown();
}

// ── Mutex ─────────────────────────────────────────────────────────────────

TEST_CASE("KernelRuntime – Mutex create / lock / unlock", "[kr]")
{
    Setup();
    auto h = CreateMutex({}, "test-mtx");
    REQUIRE(h != INVALID_HANDLE);
    REQUIRE(GetHandleType(h) == KObjectType::Mutex);

    REQUIRE(LockMutex(h));
    REQUIRE(UnlockMutex(h));
    REQUIRE(TryLockMutex(h));
    REQUIRE(UnlockMutex(h));

    REQUIRE(CloseHandle(h));
    Teardown();
}

TEST_CASE("KernelRuntime – Recursive mutex", "[kr]")
{
    Setup();
    MutexAttr attr; attr.recursive = true;
    auto h = CreateMutex(attr, "recursive");
    REQUIRE(LockMutex(h));
    REQUIRE(LockMutex(h)); // recursive – must not deadlock
    REQUIRE(UnlockMutex(h));
    REQUIRE(UnlockMutex(h));
    REQUIRE(CloseHandle(h));
    Teardown();
}

// ── Semaphore ─────────────────────────────────────────────────────────────

TEST_CASE("KernelRuntime – Semaphore signal / wait", "[kr]")
{
    Setup();
    SemaphoreAttr sa; sa.initialValue = 0; sa.maxValue = 10;
    auto h = CreateSemaphore(sa, "test-sem");
    REQUIRE(h != INVALID_HANDLE);
    REQUIRE(GetSemaphoreValue(h) == 0);

    REQUIRE(SignalSemaphore(h, 3));
    REQUIRE(GetSemaphoreValue(h) == 3);

    REQUIRE(WaitSemaphore(h, 0));  // immediate, value=2
    REQUIRE(GetSemaphoreValue(h) == 2);

    REQUIRE(CloseHandle(h));
    Teardown();
}

TEST_CASE("KernelRuntime – Semaphore wait timeout", "[kr]")
{
    Setup();
    SemaphoreAttr sa; sa.initialValue = 0;
    auto h = CreateSemaphore(sa, "timeout-sem");
    // Should time out immediately (value=0, timeout=1us)
    REQUIRE(!WaitSemaphore(h, 1));
    REQUIRE(CloseHandle(h));
    Teardown();
}

// ── Event ─────────────────────────────────────────────────────────────────

TEST_CASE("KernelRuntime – Event set / wait / clear", "[kr]")
{
    Setup();
    EventAttr ea; ea.autoReset = false; ea.initialSet = false;
    auto h = CreateEvent(ea, "test-ev");
    REQUIRE(h != INVALID_HANDLE);

    // Not set yet – should timeout
    REQUIRE(!WaitEvent(h, 1));

    REQUIRE(SetEvent(h));
    REQUIRE(WaitEvent(h, 0)); // set, should pass immediately

    REQUIRE(ClearEvent(h));
    REQUIRE(!WaitEvent(h, 1)); // cleared again

    REQUIRE(CloseHandle(h));
    Teardown();
}

TEST_CASE("KernelRuntime – Auto-reset event clears after first wait", "[kr]")
{
    Setup();
    EventAttr ea; ea.autoReset = true; ea.initialSet = true;
    auto h = CreateEvent(ea, "autoreset");
    REQUIRE(WaitEvent(h, 0));   // first waiter gets it and clears
    REQUIRE(!WaitEvent(h, 1));  // now cleared
    REQUIRE(CloseHandle(h));
    Teardown();
}

// ── Thread ────────────────────────────────────────────────────────────────

TEST_CASE("KernelRuntime – Thread create / start / join", "[kr]")
{
    Setup();
    std::atomic<int> counter{0};

    ThreadAttr attr; attr.name = "test-thread"; attr.stackSize = 64*1024;
    auto h = CreateThread([&counter](void*) -> int {
        counter.fetch_add(1);
        return 42;
    }, nullptr, attr);

    REQUIRE(h != INVALID_HANDLE);
    REQUIRE(GetHandleType(h) == KObjectType::Thread);

    auto info = GetThreadInfo(h);
    REQUIRE(info.name == "test-thread");
    REQUIRE(info.state == ThreadState::Created);

    REQUIRE(StartThread(h));

    int exitCode = 0;
    REQUIRE(JoinThread(h, &exitCode, 5'000'000)); // 5s timeout
    REQUIRE(exitCode == 42);
    REQUIRE(counter.load() == 1);

    auto info2 = GetThreadInfo(h);
    REQUIRE(info2.state == ThreadState::Dead);

    Teardown();
}

TEST_CASE("KernelRuntime – GetAllThreads lists created threads", "[kr]")
{
    Setup();
    ThreadAttr a; a.name = "t1"; a.stackSize = 32*1024;
    ThreadAttr b; a.name = "t2"; b.stackSize = 32*1024;
    auto h1 = CreateThread([](void*){ return 0; }, nullptr, a);
    auto h2 = CreateThread([](void*){ return 0; }, nullptr, b);

    auto threads = GetAllThreads();
    REQUIRE(threads.size() >= 2);

    CloseHandle(h1);
    CloseHandle(h2);
    Teardown();
}

// ── TLS ───────────────────────────────────────────────────────────────────

TEST_CASE("KernelRuntime – TLS alloc / set / get from thread", "[kr]")
{
    Setup();
    TlsKey k = TlsAlloc();
    REQUIRE(k != INVALID_TLS_KEY);

    std::atomic<void*> retrieved{nullptr};

    ThreadAttr attr; attr.name = "tls-thread"; attr.stackSize = 64*1024;
    auto h = CreateThread([&](void*) -> int {
        TlsSet(k, reinterpret_cast<void*>(0xDEADBEEF));
        retrieved.store(TlsGet(k));
        return 0;
    }, nullptr, attr);

    StartThread(h);
    JoinThread(h, nullptr, 5'000'000);
    REQUIRE(retrieved.load() == reinterpret_cast<void*>(0xDEADBEEF));

    REQUIRE(TlsFree(k));
    Teardown();
}

// ── Timer ─────────────────────────────────────────────────────────────────

TEST_CASE("KernelRuntime – One-shot timer fires callback", "[kr]")
{
    Setup();
    std::atomic<int> fired{0};

    TimerAttr ta; ta.periodUs = 0; ta.autoStart = false; // one-shot
    auto h = CreateTimer(ta, [&fired](KHandle, void*){ fired.fetch_add(1); },
                         nullptr, "test-timer");
    REQUIRE(h != INVALID_HANDLE);
    REQUIRE(StartTimer(h));

    // Give it time to fire
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(fired.load() >= 1);

    REQUIRE(StopTimer(h));
    CloseHandle(h);
    Teardown();
}

// ── Stats ─────────────────────────────────────────────────────────────────

TEST_CASE("KernelRuntime – Stats count handles correctly", "[kr]")
{
    Setup();
    auto s0 = GetStats();
    REQUIRE(s0.totalHandles == 0);

    auto mx = CreateMutex({}, "s-mtx");
    auto sm = CreateSemaphore({}, "s-sem");
    auto ev = CreateEvent({}, "s-ev");

    auto s1 = GetStats();
    REQUIRE(s1.totalMutexes    == 1);
    REQUIRE(s1.totalSemaphores == 1);
    REQUIRE(s1.totalEvents     == 1);
    REQUIRE(s1.totalHandles    == 3);

    CloseHandle(mx); CloseHandle(sm); CloseHandle(ev);

    auto s2 = GetStats();
    REQUIRE(s2.totalHandles == 0);
    Teardown();
}
