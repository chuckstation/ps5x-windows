# PS5x — Production-Ready PS5 Emulator Framework (v1.0.0)

[![CI](https://github.com/libaerto/ps5x-windows/actions/workflows/ci.yml/badge.svg)](https://github.com/libaerto/ps5x-windows/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)]()

PS5x is a production-grade, open-source, research-oriented emulator framework
for running **user-provided homebrew** on a software-emulated PS5-like environment
on **Windows**.

> **Important:** PS5x is not a PS5 game emulator. It does not include and
> never will include firmware, encryption keys, or copyrighted content. Users
> must supply any required firmware themselves.

**Repository:** [github.com/libaerto/ps5x-windows](https://github.com/libaerto/ps5x-windows)

---

## Features (v1.0.0 Production)

| Subsystem | Capability |
|-----------|------------|
| **CPU** | x86-64 interpreter — 80+ opcodes (ALU, shifts, MOVZX/MOVSX, CMOVcc, two-byte Jcc, MUL/IMUL/DIV, SSE stubs) |
| **Syscall** | 25+ Linux-compatible syscalls (write, read, mmap, brk, exit, getpid, gettid, futex, clock_gettime, …) |
| **Memory** | Host-backed guest address space with allocation tracking, leak detection, page-granularity commit/decommit |
| **GPU** | Command-list based renderer with DX12 (primary), DX11 (fallback), Vulkan (optional) backends; fence/queue synchronization |
| **ShaderCache** | LRU-eviction shader cache with hash-based lookup; thread-safe |
| **Audio** | Port-based audio output with buffer submission API; null backend for headless |
| **Input** | Configurable DualSense/Xbox/keyboard mapping with deadzone and axis scaling |
| **Debugger** | Register view, call stack, memory viewer, module browser, symbol table, breakpoints, timeline, event browser |
| **UI** | Welcome screen, recent list, firmware manager, theme, dock layout, log viewer, perf dashboard |
| **PerfTools** | Frame-time tracking, scope timers, benchmarking, rolling statistics |
| **CrashHandler** | SEH/signal crash handler with minidump generation (Windows: DbgHelp, Linux: backtrace) |
| **Metrics** | Lock-free counters, gauges, histograms with percentile estimation |
| **SaveStates** | Binary save/load with CRC32 checksums, slot-based management |
| **InputMapping** | Named profiles (Default/Xbox/DualSense), INI persistence, axis scaling/deadzone |
| **Loader** | ELF64 loader (validates headers, rejects malformed input) |
| **Filesystem** | Virtual mount-point filesystem (/app0, /system, /temp, …) |
| **KernelRuntime** | Full kernel object model: threads, mutexes, semaphores, events, timers, TLS, IPC, wait queues |

---

## Quick Start

### Prerequisites

- **OS:** Windows 10/11 (primary), Linux (headless/testing)
- **CMake** ≥ 3.22
- **C++20** compiler: MSVC 2022+, Clang 14+, GCC 11+
- **Catch2** v3 (auto-fetched by CMake)
- Git

### Build (Windows, MSVC)

```bash
git clone https://github.com/libaerto/ps5x-windows.git
cd ps5x-windows
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

### Build (Windows, Clang-cl + Ninja)

```bash
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Run Tests

```bash
cd build
ctest -C Release --output-on-failure --parallel 4
```

### Run a Homebrew ELF

```bash
.\build\bin\Release\ps5x.exe path\to\your\homebrew.elf
```

PS5x will reject the ELF and exit cleanly if it does not conform to the
expected format. No firmware is required for homebrew that uses only the
emulated syscall surface.

---

## Project Layout

```
ps5x-windows/
├── source/ps5x/
│   ├── include/PS5x/      # Public headers (one per subsystem)
│   └── src/               # Implementations
├── tests/
│   └── unit/              # Catch2 test suites (50+ suites, 1500+ assertions)
├── .github/workflows/     # CI (MSVC + Clang-cl, ASAN, static analysis, release)
├── cmake/                 # CMake modules (compiler flags, version generation)
├── docs/                  # Architecture diagrams, API reference
├── scripts/               # Dev setup, pre-commit hooks
├── ARCHITECTURE.md
├── BUILD.md
├── CHANGELOG.md
├── CONTRIBUTING.md
├── ROADMAP.md
├── SECURITY.md
└── README.md
```

---

## Configuration

PS5x uses an INI-format config file (`ps5x.toml`). A template is provided
at `ps5x.toml.template`. Key settings:

```ini
[emulator]
firmwarePath = ""          # User-supplied firmware path
gameContentPath = ""
logLevel = 2               # 0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error

[graphics]
backend = 0                # 0=DX12, 1=DX11, 2=Vulkan, 3=OpenGL, 4=Null
width = 1920
height = 1080
vsync = true
msaa = 1

[input]
mode = 0                   # 0=DualSense, 1=DS4, 2=Xbox, 3=Keyboard
deadzone = 0.1

[audio]
sampleRate = 48000
bufferSize = 512
masterVolume = 1.0

[debug]
enableDebugger = false
dumpShaders = false
traceSyscalls = false
```

---

## Documentation

| Document | Purpose |
|----------|---------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Subsystem design, data flow, layering |
| [BUILD.md](BUILD.md) | Detailed build instructions and CMake options |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Code style, PR process, test requirements |
| [CHANGELOG.md](CHANGELOG.md) | Version history |
| [ROADMAP.md](ROADMAP.md) | Phase history and future plans |
| [SECURITY.md](SECURITY.md) | Vulnerability reporting and scope |

---

## Testing

```bash
ctest -C Release --output-on-failure          # all suites
ctest -R phase8 --output-on-failure           # Phase 8+ only
ctest -R fuzz   --output-on-failure           # robustness
ctest -R concurrency --output-on-failure      # thread-safety
```

Production target: **50+ test suites · 1 500+ assertions · zero warnings**

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Every PR must:
1. Build warning-free in both Debug and Release.
2. Pass the full test suite.
3. Add tests for new functionality.
4. Update CHANGELOG.md.

---

## License

MIT — see [LICENSE](LICENSE).

---

## Legal

PS5x contains no Sony firmware, encryption keys, or copyrighted game content.
"PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment.
This project is not affiliated with or endorsed by Sony.

## Acknowledgements

- **Kyty** (MIT, © 2021 InoriRus) — upstream reference implementation
- **Catch2** (BSL-1.0) — test framework
- **libaerto** contributors
