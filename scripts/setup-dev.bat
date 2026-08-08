@echo off
REM PS5x – Developer environment setup for Windows
REM Run once after cloning the repository.

setlocal EnableDelayedExpansion

echo [PS5x Setup] Initialising developer environment...

REM ── 1. Check prerequisites ───────────────────────────────────────────────
where cmake >nul 2>&1 || (
    echo [ERROR] cmake not found. Install from https://cmake.org/download/
    exit /b 1
)
where git >nul 2>&1 || (
    echo [ERROR] git not found. Install from https://git-scm.com/
    exit /b 1
)

REM ── 2. Kyty submodule ────────────────────────────────────────────────────
echo [PS5x Setup] Initialising Kyty submodule...
git submodule update --init --recursive --depth 1
if errorlevel 1 (
    echo [WARN] Submodule init failed – continuing without full Kyty source.
    echo        PS5x modules will still compile; Kyty integration requires the submodule.
)

REM ── 3. Install pre-commit hook ────────────────────────────────────────────
if exist ".git\hooks" (
    echo [PS5x Setup] Installing pre-commit hook...
    copy /Y "scripts\pre-commit.sh" ".git\hooks\pre-commit" >nul
    echo [OK] Pre-commit hook installed.
)

REM ── 4. Configure cmake (Release) ────────────────────────────────────────
echo [PS5x Setup] Configuring Release build...
cmake -S . -B build ^
    -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DPS5X_BUILD_TESTS=ON ^
    -DPS5X_ENABLE_DX12=ON ^
    -DPS5X_ENABLE_DX11=ON ^
    -DPS5X_ENABLE_VULKAN=OFF

if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

REM ── 5. Configure cmake (Debug) ────────────────────────────────────────
echo [PS5x Setup] Configuring Debug build...
cmake -S . -B build-debug ^
    -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DPS5X_BUILD_TESTS=ON ^
    -DPS5X_ENABLE_DX12=ON ^
    -DPS5X_ENABLE_DX11=ON ^
    -DPS5X_ENABLE_VULKAN=OFF

echo.
echo [PS5x Setup] Done!
echo.
echo   Build Release:   cmake --build build --config Release --parallel
echo   Build Debug:     cmake --build build-debug --config Debug --parallel
echo   Run tests:       ctest --test-dir build-debug -C Debug --output-on-failure
echo   Run emulator:    build\bin\Release\ps5x.exe
echo.
echo REMINDER: You must supply your own PS5 firmware.
echo           Set emulator.firmware_path in ps5x.toml
