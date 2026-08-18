# Building ChuckStation 5

## System Requirements
- CMake 3.22 or newer
- C++20 compiler
- Catch2 (automatically fetched by CMake during test builds)

## Instructions

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```
