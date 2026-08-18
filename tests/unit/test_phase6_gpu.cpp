// PS5x – Phase 6 GPU tests (queues, fences, barriers, stats)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/GPU/GPU.h"
#include <array>
#include <thread>
#include <chrono>

using namespace PS5x::GPU;

TEST_CASE("Phase6::GPU::CreateDestroyQueue_Graphics", "[gpu][phase6]")
{
    CmdQueueHandle h = CreateQueue(QueueType::Graphics);
    CHECK(h != INVALID_CMD_QUEUE);
    CHECK(DestroyQueue(h));
}

TEST_CASE("Phase6::GPU::CreateDestroyQueue_Compute", "[gpu][phase6]")
{
    CmdQueueHandle h = CreateQueue(QueueType::Compute);
    CHECK(h != INVALID_CMD_QUEUE);
    CHECK(DestroyQueue(h));
}

TEST_CASE("Phase6::GPU::CreateDestroyQueue_Transfer", "[gpu][phase6]")
{
    CmdQueueHandle h = CreateQueue(QueueType::Transfer);
    CHECK(h != INVALID_CMD_QUEUE);
    CHECK(DestroyQueue(h));
}

TEST_CASE("Phase6::GPU::DestroyInvalidQueue", "[gpu][phase6]")
{
    CHECK_FALSE(DestroyQueue(INVALID_CMD_QUEUE));
}

TEST_CASE("Phase6::GPU::MultipleQueues", "[gpu][phase6]")
{
    auto q1 = CreateQueue(QueueType::Graphics);
    auto q2 = CreateQueue(QueueType::Compute);
    auto q3 = CreateQueue(QueueType::Transfer);
    CHECK(q1 != q2);
    CHECK(q2 != q3);
    CHECK(q1 != q3);
    DestroyQueue(q1);
    DestroyQueue(q2);
    DestroyQueue(q3);
}

// ── Fence tests ────────────────────────────────────────────────────────────

TEST_CASE("Phase6::GPU::Fence::CreateDestroy", "[gpu][phase6]")
{
    FenceHandle h = CreateFence();
    CHECK(h != INVALID_FENCE);
    CHECK_FALSE(IsFenceSignalled(h));
    DestroyFence(h);
}

TEST_CASE("Phase6::GPU::Fence::SignalAndCheck", "[gpu][phase6]")
{
    FenceHandle h = CreateFence();
    CHECK_FALSE(IsFenceSignalled(h));
    CHECK(SignalFence(h));
    CHECK(IsFenceSignalled(h));
    DestroyFence(h);
}

TEST_CASE("Phase6::GPU::Fence::WaitAlreadySignalled", "[gpu][phase6]")
{
    FenceHandle h = CreateFence();
    SignalFence(h);
    CHECK(WaitFence(h, 0)); // should return immediately
    DestroyFence(h);
}

TEST_CASE("Phase6::GPU::Fence::WaitTimeout", "[gpu][phase6]")
{
    FenceHandle h = CreateFence();
    bool result = WaitFence(h, 1); // 1 µs – should timeout
    CHECK_FALSE(result);
    DestroyFence(h);
}

TEST_CASE("Phase6::GPU::Fence::ThreadedSignal", "[gpu][phase6]")
{
    FenceHandle h = CreateFence();
    std::atomic<bool> done{false};

    std::thread waiter([&]{
        bool ok = WaitFence(h, 2'000'000);
        CHECK(ok);
        done.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    SignalFence(h);
    waiter.join();
    CHECK(done.load());
    DestroyFence(h);
}

TEST_CASE("Phase6::GPU::Fence::InvalidSignal", "[gpu][phase6]")
{
    CHECK_FALSE(SignalFence(INVALID_FENCE));
}

TEST_CASE("Phase6::GPU::Fence::InvalidWait", "[gpu][phase6]")
{
    CHECK_FALSE(WaitFence(INVALID_FENCE, 0));
}

TEST_CASE("Phase6::GPU::Fence::InvalidIsSignalled", "[gpu][phase6]")
{
    CHECK_FALSE(IsFenceSignalled(INVALID_FENCE));
}

TEST_CASE("Phase6::GPU::Fence::MultipleSignals", "[gpu][phase6]")
{
    FenceHandle h = CreateFence();
    SignalFence(h);
    SignalFence(h); // idempotent
    CHECK(IsFenceSignalled(h));
    DestroyFence(h);
}

// ── Barrier tests ──────────────────────────────────────────────────────────

TEST_CASE("Phase6::GPU::Barrier::InsertSingle", "[gpu][phase6]")
{
    Barrier b;
    b.gpuAddr = 0x1000'0000;
    b.before  = ResourceState::RenderTarget;
    b.after   = ResourceState::ShaderRead;
    CHECK(InsertBarrier(b));
}

TEST_CASE("Phase6::GPU::Barrier::InsertMultiple", "[gpu][phase6]")
{
    Barrier barriers[3] = {
        {0x1000, ResourceState::Undefined,    ResourceState::TransferDst},
        {0x2000, ResourceState::TransferDst,  ResourceState::ShaderRead},
        {0x3000, ResourceState::ShaderRead,   ResourceState::Present},
    };
    CHECK(InsertBarriers(barriers, 3));
}

TEST_CASE("Phase6::GPU::Barrier::AllResourceStates", "[gpu][phase6]")
{
    using RS = ResourceState;
    std::array states = {RS::Undefined, RS::RenderTarget, RS::ShaderRead,
                         RS::TransferSrc, RS::TransferDst, RS::Present, RS::DepthWrite};
    for (auto s : states) {
        Barrier b{0xABCD, s, RS::Undefined};
        CHECK(InsertBarrier(b));
    }
}

// ── Stats tests ────────────────────────────────────────────────────────────

TEST_CASE("Phase6::GPU::Stats::BarrierCounted", "[gpu][phase6]")
{
    GpuStats before = GetGpuStats();
    Barrier b{0xFF00, ResourceState::Undefined, ResourceState::RenderTarget};
    InsertBarrier(b);
    GpuStats after = GetGpuStats();
    CHECK(after.barriers >= before.barriers + 1);
}

TEST_CASE("Phase6::GPU::Stats::FencesSignaledCounted", "[gpu][phase6]")
{
    GpuStats before = GetGpuStats();
    FenceHandle h = CreateFence();
    SignalFence(h);
    GpuStats after = GetGpuStats();
    CHECK(after.fencesSignaled >= before.fencesSignaled + 1);
    DestroyFence(h);
}

TEST_CASE("Phase6::GPU::Stats::ActiveQueues", "[gpu][phase6]")
{
    GpuStats before = GetGpuStats();
    auto q = CreateQueue(QueueType::Graphics);
    GpuStats after = GetGpuStats();
    CHECK(after.activeQueues >= before.activeQueues + 1);
    DestroyQueue(q);
    GpuStats fin = GetGpuStats();
    CHECK(fin.activeQueues == before.activeQueues);
}
