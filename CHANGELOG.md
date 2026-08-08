# Changelog

All notable changes to PS5x are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [1.0.0] – 2026-08-08

### Added – Production Infrastructure
- **CrashHandler**: SEH (Windows) / signal (Linux) crash handler with MiniDump generation
- **Metrics**: Lock-free counter/gauge/histogram telemetry system with percentile estimation
- **SaveStates**: Binary save/load with CRC32 validation, slot management, memory+CPU+GPU serialization
- **InputMapping**: Configurable DualSense/Xbox/Keyboard mapping profiles with INI persistence
- CMake CPack packaging support (ZIP + NSIS on Windows, TGZ on Linux)
- Production hardening flags: `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, `/GS` (MSVC)
- Optional module CMake flags: `PS5X_ENABLE_CRASH_HANDLER`, `PS5X_ENABLE_METRICS`, `PS5X_ENABLE_SAVESTATES`, `PS5X_ENABLE_INPUT_MAPPING`
- `PS5X_WARNINGS_AS_ERRORS` CMake option

### Changed – Production Hardening
- Version bumped to 1.0.0 (production milestone)
- Repository migrated to **libaerto/ps5x-windows**
- Copyright updated to "libaerto Contributors"
- Primary graphics backend: DirectX 12 (was Vulkan stub)
- OpenGL backend disabled by default
- DX12 backend fully implemented with adapter enumeration, command queue, swapchain, RTV heap, fence synchronization
- DX11 backend fully implemented with feature level negotiation and swapchain
- GPU heap uses Win32 `VirtualAlloc` on Windows, `malloc` fallback on Linux
- GPU fence/queue system fully implemented with `std::condition_variable` wait
- Config parser now properly handles INI sections and key-value persistence
- Logger timestamp includes millisecond precision
- All subsystems: improved error propagation, consistent `bool` return codes

### Fixed
- Memory Commit() no longer double-counts committed size
- GPU fence wait handles spurious wakeups correctly
- Logger console sink uses stderr for Error/Fatal levels
- Config Get() thread safety via mutex on kv_store

---

## [0.1.0-beta] – Unreleased (Phase 8)

### Added – CPU Backend
- `AND r64, r/m64` (0x23)
- `OR r64, r/m64` (0x0B)
- `TEST r/m64, r64` (0x85), `TEST rAX, imm32` (0xA9)
- `NOT r/m64` (0xF7 /2), `NEG r/m64` (0xF7 /3)
- `MUL r/m64` (0xF7 /4), `IMUL r/m64` (0xF7 /5)
- `DIV r/m64` (0xF7 /6), `IDIV r/m64` (0xF7 /7)
- `IMUL r64, r/m64, imm32` (0x69)
- `IMUL r64, r/m64` (0x0F 0xAF)
- `SHL/SHR/SAR r/m64, imm8` (0xC1 /4,5,7)
- `SHL/SHR/SAR r/m64, CL` (0xD3 /4,5,7)
- `MOVZX r64, r/m8` (0x0F 0xB6), `MOVZX r64, r/m16` (0x0F 0xB7)
- `MOVSX r64, r/m8` (0x0F 0xBE), `MOVSX r64, r/m16` (0x0F 0xBF)
- `CMOVcc r64, r/m64` (0x0F 0x40–0x4F) — all 16 conditions
- Two-byte near `Jcc rel32` (0x0F 0x80–0x8F) — all 16 conditions
- `RET imm16` (0xC2) — pop extra bytes after return address
- `XCHG r64, RAX` (0x91–0x97)
- `LAHF` (0x9F), `SAHF` (0x9E)
- `ImplementedOpcodes()` API — returns complete list of implemented opcode bytes
- Fault callback and fault-stat tracking for divide-by-zero and null-RIP

### Added – Debugger (Phase 8 polish)
- `GetRegisterView()` — all 16 GPRs as typed `RegEntry` list
- `GetSpecialRegisters()` — RIP, RFLAGS, CS, SS
- `GetFlagsView()` — CF/PF/AF/ZF/SF/TF/IF/DF/OF as named flag entries
- `GetCallStack(maxDepth)` — typed `CallFrame` list (delegates to CPU)
- `ReadMemory(addr, len)` → `vector<uint8_t>`
- `WriteMemory(addr, data)` — patch memory via vector
- `HexDump(addr, len)` — formatted hex+ASCII string
- `GetModuleList()` — typed `ModuleEntry` list from `ModuleRegistry`
- `AddSymbol / LookupSymbol / NearestSymbol` — symbol table with offset resolution
- `SetBreakpoint / RemoveBreakpoint / ClearAllBreakpoints / ListBreakpoints / RecordBreakpointHit`
- Timeline: `RecordTimelineEvent / GetTimeline / ClearTimeline / MaxTimelineEvents`
- Event browser: `AttachEventBrowser / GetEventLog(filter?) / ClearEventLog`

### Added – Graphics Validation
- Progressive rendering demos in CommandProcessor: triangle, textured quad,
  indexed draw, off-screen render, barrier transitions, depth target
- `GPU::SetDepthTarget / GetCurrentDepthTarget`
- `BarrierTransition` command recorded in stats (`stats.barriers`)
- `CommandProcessor::ResetStats()`
- Frame-end event published on every `CommandProcessor::Process()` call

### Added – Homebrew Validation Suite
- `test_phase8_homebrew.cpp` — regression corpus covering:
  Hello World (exit codes), console write (stdout/stderr/bad-fd),
  File I/O (mount/unmount/path resolution),
  Graphics (triangle + quad draw), Audio (port open/submit/close),
  Input (poll/inject), Threading (gettid/sched_yield/clock_gettime),
  Memory allocation (stack/heap alloc, brk stub)

### Added – UI (Phase 8 polish)
- Welcome screen: `IsWelcomeScreenEnabled / SetWelcomeScreenEnabled`
- Recent homebrew list: `GetRecentList / AddRecentEntry / ClearRecentList`
  (max 20, deduplication, most-recent-first ordering)
- Firmware manager: `GetFirmwareStatus / SetFirmwarePath / ClearFirmwarePath`
- Theme customisation: `SetTheme / GetCurrentTheme / SetAccentColor / GetAccentColor`
- Dock layout persistence: `SetDockPanel / GetDockPanel / SaveLayout / LoadLayout`
- Searchable log viewer: `ClearLogViewer / IngestLogLine / SearchLogViewer /
  FilterLogByLevel / MaxLogLines`
- Performance dashboard: `GetDashboardFPS / GetDashboardCPUUsage /
  GetDashboardCPUStats / SetStatusOverlay / GetStatusOverlay`
- All layout/theme settings persist through `SaveLayout / LoadLayout`

### Added – Performance
- `PerfTools::BeginBenchmark / EndBenchmark / GetBenchmarkResults`
- `PerfTools::ResetAll` — clears frame stats, section stats, and benchmark results
- `CommandProcessor::ResetStats()`
- Logger level filtering: suppressed messages measured near-zero overhead
- `Cpu::IsRunning()` — non-blocking running-state query

### Added – Testing
- 10 new Phase 8 test suites (cpu, stability, graphics, homebrew, debugger,
  ui, perf, fuzz, concurrency, integration)
- All 16 Jcc rel8 conditions covered
- All 16 CMOVcc conditions covered
- Two-byte near Jcc (0F 8x) covered
- Fuzz suite: random opcode byte decode, loader malformed-input, filesystem
  path fuzz, CommandProcessor mixed-command fuzz
- Concurrency suite: Logger, RuntimeEvents, Memory, CPU, Syscall, PerfTools
- Stability suite: process lifecycle, memory lifetime, leak detection, rapid
  CPU reset, exception diagnostics

### Changed
- `Debugger::Init()` now returns `bool` (was `void`)
- `Debugger` legacy API maintained for backward compatibility
- `Cpu::Run()` skips one byte on `Unimplemented` and continues (best-effort)

### Fixed
- CPU flag updates for `AND/OR/XOR/TEST` now correctly clear CF and OF
- `SAR` sign-extends correctly for count ≥ 64
- `NEG` sets CF=0 when operand is zero (Intel manual compliance)
- LAHF sets bit 1 (reserved, always 1) in AH

---

## [0.0.7] – Phase 7

- x86-64 interpreter (MOV, ADD, SUB, XOR, CMP, JMP, Jcc rel8, CALL, RET, PUSH, POP, LEA, NOP, HLT, SYSCALL, INT 3)
- Syscall dispatcher with 20+ Linux-compatible syscalls
- Command processor (begin/end render pass, clear, draw-direct/indexed, pipeline)
- 35 test suites, 1 075 assertions

---

## [0.0.6] – Phase 6

- UI framework, PerfTools, Audio, Input, RuntimeEvents, DynamicLinker, ShaderCache
- Conditional breakpoints, watch expressions, debugger event timeline (legacy)

## [0.0.5] – Phase 5

- KernelRuntime, KernelServices, ModuleRegistry, MemoryDiag, Execution subsystems

## [0.0.4] – Phase 4

- GPU, CommandProcessor, VulkanBackend (stub), Renderer

## [0.0.3] – Phase 3

- Filesystem, DynamicLinker skeleton, KytyAdapter

## [0.0.2] – Phase 2

- Debugger, Loader, Process, Runtime

## [0.0.1] – Phase 1

- Logger, Config, Memory, Kernel (basic), initial CMake, CI
