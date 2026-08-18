// ChuckStation5 KernelServices
// ChuckStation5 – Kernel Services
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
// Additional kernel-level services beyond KernelRuntime:
//   Shared memory regions, message queues, lightweight spinlocks,
//   read-write locks, condition variables, scheduler interfaces.
#pragma once

#include <atomic>
#include <thread>

#include "ChuckStation5/KernelRuntime/KernelRuntime.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ChuckStation5::KernelServices {

// ── Shared memory ─────────────────────────────────────────────────────────
using ShmHandle = int32_t;
static constexpr ShmHandle INVALID_SHM = -1;

struct ShmDesc
{
    ShmHandle   handle   = INVALID_SHM;
    std::string name;
    size_t      size     = 0;
    void*       hostPtr  = nullptr;
    uint32_t    refCount = 0;
};

ShmHandle   CreateShm(std::string_view name, size_t size);
ShmHandle   OpenShm(std::string_view name);
bool        CloseShm(ShmHandle h);
void*       MapShm(ShmHandle h);
bool        UnmapShm(ShmHandle h);
std::optional<ShmDesc> GetShmDesc(ShmHandle h);

// ── Message queue ─────────────────────────────────────────────────────────
using MqHandle = int32_t;
static constexpr MqHandle INVALID_MQ = -1;

struct MqAttr
{
    uint32_t maxMessages = 32;
    uint32_t maxMsgSize  = 256;
};

MqHandle CreateMq(std::string_view name, const MqAttr& attr = {});
bool     CloseMq(MqHandle h);
bool     SendMsg(MqHandle h, const void* data, size_t size, uint32_t priority = 0,
                 uint64_t timeoutUs = UINT64_MAX);
bool     RecvMsg(MqHandle h, void* buf, size_t bufSize, size_t* bytesOut = nullptr,
                 uint32_t* priorityOut = nullptr, uint64_t timeoutUs = UINT64_MAX);
uint32_t MqDepth(MqHandle h);  ///< messages currently queued

// ── Spinlock (busy-wait, for very short critical sections) ─────────────────
struct Spinlock
{
    void lock()
    {
        while (_flag.exchange(true, std::memory_order_acquire))
            std::this_thread::yield();
    }
    bool try_lock()
    {
        return !_flag.exchange(true, std::memory_order_acquire);
    }
    void unlock()
    {
        _flag.store(false, std::memory_order_release);
    }
private:
    std::atomic<bool> _flag{false};
};

// ── Read-Write lock ────────────────────────────────────────────────────────
using RwHandle = int32_t;
static constexpr RwHandle INVALID_RW = -1;

RwHandle  CreateRwLock(std::string_view name = "");
bool      RdLock(RwHandle h, uint64_t timeoutUs = UINT64_MAX);
bool      WrLock(RwHandle h, uint64_t timeoutUs = UINT64_MAX);
bool      TryRdLock(RwHandle h);
bool      TryWrLock(RwHandle h);
bool      RdUnlock(RwHandle h);
bool      WrUnlock(RwHandle h);
bool      DestroyRwLock(RwHandle h);

// ── Condition variable (pairs with a KernelRuntime mutex) ─────────────────
using CvHandle = int32_t;
static constexpr CvHandle INVALID_CV = -1;

CvHandle  CreateCondVar(std::string_view name = "");
bool      WaitCondVar(CvHandle cv, KernelRuntime::KHandle mutex,
                      uint64_t timeoutUs = UINT64_MAX);
bool      SignalCondVar(CvHandle cv);
bool      BroadcastCondVar(CvHandle cv);
bool      DestroyCondVar(CvHandle cv);

// ── Scheduler interface (host-side only) ──────────────────────────────────

/// Yield the current host thread.
void     SchedYield();

/// Set a scheduler hint for the current thread's priority.
/// Values 256–767 (PS5 priority range); clamped and mapped to host.
bool     SchedSetPriority(KernelRuntime::KHandle threadHandle, int psPriority);

/// Sleep the calling thread for the given number of microseconds.
void     Sleep(uint64_t us);

/// Nanosecond sleep (higher resolution on supported platforms).
void     SleepNs(uint64_t ns);

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();

// ── Statistics ────────────────────────────────────────────────────────────
struct KernelServiceStats
{
    uint32_t shmCount  = 0;
    uint32_t mqCount   = 0;
    uint32_t rwCount   = 0;
    uint32_t cvCount   = 0;
};
KernelServiceStats GetStats();

} // namespace ChuckStation5::KernelServices
