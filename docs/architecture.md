# PS5x Architecture

Repository: [github.com/libaerto/ps5x-windows](https://github.com/libaerto/ps5x-windows) · Version: 1.0.0

## Overview

PS5x layers a clean, modular C++20 API on top of Kyty's existing PlayStation
emulation machinery.  Kyty lives in `kyty-upstream/` and is compiled only on
Windows when `PS5X_INCLUDE_KYTY=ON`.  All PS5x application code talks
exclusively to PS5x module interfaces.  **Only `KytyAdapter`** may include
Kyty headers.

```
┌─────────────────────────────────────────────────────────────────┐
│  Application  (src/main.cpp)                                     │
├──────────┬───────────┬──────────┬──────────┬────────┬───────────┤
│  Logger  │  Config   │  Memory  │  Loader  │  GPU   │  Debugger │
├──────────┴───────────┴──────────┴──────────┴────────┴───────────┤
│  Input    Audio    Filesystem    Renderer    UI                  │
├─────────────────────────────────────────────────────────────────┤
│  CrashHandler  Metrics  SaveStates  InputMapping  ShaderCache   │
├─────────────────────────────────────────────────────────────────┤
│  KytyAdapter  (ONLY file that may include Kyty headers)          │
├─────────────────────────────────────────────────────────────────┤
│  Kyty upstream  (kyty-upstream/source/ – Windows MSVC/MinGW)    │
└─────────────────────────────────────────────────────────────────┘
```

## Module Reference

### Logger  (`PS5x::Logger`)
Thread-safe, zero-overhead at compile-time when disabled.  Multiple sinks
(file, console, UI console pane).  `source_location`-aware macros.
Millisecond-precision timestamps. Console sink uses stderr for Error/Fatal levels.
No dependencies.

### Config  (`PS5x::Config`)
Typed config structs.  INI-format parse with section support and key-value
persistence. Thread-safe `Get()` via mutex on `kv_store`.
Strict firmware path validation – PS5x **never** supplies firmware.
Depends on: Logger.

### Memory  (`PS5x::Memory`)
Full tracked virtual allocator replacing Kernel stubs.
`Map`/`Reserve`/`Commit`/`Protect`/`Unmap` → OS `VirtualAlloc`/`mmap`.
`Alloc`/`Free` with alignment and leak tracking.
Region query: `FindRegion`, `IsReadable`, `IsWritable`, `IsExecutable`.
Debug: `GetStats`, `ForEachRegion`, `ReportLeaks`.
Page-granularity commit/decommit. Commit() correctly tracks committed size.
Depends on: Logger.

### Kernel  (`PS5x::Kernel`)
Legacy compatibility shim over Memory.  New code should use Memory directly.
Depends on: Logger, Memory.

### Loader  (`PS5x::Loader`)
Complete ELF64 parser and loader.
`InspectElf` → header-only parse (safe on untrusted input).
`MapSegments` → maps `PT_LOAD` segments via Memory with correct protections.
`LoadExecutable` = InspectElf + ValidateExecutable + MapSegments.
`LoadParamSfo` → SFO title/version/content metadata.
`ValidateFirmware` → existence check only; PS5x never supplies firmware.
Supports ET_EXEC, ET_DYN, ET_SCE_EXEC, ET_SCE_DYNEXEC, ET_SCE_DYNAMIC.
Depends on: Logger, Memory.

### Input  (`PS5x::Input`)
SDL2 GameController backend.  Detects DualSense/DS4/Xbox by VID:PID.
Normalises axes with deadzone, synthesises L2/R2 buttons.
Hotplug via SDL events.  Keyboard virtual pad always in slot 0.
Haptic rumble via SDL_HapticRumblePlay.
Depends on: Logger, Config, SDL2 (optional).

### InputMapping  (`PS5x::InputMapping`)
Named input profiles (Default/Xbox/DualSense) with INI persistence.
Configurable axis scaling, deadzone, and button remapping.
Profile load/save via INI files.
Depends on: Logger, Config.

### Audio  (`PS5x::Audio`)
Port-based API (sceAudioOut style).  Per-port ring buffer.
SDL2 audio device callback mixes all running ports.
Sine-wave self-test for validation without a game.
Graceful degradation (no crash) when no audio device is present.
Null backend for headless/testing.
Depends on: Logger, Config, SDL2 (optional).

### Filesystem  (`PS5x::Filesystem`)
VFS with six mount points: `/app0`, `/savedata`, `/system`, `/temp`,
`/user`, `/hostapp`.  Read-only mount enforcement.  Path normalisation
(collapses `..`, `//`, `.`).  Full POSIX-style file I/O.
`EnsureSaveDir` helper.  `DumpMounts` for debug.
Depends on: Logger.

### Renderer  (`PS5x::Renderer`)
`IRendererBackend` pure virtual interface.
Factory: `CreateBackend(GraphicsBackend)`.
DX12 (primary), DX11 (fallback), Vulkan (optional), OpenGL (disabled by default), Null backends.
Depends on: Logger, Config.

### GPU  (`PS5x::GPU`)
Command-list based renderer with full fence/queue synchronization.
DX12: adapter enumeration, command queue, swapchain, RTV heap, fence sync.
DX11: feature level negotiation and swapchain.
GPU heap uses Win32 `VirtualAlloc` on Windows, `malloc` fallback on Linux.
Fence wait handles spurious wakeups correctly.
Depends on: Logger, Renderer.

### ShaderCache  (`PS5x::ShaderCache`)
Content-addressed binary cache keyed by `ShaderKey{spirvHash, pipelineHash, stage}`.
LRU eviction when `entries.size() >= maxEntries`. Thread-safe.
Background worker thread shares the same `CacheState` mutex.
Disk format: flat binary `[count][key+name+binary+meta...]`.
Depends on: Logger, RuntimeEvents.

### UI  (`PS5x::UI`)
ImGui PS5-inspired dark theme (navy/blue/white palette).
Welcome screen, recent list, firmware manager, theme, dock layout,
searchable log viewer, performance dashboard.
Log console pane subscribes to Logger via sink.
Event callback model decouples UI from emulator state.
Depends on: Logger, Config.

### Debugger  (`PS5x::Debugger`)
Breakpoints with INT3 patching (unpatches on remove).
Register view, call stack, memory viewer, module browser, symbol table,
breakpoints, timeline, event browser.
`HexDumpRegion` – formatted hex + ASCII of any mapped range.
`WriteCrashDump` – timestamped file with CPU state, memory map, stack.
Callback model: `OnBreakpointHit` fires registered `BreakpointHitFn`.
Depends on: Logger, Memory.

### CrashHandler  (`PS5x::CrashHandler`)
SEH (Windows) / signal (Linux) crash handler with MiniDump generation.
Windows: DbgHelp `MiniDumpWriteDump`. Linux: `backtrace`/`backtrace_symbols`.
Optional via `PS5X_ENABLE_CRASH_HANDLER` CMake flag.
Depends on: Logger.

### Metrics  (`PS5x::Metrics`)
Lock-free counters, gauges, and histograms with percentile estimation.
Designed for low-overhead telemetry in production builds.
Optional via `PS5X_ENABLE_METRICS` CMake flag.
Depends on: Logger.

### SaveState  (`PS5x::SaveState`)
Binary save/load with CRC32 checksums for integrity validation.
Slot-based management. Serializes memory + CPU + GPU state.
Optional via `PS5X_ENABLE_SAVESTATES` CMake flag.
Depends on: Logger, Memory.

### KytyAdapter  (`PS5x::KytyAdapter`)
**Isolation layer.  The ONLY `.cpp` in PS5x that may `#include` Kyty headers.**
All functions forward to PS5x native implementations when `PS5X_INCLUDE_KYTY`
is OFF or on non-Windows.  When Kyty is compiled in, real Kyty calls are
inserted here behind the same typed interface.
Bridges: `LoadProgram`, `ExecuteProgram`, `VirtualAlloc`, `VirtualFree`,
`InitVulkan`, `InitAudio`, `InstallLogBridge`.
Depends on: Logger, Memory.

## Dependency Graph

```
KytyAdapter  ──► Memory ──► Logger
Loader       ──► Memory, Logger
Kernel       ──► Memory, Logger
Debugger     ──► Memory, Logger
Config       ──► Logger
Input        ──► Config, Logger, [SDL2]
InputMapping ──► Config, Logger
Audio        ──► Config, Logger, [SDL2]
Filesystem   ──► Logger
GPU          ──► Renderer, Logger
Renderer     ──► Config, Logger
UI           ──► Config, Logger
CrashHandler ──► Logger
Metrics      ──► Logger
SaveState    ──► Memory, Logger
ShaderCache  ──► RuntimeEvents, Logger
PerfTools    ──► Logger
main         ──► all modules
```

## Naming Conventions

| Item | Convention | Example |
|------|-----------|---------|
| Namespace | `PS5x::<Module>` | `PS5x::Memory` |
| Public header | `include/PS5x/<Module>/<Module>.h` | |
| Implementation | `src/<Module>/<Module>.cpp` | |
| Unit test | `tests/unit/test_<module>.cpp` | |
| CMake target | `ps5x_<module>` | `ps5x_memory` |

## Adding a New Module

1. Create `include/PS5x/<Name>/<Name>.h` with SPDX header.
2. Create `src/<Name>/<Name>.cpp`.
3. Add `ps5x_module(<name> src/<Name>/<Name>.cpp)` in `source/ps5x/CMakeLists.txt`.
4. Add `target_link_libraries` for deps.
5. Add `tests/unit/test_<name>.cpp` with Init/Shutdown + happy-path tests.
6. Add `ps5x_test(<name> test_<name>.cpp)` in `tests/unit/CMakeLists.txt`.
7. Document in this file.

## Integration Points (TODO markers)

Every stub carries a `// TODO(milestone-N): ...` comment.  The milestone
number matches `docs/milestones.md`.  Never remove a TODO without wiring
the actual implementation.

---

## Phase 3 Additions

### Runtime  (`PS5x::Runtime`)
Ordered startup/teardown with dependency graph (Kahn's topological sort).
`Register` → `InitAll` → `ShutdownAll` lifecycle.
Timing is measured per subsystem and reported via Logger.
**Critical**: lock is released before calling init/shutdown callbacks to
prevent deadlock when callbacks re-enter the Runtime (e.g. calling `IsRunning`).
Depends on: all other modules (transitively).

### KernelRuntime  (`PS5x::KernelRuntime`)
Emulated PS5 kernel object layer built on C++20 primitives.
All objects stored in a `KHandle`-keyed table (`std::unordered_map<int32_t, HandleEntry>`
holding `std::shared_ptr<void>`). Each object type has a typed getter.

TLS uses a `(std::thread::id, TlsKey)` global map rather than per-thread
currentHandle lookup, making it work reliably from any thread without requiring
the emulated thread to be registered first.

JoinThread uses a polling loop (100µs sleeps) rather than `condition_variable`
to avoid mutex/thread-destructor race conditions during test teardown.

Threads do **not** `Memory::Map` the host stack; host OS threads manage their
own stack. Guest stack allocation will be added in milestone-2 via KytyAdapter.

Full kernel object model: threads, mutexes, semaphores, events, timers, TLS, IPC, wait queues.
Depends on: Logger, Memory.

### Process  (`PS5x::Process`)
Full process lifecycle manager.
`Create` validates firmware (user-supplied only), mounts `/app0`+`/system`,
loads the main ELF, and creates (but does not start) the main thread.
`Terminate` releases `ps.mtx` before calling `StopThread` and
`Loader::UnloadExecutable` to prevent lock-order inversion.
Exit callbacks are cleared in `Init()`/`Shutdown()` to avoid dangling
reference crashes when the same test binary runs multiple test cases.
Depends on: Logger, Memory, Filesystem, Loader, KernelRuntime.

---

## Phase 4 Additions

### Execution  (`PS5x::Execution`)
Top-level guest execution API decoupling all subsystems.
`LoadProgram` orchestrates: firmware validation → VFS mount → Process::Create.
`ExecContext::mtx` is `std::recursive_mutex` so `SetState` (which acquires the lock
to call `onStateChange` callbacks) can be called safely from within exit callbacks
that already hold the lock. Frame and syscall counters are `std::atomic<uint64_t>`.
Depends on: Logger, Loader, Process, KernelRuntime, Filesystem, Debugger, KytyAdapter.

### ModuleRegistry  (`PS5x::ModuleRegistry`)
Tracks all loaded modules with reference-counted descriptors.
`Register` deduplicates by name and populates the export table from ELF symbols.
`Load` calls `Loader::LoadExecutable` then registers and applies relocations.
`Unload` refuses when dependents exist (prevents use-after-free in PLT stubs).
Load order is maintained as a simple insertion-order vector; `TopoSort` used for
dependency ordering when needed.
Depends on: Logger, Loader.

### KernelServices  (`PS5x::KernelServices`)
Supplementary kernel primitives complementing KernelRuntime:
- **Shared memory**: host `Memory::Map` + reference counting + name registry
- **Message queue**: priority-ordered `std::deque` guarded by condvar pair
- **Spinlock**: `std::atomic<bool>` with `std::this_thread::yield()` backoff;
  defined inline to avoid member-function-out-of-class-definition issues
- **RW lock**: thin wrapper over `std::shared_mutex`
- **CondVar**: `std::condition_variable` with `wait_for` timeout
- **Scheduler**: `SchedYield` / `SchedSetPriority` / `Sleep` / `SleepNs`
Depends on: Logger, Memory, KernelRuntime.

### MemoryDiag  (`PS5x::MemoryDiag`)
Observability layer over the Memory Manager.
Snapshots are `{timestamp, label, Memory::Stats, []Region}` structs.
`DiffSnapshots` compares two snapshots and lists added/removed regions.
History ring (max 65 536 events) is disabled by default; enabled for profiling.
`ComputeFragmentation` sorts regions by base address and measures gaps.
`SearchPattern` walks all readable committed regions with `std::memcmp`.
Depends on: Logger, Memory.

---

## Phase 5 Additions

### RuntimeEvents  (`PS5x::RuntimeEvents`)
Structured event bus that crosses all subsystem boundaries.
27 `EventType` values with typed `EventPayload` variant (10 types).
Ring buffer (default 65 536) stores all events; `GetRecent`/`GetByType` query.
Subscribers get filtered or unfiltered callbacks synchronously inside `Publish`.
The watchdog runs on a dedicated OS thread polling every 100 ms; `KickWatchdog`
resets the timer from the guest heartbeat path.
`ScopeTimer` emits `ProfileBegin`/`ProfileEnd` events for the timeline viewer.
Depends on: Logger.

### DynamicLinker  (`PS5x::DynamicLinker`)
ELF x86-64 relocation processor layered over `ModuleRegistry`.
`ApplyReloc` temporarily re-protects pages (RW) to write GOT/PLT slots, then
restores the original protection (RX for code, R for data).
Symbol cache uses FNV-1a key lookup; `InvalidateCache(id)` clears entries on
module unload to prevent stale address resolution.
`DetectCycles` uses iterative DFS; `TopologicalOrder` uses Kahn's algorithm,
both operating over `ModuleRegistry::GetDependencies`.
Full PLT/GOT patching for SCE-format relocations is TODO(milestone-5).
Depends on: Logger, Memory, ModuleRegistry, RuntimeEvents.

### PerfTools  (`PS5x::PerfTools`)
Layered profiling tools with zero mandatory overhead when unused.
Frame timer uses a 60-frame rolling deque for avg/min/max FPS.
CPU profiler uses a per-`thread::id` token stack so nested scopes resolve
correctly without global ordering assumptions.
Worker pool uses `std::condition_variable` pair (`cv` for job dispatch,
`doneCv` for `WaitAll` completion notification).
Benchmark function excludes warmup iterations from statistics and computes
population std-deviation for latency spread analysis.
Depends on: Logger.

---

## Phase 9 Additions (v1.0.0 Production)

### CrashHandler  (`PS5x::CrashHandler`)
SEH (Windows) / signal (Linux) crash handler with MiniDump generation.
Windows: DbgHelp `MiniDumpWriteDump` for rich crash dumps.
Linux: `backtrace`/`backtrace_symbols` for stack trace capture.
Optional via `PS5X_ENABLE_CRASH_HANDLER` CMake flag.
Depends on: Logger.

### Metrics  (`PS5x::Metrics`)
Lock-free counters, gauges, and histograms with percentile estimation.
Designed for low-overhead telemetry in production builds.
Supports: `Counter::Increment`, `Gauge::Set/Increment/Decrement`,
`Histogram::Observe` with configurable bucket boundaries.
Percentile estimation via HDR histogram algorithm.
Optional via `PS5X_ENABLE_METRICS` CMake flag.
Depends on: Logger.

### SaveState  (`PS5x::SaveState`)
Binary save/load with CRC32 checksums for integrity validation.
Slot-based management (configurable number of slots).
Serializes: guest memory, CPU registers/flags, GPU state.
`Save(slot)` → binary blob with header + CRC32.
`Load(slot)` → validates CRC32, restores all state.
Optional via `PS5X_ENABLE_SAVESTATES` CMake flag.
Depends on: Logger, Memory.

### InputMapping  (`PS5x::InputMapping`)
Named input profiles (Default/Xbox/DualSense) with INI persistence.
Configurable axis scaling, deadzone, and button remapping per profile.
`CreateProfile`, `LoadProfile`, `SaveProfile`, `DeleteProfile`.
`MapButton`, `MapAxis`, `SetDeadzone`, `SetAxisScale`.
Optional via `PS5X_ENABLE_INPUT_MAPPING` CMake flag.
Depends on: Logger, Config.
