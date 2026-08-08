# Building PS5x (v1.0.0)

Repository: [github.com/libaerto/ps5x-windows](https://github.com/libaerto/ps5x-windows)

## Requirements

| Tool | Minimum Version |
|------|----------------|
| CMake | 3.22 |
| GCC | 11 |
| Clang | 14 |
| MSVC | 2022 (19.30) |
| Ninja (optional) | 1.10 |

Catch2 v3 is automatically downloaded by CMake via FetchContent.

---

## Release Build (Windows, MSVC)

```bash
git clone https://github.com/libaerto/ps5x-windows.git
cd ps5x-windows
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

## Release Build (Windows, Clang-cl + Ninja)

```bash
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Debug Build

```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel
```

## Run Tests

```bash
cd build
ctest -C Release --output-on-failure              # all suites
ctest -R phase8 --output-on-failure               # Phase 8+ only
ctest -R concurrency --output-on-failure          # thread-safety
ctest -R fuzz --output-on-failure                 # robustness
```

## CMake Options

### Build & Testing

| Option | Default | Description |
|--------|---------|-------------|
| `PS5X_BUILD_TESTS` | ON | Build test executables |
| `PS5X_ENABLE_ASAN` | OFF | Address sanitiser |
| `PS5X_ENABLE_TSAN` | OFF | Thread sanitiser |
| `PS5X_ENABLE_UBSAN` | OFF | UB sanitiser |
| `PS5X_WARNINGS_AS_ERRORS` | ON | `-Werror` / `/WX` |

### Graphics Backends

| Option | Default | Description |
|--------|---------|-------------|
| `PS5X_ENABLE_DX12` | ON | DirectX 12 renderer backend (primary) |
| `PS5X_ENABLE_DX11` | ON | DirectX 11 renderer backend (fallback) |
| `PS5X_ENABLE_VULKAN` | OFF | Vulkan renderer backend (optional) |
| `PS5X_ENABLE_OPENGL` | OFF | OpenGL renderer backend (disabled by default) |

### Production Modules (v1.0.0)

| Option | Default | Description |
|--------|---------|-------------|
| `PS5X_ENABLE_CRASH_HANDLER` | ON | SEH/signal crash handler with minidump generation |
| `PS5X_ENABLE_METRICS` | ON | Lock-free counters, gauges, histograms telemetry |
| `PS5X_ENABLE_SAVESTATES` | ON | Binary save/load with CRC32 checksums |
| `PS5X_ENABLE_INPUT_MAPPING` | ON | Named input profiles with INI persistence |

### Other

| Option | Default | Description |
|--------|---------|-------------|
| `PS5X_INCLUDE_KYTY` | OFF | Include Kyty upstream in the build (Windows only) |
| `PS5X_CLANG_TIDY` | OFF | Run clang-tidy during build |

Example with sanitisers:
```bash
cmake -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPS5X_ENABLE_ASAN=ON \
  -DPS5X_ENABLE_UBSAN=ON
cmake --build build-asan --parallel
cd build-asan && ctest --output-on-failure
```

Example with production modules disabled (minimal build):
```bash
cmake -B build-minimal \
  -DCMAKE_BUILD_TYPE=Release \
  -DPS5X_ENABLE_CRASH_HANDLER=OFF \
  -DPS5X_ENABLE_METRICS=OFF \
  -DPS5X_ENABLE_SAVESTATES=OFF \
  -DPS5X_ENABLE_INPUT_MAPPING=OFF
cmake --build build-minimal --parallel
```

## Packaging (CPack)

PS5x v1.0.0 includes CPack support for creating distributable packages:

```bash
cmake --build build --config Release
cd build
cpack -C Release
```

- **Windows**: produces ZIP archive and optional NSIS installer
- **Linux**: produces TGZ archive

## CI

GitHub Actions runs on every push and PR:
1. Configure (Debug + Release)
2. Build
3. Test (`ctest --output-on-failure`)
4. Formatting check (`clang-format --dry-run`)
5. Static analysis (`clang-tidy`)
6. Produce versioned artifact on tagged commits
