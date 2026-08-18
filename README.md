# ChuckStation 5

ChuckStation 5 is an open-source PlayStation 5 emulator backend and compatibility layer written in C++20.

## Overview

ChuckStation 5 provides a high-performance guest execution framework for PS5 homebrew and research applications.

Key subsystems:
- **CPU Interpreter:** x86-64 execution pipeline supporting ALU, vector/SSE stubs, control flow, and syscall traps.
- **Syscalls:** POSIX/FreeBSD-compatible syscall layer for memory management, process execution, and synchronization.
- **Kernel Runtime:** Handles guest threads, mutexes, condition variables, events, and virtual memory space.
- **Graphics & Display:** Modular command-list processor supporting Vulkan, DirectX 12, DirectX 11, and headless backends.
- **Audio & Input:** Low-latency audio buffer submitter and configurable input mapping.

---

## Building

### Requirements
- C++20 compliant compiler (MSVC 2022+, GCC 11+, or Clang 14+)
- CMake 3.22+
- Ninja or Visual Studio build system

### Linux / Unix

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows (MSVC)

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

---

## Testing

Run the test suite using CTest:

```bash
ctest --test-dir build --output-on-failure
```

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
