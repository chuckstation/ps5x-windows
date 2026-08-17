// PS5x – Kernel Runtime
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// Emulated PS5 kernel objects:
//   Handle table, Threads, Mutexes, Semaphores, Events, Timers, TLS
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PS5x::KernelRuntime
{

// ── Handle system ─────────────────────────────────────────────────────────
using KHandle = int32_t;
static constexpr KHandle INVALID_HANDLE = -1;

enum class KObjectType : uint8_t
{
	Unknown = 0,
	Thread = 1,
	Mutex = 2,
	Semaphore = 3,
	Event = 4,
	Timer = 5,
	File = 6,
	Socket = 7,
	Module = 8,
};

const char* KObjectTypeName(KObjectType t);

// ── Thread ────────────────────────────────────────────────────────────────
enum class ThreadState : uint8_t
{
	Created = 0,
	Running = 1,
	Sleeping = 2,
	Waiting = 3,
	Stopped = 4,
	Dead = 5,
};

using ThreadEntryFn = std::function<int(void* arg)>;

struct ThreadAttr
{
	std::string name;
	size_t stackSize = 256 * 1024; // 256 KiB default
	int priority = 700;            // PS5 priority (256–767)
	uint64_t affinityMask = 0xFF;  // all cores
};

struct ThreadInfo
{
	KHandle handle = INVALID_HANDLE;
	std::string name;
	ThreadState state = ThreadState::Created;
	int priority = 700;
	uint64_t stackBase = 0;
	size_t stackSize = 0;
	uint64_t tlsBase = 0;
	uint64_t startTime = 0; // microseconds
	int exitCode = 0;
};

// ── Synchronisation ───────────────────────────────────────────────────────
struct MutexAttr
{
	bool recursive = false;
	bool shared = false;
};

struct SemaphoreAttr
{
	int32_t initialValue = 0;
	int32_t maxValue = INT32_MAX;
};

struct EventAttr
{
	bool autoReset = false; ///< auto-clear after first waiter wakes
	bool initialSet = false;
};

// ── Timer ─────────────────────────────────────────────────────────────────
using TimerCallbackFn = std::function<void(KHandle timerId, void* arg)>;

struct TimerAttr
{
	uint64_t periodUs = 0; ///< 0 = one-shot
	bool autoStart = false;
};

// ── TLS ───────────────────────────────────────────────────────────────────
using TlsKey = uint32_t;
static constexpr TlsKey INVALID_TLS_KEY = UINT32_MAX;
using TlsDestructorFn = std::function<void(void*)>;

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();
void Reset(); ///< Destroy all kernel objects (between program runs)

// ── Handle table ─────────────────────────────────────────────────────────
KObjectType GetHandleType(KHandle h);
bool CloseHandle(KHandle h);

// ── Threads ───────────────────────────────────────────────────────────────
KHandle CreateThread(ThreadEntryFn fn, void* arg, const ThreadAttr& attr);
bool StartThread(KHandle h);
bool StopThread(KHandle h);
bool JoinThread(KHandle h, int* exitCode = nullptr, uint64_t timeoutUs = UINT64_MAX);
bool SetThreadPriority(KHandle h, int priority);
bool SetThreadAffinity(KHandle h, uint64_t mask);
ThreadInfo GetThreadInfo(KHandle h);
std::vector<ThreadInfo> GetAllThreads();
KHandle GetCurrentThreadHandle();

// ── Mutex ─────────────────────────────────────────────────────────────────
KHandle Lock(KHandle h); // convenience – lock + return same handle
KHandle CreateMutex(const MutexAttr& attr = {}, std::string_view name = "");
bool LockMutex(KHandle h);
bool TryLockMutex(KHandle h);
bool UnlockMutex(KHandle h);

// ── Semaphore ─────────────────────────────────────────────────────────────
KHandle CreateSemaphore(const SemaphoreAttr& attr, std::string_view name = "");
bool WaitSemaphore(KHandle h, uint64_t timeoutUs = UINT64_MAX);
bool SignalSemaphore(KHandle h, int32_t count = 1);
int32_t GetSemaphoreValue(KHandle h);

// ── Event ─────────────────────────────────────────────────────────────────
KHandle CreateEvent(const EventAttr& attr = {}, std::string_view name = "");
bool SetEvent(KHandle h);
bool ClearEvent(KHandle h);
bool WaitEvent(KHandle h, uint64_t timeoutUs = UINT64_MAX);

// ── Timer ─────────────────────────────────────────────────────────────────
KHandle CreateTimer(const TimerAttr& attr, TimerCallbackFn cb, void* arg = nullptr, std::string_view name = "");
bool StartTimer(KHandle h);
bool StopTimer(KHandle h);
bool CancelTimer(KHandle h);

// ── Thread-Local Storage ──────────────────────────────────────────────────
TlsKey TlsAlloc(TlsDestructorFn dtor = {});
bool TlsFree(TlsKey key);
bool TlsSet(TlsKey key, void* value);
void* TlsGet(TlsKey key);

// ── Statistics ────────────────────────────────────────────────────────────
struct KernelStats
{
	uint32_t totalThreads = 0;
	uint32_t runningThreads = 0;
	uint32_t totalMutexes = 0;
	uint32_t totalSemaphores = 0;
	uint32_t totalEvents = 0;
	uint32_t totalTimers = 0;
	uint32_t totalHandles = 0;
};
KernelStats GetStats();

// ── Phase 6: Object namespaces ────────────────────────────────────────────

struct NamespaceId
{
	uint32_t value = 0;
};
static constexpr NamespaceId ROOT_NS{0};

/// Register a kernel object under a name in a namespace.
bool RegisterName(std::string_view name, KHandle handle, NamespaceId ns = ROOT_NS);

/// Lookup a named kernel object. Returns INVALID_HANDLE if not found.
KHandle LookupName(std::string_view name, NamespaceId ns = ROOT_NS);

/// Remove a name registration.
bool UnregisterName(std::string_view name, NamespaceId ns = ROOT_NS);

// ── Phase 6: Wait queues ──────────────────────────────────────────────────

using WqHandle = int32_t;
static constexpr WqHandle INVALID_WQ = -1;

/// Create a wait queue (FIFO ordering).
WqHandle CreateWaitQueue(std::string_view name = "");

/// Enqueue the calling thread onto the wait queue (blocks until woken).
/// Returns false on timeout.
bool WaitOnQueue(WqHandle wq, uint64_t timeoutUs = UINT64_MAX);

/// Wake one thread waiting on the queue.
bool WakeOne(WqHandle wq);

/// Wake all threads waiting on the queue.
uint32_t WakeAll(WqHandle wq);

/// Destroy a wait queue (wakes all waiters with an error).
bool DestroyWaitQueue(WqHandle wq);

// ── Phase 6: Handle duplication ───────────────────────────────────────────

/// Duplicate a handle (increases reference count).
KHandle DuplicateHandle(KHandle src);

// ── Phase 6: Resource limits ──────────────────────────────────────────────

struct ResourceLimits
{
	size_t maxMemoryBytes = SIZE_MAX;
	uint32_t maxThreads = 512;
	uint32_t maxHandles = 65536;
	uint32_t maxOpenFiles = 1024;
};

void SetResourceLimits(const ResourceLimits& limits);
ResourceLimits GetResourceLimits();

// ── Phase 6: IPC foundations ──────────────────────────────────────────────

using IpcPortHandle = int32_t;
static constexpr IpcPortHandle INVALID_IPC_PORT = -1;

/// Create a named IPC port (server side).
IpcPortHandle CreateIpcPort(std::string_view name);

/// Connect to a named IPC port (client side).
IpcPortHandle ConnectIpcPort(std::string_view name);

/// Close an IPC port.
bool CloseIpcPort(IpcPortHandle h);

/// Send raw bytes to an IPC port (non-blocking if possible).
bool IpcSend(IpcPortHandle h, const void* data, size_t size);

/// Receive raw bytes from an IPC port.
/// Returns bytes received or -1 on error.
int64_t IpcRecv(IpcPortHandle h, void* buf, size_t bufSize, uint64_t timeoutUs = UINT64_MAX);

// ── Phase 8: Generic handle allocator ────────────────────────────────────
/// Allocate a handle of the given ObjectType. Returns a unique KHandle.
KHandle AllocHandle(KObjectType type);
/// Free a previously allocated handle.
void FreeHandle(KHandle h);

} // namespace PS5x::KernelRuntime
