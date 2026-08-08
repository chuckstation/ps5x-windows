# Contributing to PS5x

Thank you for your interest in contributing. PS5x is a research project;
contributions that improve correctness, stability, documentation, or test
coverage are most welcome.

---

## Ground Rules

1. **No proprietary content.** Do not submit firmware, encryption keys,
   game assets, or any copyrighted Sony material. PRs containing such
   content will be closed immediately.

2. **No commercial game compatibility work.** The project scope is
   research and homebrew. PRs targeting commercial game support are
   out of scope.

3. **Tests required.** Every new feature must be accompanied by tests.
   Bug fixes should add a regression test.

4. **Warning-free.** All code must compile without warnings under the
   project's default flags (`-Wall -Wextra -Wpedantic -Werror`).

---

## Development Workflow

```bash
# Clone
git clone https://github.com/libaerto/ps5x-windows.git
cd ps5x-windows

# Build (Debug)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

# Run all tests
cd build && ctest -C Debug --output-on-failure

# Run a specific suite
ctest -R phase8_cpu --output-on-failure
```

---

## Code Style

- **Language standard:** C++20.
- **Naming:** `PascalCase` for types and functions, `snake_case` for local
  variables, `kCamelCase` for compile-time constants.
- **Namespaces:** all public code lives under `PS5x::SubsystemName`.
- **Headers:** one header per subsystem in `include/PS5x/<Name>/<Name>.h`.
- **No raw `new`/`delete`:** use smart pointers or the `Memory` module.
- **No exceptions in hot paths:** use `std::optional` and typed error enums.
- **Thread safety:** document thread-safety guarantees in the header.
  Protect shared state with `std::mutex`; prefer lock-free atomics where
  appropriate.

---

## Test Requirements

- Catch2 v3 (auto-fetched).
- Every subsystem must have a dedicated test file (`test_<subsystem>.cpp`).
- Use descriptive test names: `SubsystemName::Category::WhatItTests`.
- Phase N features go in `test_phaseN_<subsystem>.cpp`.
- Each PR should bring net-positive assertion count.

---

## Pull Request Process

1. Fork → branch from `main`.
2. Implement feature + tests.
3. Update `CHANGELOG.md` under `[Unreleased]`.
4. Open a PR with a clear description of what changed and why.
5. CI must be green (Debug + Release, all tests, formatting, static analysis).
6. At least one maintainer review required before merge.

---

## Reporting Issues

Use GitHub Issues. Include:
- OS, compiler version, CMake version.
- Steps to reproduce.
- Expected vs. actual behaviour.
- Build type (Debug/Release) and any relevant log output.
