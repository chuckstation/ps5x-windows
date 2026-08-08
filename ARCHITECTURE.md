# PS5x Architecture

Repository: [github.com/libaerto/ps5x-windows](https://github.com/libaerto/ps5x-windows) · Version: 1.0.0

## Overview

PS5x is structured as a layered set of independent subsystems. Each subsystem
exposes a clean C++ API via a header in `include/PS5x/<Subsystem>/` and is
implemented in `src/<Subsystem>/`. Subsystems communicate primarily through
the **RuntimeEvents** publish/subscribe bus and through direct API calls for
synchronous operations.

```
┌─────────────────────────────────────────────────────────────┐
│                        UI / Frontend                        │
│  WelcomeScreen · RecentList · FirmwareMgr · Dock · LogView  │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│              PerfTools / Debugger / Metrics                  │
│  FrameTime · ScopeTimer · Benchmarks · RegisterView · TL    │
│  Counters · Gauges · Histograms · Percentiles               │
└────────┬──────────────────┬───────────────────────┬─────────┘
         │                  │                       │
┌────────▼────────┐ ┌───────▼────────┐ ┌───────────▼────────┐
│   CPU (Interp)  │ │  GPU / CmdProc │ │   Audio / Input     │
│  x86-64 ISA     │ │  CommandList   │ │  Ports / Buffers    │
│  80+ opcodes    │ │  DX12/DX11/Vk  │ │  PadState / Inject  │
└────────┬────────┘ └───────┬────────┘ └─────────────────────┘
         │                  │
┌────────▼────────┐ ┌───────▼────────┐ ┌────────────────────┐
│  Syscall Disp.  │ │ ShaderCache    │ │ InputMapping       │
│  25+ syscalls   │ │ key→bytecode   │ │ Profiles · INI     │
└────────┬────────┘ └────────────────┘ └────────────────────┘
         │
┌────────▼────────────────────────────────────────────────────┐
│                   Kernel / KernelRuntime                    │
│   Process · Threads · Handles · KernelServices              │
└────────┬────────────────────────────────────────────────────┘
         │
┌────────▼────────────────────────────────────────────────────┐
│          Memory / MemoryDiag / Loader / SaveState            │
│   Host-backed guest address space · ELF64 loader            │
│   Allocation tracking · Leak detection · CRC32 checksums    │
└────────┬────────────────────────────────────────────────────┘
         │
┌────────▼────────────────────────────────────────────────────┐
│                    Runtime / RuntimeEvents                  │
│   Subsystem lifecycle · Publish-subscribe event bus         │
└─────────────────────────────────────────────────────────────┘
         │
┌────────▼────────────────────────────────────────────────────┐
│          Logger / Config / Filesystem / CrashHandler        │
│   Structured logging · INI config · Virtual mounts          │
│   SEH/signal handler · MiniDump generation                  │
└─────────────────────────────────────────────────────────────┘
```

## Subsystems

### Logger
Singleton; thread-safe write path. Levels: Debug/Info/Warn/Error/Fatal.
Macros: `PS5X_DEBUG`, `PS5X_INFO`, `PS5X_WARN`, `PS5X_ERROR`, `PS5X_FATAL`.
Filtering by level produces near-zero overhead (compile-time check).
Millisecond-precision timestamps. Console sink uses stderr for Error/Fatal.

### Config
Key-value store backed by an INI file. Read/written at startup/shutdown.
UI and subsystems read config via typed `Get<T>` / `Set<T>`.
Thread-safe `Get()` via mutex on `kv_store`.

### Memory
Host-backed allocation: `AllocHost(size, AllocType) → void*`, `FreeHost(ptr)`.
Stats: `GetStats()` → `{totalAllocated, peakAllocated, allocationCount}`.
Page-granularity commit/decommit. Commit() correctly tracks committed size.
Thread-safe via internal mutex.

### MemoryDiag
Allocation tracker: `RecordAlloc(tag, size, label)` / `RecordFree(tag)`.
`GetReport()` → `{leakCount, trackedBytes}`. Used by Phase 8 stability tests.

### Loader
ELF64 parser: validates magic, class, section headers. Returns typed
`LoadResult` enum. Accepts buffer or file path. Rejects all malformed input.

### Kernel / KernelRuntime
Object handle allocator (`AllocHandle / FreeHandle`).
`ObjectType` enum: Thread, Mutex, Semaphore, Event, File, Socket.
Full kernel object model: threads, mutexes, semaphores, events, timers, TLS, IPC, wait queues.

### Process
Lightweight process table. `Init / Shutdown` are idempotent.
Stress-tested for rapid create/destroy cycles.

### CPU (x86-64 Interpreter)
Single-threaded fetch-decode-execute loop.
State: `CpuContext` (16 GPRs, RIP, RFLAGS, XMM[0-15]).
`Step()` → `StepResult` (Ok / Syscall / Breakpoint / Fault / Halt / Unimplemented).
`Run()` loops until non-Ok result; skips one byte on Unimplemented (best-effort).
Breakpoints: address table → O(1) lookup per instruction.
Call stack: push on CALL (E8/FF/2), pop on RET (C3/C2).
80+ opcodes: ALU, shifts, MOVZX/MOVSX, CMOVcc, two-byte Jcc, MUL/IMUL/DIV, SSE stubs.
Statistics: `instructionsExecuted, syscallsDispatched, faults, breakpointsHit, unimplemented`.

### Syscall Dispatcher
`Dispatch(CpuContext)`: reads RAX as syscall number, dispatches to registered handler.
Handlers update RAX with return value (negative = errno).
Builtins: write/read, mmap/munmap, brk, exit/exit_group, getpid/gettid,
          sched_yield, clock_gettime, nanosleep, futex, clock_gettime, …
25+ Linux-compatible syscalls.

### GPU / CommandProcessor
`CommandList` records commands: BeginRenderPass, ClearColor, SetViewport,
SetScissor, DrawDirect, DrawIndexed, BarrierTransition, SetRenderTarget, End.
`CommandProcessor::Process(cl)` dispatches commands against DX12/DX11/Vulkan/null backend.
**DX12 (primary)**: adapter enumeration, command queue, swapchain, RTV heap, fence synchronization.
**DX11 (fallback)**: feature level negotiation, swapchain.
GPU heap uses Win32 `VirtualAlloc` on Windows, `malloc` fallback on Linux.
Fence wait handles spurious wakeups correctly.
Stats: `commandLists, drawCalls, renderPassBegins, barriers`.
`RuntimeEvents::FrameEnd` is published on every `Process()` call.

### ShaderCache
`ComputeKey(data, size)` → `uint64_t`. `Store(key, bytecode)` / `Lookup(key)`.
LRU eviction policy. Thread-safe.

### Audio
Port-based: `OpenPort(type, channels, sampleRate, format)` → handle.
`SubmitBuffer(port, data, size)` → `AudioResult::Ok`.
Null backend for headless; port state fully tracked.

### Input
`Poll(padIndex)` → `PadState{buttons, axisLX, axisLY, axisRX, axisRY}`.
`Inject(padIndex, state)` for test automation.

### InputMapping
Named input profiles (Default/Xbox/DualSense) with INI persistence.
Configurable axis scaling, deadzone, and button remapping per profile.
`CreateProfile`, `LoadProfile`, `SaveProfile`, `DeleteProfile`.
`MapButton`, `MapAxis`, `SetDeadzone`, `SetAxisScale`.
Optional via `PS5X_ENABLE_INPUT_MAPPING` CMake flag.

### Filesystem
Virtual mount points: `/app0`, `/system0`, `/temp0`, `/hostapp`.
`Mount / Unmount / Resolve(guestPath)` → host path.
`GetMountPoints()` → vector of active mounts.

### Debugger
**Phase 8 additions:**
- Register viewer: `GetRegisterView()` / `GetSpecialRegisters()` / `GetFlagsView()`
- Call stack: `GetCallStack(maxDepth)` — typed `CallFrame` list
- Memory: `ReadMemory(addr,len)→bytes`, `WriteMemory(addr,data)`, `HexDump(addr,len)`
- Modules: `GetModuleList()` from ModuleRegistry
- Symbols: `AddSymbol / LookupSymbol / NearestSymbol` — offset-aware resolution
- Breakpoints: `SetBreakpoint / RemoveBreakpoint / ClearAllBreakpoints / ListBreakpoints / RecordBreakpointHit`
- Timeline: ring buffer (max 1 024) of `{category, address, timestampNs}`
- Event browser: subscribes to RuntimeEvents bus; filterable `GetEventLog(type?)`

### CrashHandler
SEH (Windows) / signal (Linux) crash handler with MiniDump generation.
Windows: DbgHelp `MiniDumpWriteDump`. Linux: `backtrace`/`backtrace_symbols`.
Optional via `PS5X_ENABLE_CRASH_HANDLER` CMake flag.

### Metrics
Lock-free counters, gauges, and histograms with percentile estimation.
Designed for low-overhead telemetry in production builds.
Optional via `PS5X_ENABLE_METRICS` CMake flag.

### SaveState
Binary save/load with CRC32 checksums for integrity validation.
Slot-based management. Serializes memory + CPU + GPU state.
Optional via `PS5X_ENABLE_SAVESTATES` CMake flag.

### RuntimeEvents
Publish-subscribe: `Subscribe(EventType, handler)` / `Publish(EventType, Event)`.
Thread-safe. EventTypes: FrameEnd, ProcessStart, ProcessStop, SyscallDispatched, …

### UI
See CHANGELOG for full feature list. Backed by `Config` for persistence.
Dock panels serialised by ID. Log viewer capped at `MaxLogLines()`.

### PerfTools
Frame-time ring buffer (`FrameWindowSize()` samples).
Scope timers accumulate per named section.
Named benchmarks: `BeginBenchmark / EndBenchmark → ms`.
`ResetAll()` clears everything. Thread-safe.

## Threading Model

Each subsystem is internally thread-safe via `std::mutex`.
The CPU interpreter is single-threaded; `Pause()` / `Resume()` use atomics.
RuntimeEvents subscribers are called from the publisher's thread — handlers
must be short and must not re-enter `Publish`.

## Build System

CMake 3.22+. Targets: `ps5x` (library), `ps5x_main` (executable), per-suite
test executables registered via `ps5x_test(name file.cpp)`.
All targets compile with `-Wall -Wextra -Wpedantic -Werror` and `-std=c++20`.
Production hardening flags: `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, `/GS` (MSVC).
CPack packaging: ZIP + NSIS (Windows), TGZ (Linux).
