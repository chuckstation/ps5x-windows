// ChuckStation5 – Phase 6 KernelRuntime tests (namespaces, wait queues, IPC, limits)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/KernelRuntime/KernelRuntime.h"
#include <thread>
#include <atomic>
#include <chrono>

using namespace ChuckStation5::KernelRuntime;

// ── Object namespace tests ─────────────────────────────────────────────────

TEST_CASE("Phase6::KR::ObjectNamespace::RegisterAndLookup", "[kernel_runtime][phase6]")
{
    // Register a dummy handle
    KHandle h = 42;
    CHECK(RegisterName("my_event", h));
    CHECK(LookupName("my_event") == h);
}

TEST_CASE("Phase6::KR::ObjectNamespace::LookupMissing", "[kernel_runtime][phase6]")
{
    CHECK(LookupName("no_such_object") == INVALID_HANDLE);
}

TEST_CASE("Phase6::KR::ObjectNamespace::UnregisterName", "[kernel_runtime][phase6]")
{
    RegisterName("to_remove", 99);
    CHECK(LookupName("to_remove") == 99);
    CHECK(UnregisterName("to_remove"));
    CHECK(LookupName("to_remove") == INVALID_HANDLE);
}

TEST_CASE("Phase6::KR::ObjectNamespace::SeparateNamespaces", "[kernel_runtime][phase6]")
{
    NamespaceId ns1{1}, ns2{2};
    RegisterName("shared_name", 10, ns1);
    RegisterName("shared_name", 20, ns2);
    CHECK(LookupName("shared_name", ns1) == 10);
    CHECK(LookupName("shared_name", ns2) == 20);
    // Should not appear in ROOT_NS
    CHECK(LookupName("shared_name") == INVALID_HANDLE);
}

TEST_CASE("Phase6::KR::ObjectNamespace::OverwriteHandle", "[kernel_runtime][phase6]")
{
    RegisterName("overwrite_me", 1);
    RegisterName("overwrite_me", 2);
    CHECK(LookupName("overwrite_me") == 2);
    UnregisterName("overwrite_me");
}

TEST_CASE("Phase6::KR::ObjectNamespace::UnregisterMissing", "[kernel_runtime][phase6]")
{
    CHECK_FALSE(UnregisterName("ghost_name"));
}

// ── Wait queue tests ───────────────────────────────────────────────────────

TEST_CASE("Phase6::KR::WaitQueue::CreateDestroy", "[kernel_runtime][phase6]")
{
    WqHandle wq = CreateWaitQueue("test_wq");
    CHECK(wq != INVALID_WQ);
    CHECK(DestroyWaitQueue(wq));
}

TEST_CASE("Phase6::KR::WaitQueue::DestroyInvalid", "[kernel_runtime][phase6]")
{
    CHECK_FALSE(DestroyWaitQueue(INVALID_WQ));
}

TEST_CASE("Phase6::KR::WaitQueue::WakeOne_noWaiters", "[kernel_runtime][phase6]")
{
    WqHandle wq = CreateWaitQueue("empty_wq");
    CHECK(WakeOne(wq));   // no waiters - should not crash
    CHECK(WakeAll(wq) == 0);
    DestroyWaitQueue(wq);
}

TEST_CASE("Phase6::KR::WaitQueue::WakeOneUnblocksWaiter", "[kernel_runtime][phase6]")
{
    WqHandle wq = CreateWaitQueue("wake_one_test");
    std::atomic<bool> woke{false};

    std::thread waiter([&]{
        WaitOnQueue(wq, 5'000'000); // 5 s timeout
        woke.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WakeOne(wq);
    waiter.join();
    CHECK(woke.load());
    DestroyWaitQueue(wq);
}

TEST_CASE("Phase6::KR::WaitQueue::WakeAllUnblocksMultiple", "[kernel_runtime][phase6]")
{
    WqHandle wq = CreateWaitQueue("wake_all_test");
    std::atomic<int> woke{0};

    auto worker = [&]{ WaitOnQueue(wq, 5'000'000); woke.fetch_add(1); };
    std::thread t1(worker), t2(worker), t3(worker);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    WakeAll(wq);
    t1.join(); t2.join(); t3.join();
    CHECK(woke.load() == 3);
    DestroyWaitQueue(wq);
}

TEST_CASE("Phase6::KR::WaitQueue::TimeoutReturnsfalse", "[kernel_runtime][phase6]")
{
    WqHandle wq = CreateWaitQueue("timeout_wq");
    bool result = WaitOnQueue(wq, 1); // 1 microsecond timeout
    CHECK_FALSE(result);
    DestroyWaitQueue(wq);
}

// ── Resource limits tests ──────────────────────────────────────────────────

TEST_CASE("Phase6::KR::ResourceLimits::DefaultLimits", "[kernel_runtime][phase6]")
{
    ResourceLimits lim = GetResourceLimits();
    CHECK(lim.maxThreads  > 0);
    CHECK(lim.maxHandles  > 0);
    CHECK(lim.maxOpenFiles > 0);
}

TEST_CASE("Phase6::KR::ResourceLimits::SetAndGet", "[kernel_runtime][phase6]")
{
    ResourceLimits custom;
    custom.maxMemoryBytes = 512 * 1024 * 1024ULL;
    custom.maxThreads     = 64;
    custom.maxHandles     = 4096;
    custom.maxOpenFiles   = 128;
    SetResourceLimits(custom);

    ResourceLimits got = GetResourceLimits();
    CHECK(got.maxMemoryBytes == custom.maxMemoryBytes);
    CHECK(got.maxThreads     == custom.maxThreads);
    CHECK(got.maxHandles     == custom.maxHandles);
    CHECK(got.maxOpenFiles   == custom.maxOpenFiles);

    // Restore defaults
    ResourceLimits def;
    SetResourceLimits(def);
}

// ── IPC tests ─────────────────────────────────────────────────────────────

TEST_CASE("Phase6::KR::IPC::CreateAndClose", "[kernel_runtime][phase6]")
{
    IpcPortHandle h = CreateIpcPort("test.port.create");
    CHECK(h != INVALID_IPC_PORT);
    CHECK(CloseIpcPort(h));
}

TEST_CASE("Phase6::KR::IPC::ConnectAfterCreate", "[kernel_runtime][phase6]")
{
    IpcPortHandle srv = CreateIpcPort("test.port.connect");
    IpcPortHandle cli = ConnectIpcPort("test.port.connect");
    CHECK(srv != INVALID_IPC_PORT);
    CHECK(cli != INVALID_IPC_PORT);
    CHECK(cli != srv);
    CloseIpcPort(cli);
    CloseIpcPort(srv);
}

TEST_CASE("Phase6::KR::IPC::ConnectMissing", "[kernel_runtime][phase6]")
{
    IpcPortHandle h = ConnectIpcPort("no.such.port");
    CHECK(h == INVALID_IPC_PORT);
}

TEST_CASE("Phase6::KR::IPC::SendRecv", "[kernel_runtime][phase6]")
{
    IpcPortHandle srv = CreateIpcPort("test.port.sendrecv");
    IpcPortHandle cli = ConnectIpcPort("test.port.sendrecv");

    const char msg[] = "hello ipc";
    CHECK(IpcSend(cli, msg, sizeof(msg)));

    char buf[64] = {};
    int64_t n = IpcRecv(srv, buf, sizeof(buf), 1'000'000);
    CHECK(n == static_cast<int64_t>(sizeof(msg)));
    CHECK(std::string(buf) == "hello ipc");

    CloseIpcPort(cli);
    CloseIpcPort(srv);
}

TEST_CASE("Phase6::KR::IPC::MultiMessage", "[kernel_runtime][phase6]")
{
    IpcPortHandle srv = CreateIpcPort("test.port.multi");
    IpcPortHandle cli = ConnectIpcPort("test.port.multi");

    for (int i = 0; i < 5; ++i) {
        uint32_t val = static_cast<uint32_t>(i);
        IpcSend(cli, &val, sizeof(val));
    }
    for (int i = 0; i < 5; ++i) {
        uint32_t got = 0;
        int64_t n = IpcRecv(srv, &got, sizeof(got), 100'000);
        CHECK(n == sizeof(got));
        CHECK(got == static_cast<uint32_t>(i));
    }
    CloseIpcPort(cli);
    CloseIpcPort(srv);
}

TEST_CASE("Phase6::KR::IPC::RecvTimeout", "[kernel_runtime][phase6]")
{
    IpcPortHandle srv = CreateIpcPort("test.port.timeout");
    int64_t n = IpcRecv(srv, nullptr, 0, 1); // 1 µs
    CHECK(n == -1);
    CloseIpcPort(srv);
}

TEST_CASE("Phase6::KR::IPC::CloseInvalidPort", "[kernel_runtime][phase6]")
{
    CHECK_FALSE(CloseIpcPort(INVALID_IPC_PORT));
}

TEST_CASE("Phase6::KR::IPC::SendReceive_Thread", "[kernel_runtime][phase6]")
{
    IpcPortHandle srv = CreateIpcPort("test.port.thread");
    IpcPortHandle cli = ConnectIpcPort("test.port.thread");

    std::atomic<bool> done{false};
    std::thread receiver([&]{
        char buf[32] = {};
        int64_t n = IpcRecv(srv, buf, sizeof(buf), 2'000'000);
        CHECK(n > 0);
        done.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const char data[] = "async msg";
    IpcSend(cli, data, sizeof(data));
    receiver.join();
    CHECK(done.load());

    CloseIpcPort(cli);
    CloseIpcPort(srv);
}
