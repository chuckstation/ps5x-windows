cmake_minimum_required(VERSION 3.22)

# ─── PS5x ────────────────────────────────────────────────────────────────────
# Research-oriented PlayStation 5 emulator framework for Windows Built on top of
# the Kyty project (MIT, Copyright © 2021 InoriRus) PS5x additions: MIT,
# Copyright © 2024-2026 libaerto Contributors
#
# Repository: https://github.com/libaerto/ps5x-windows Kyty upstream is included
# as a reference / component on Windows only. PS5x modules are cross-platform
# C++20 and compile on any host.
# ─────────────────────────────────────────────────────────────────────────────

project(
  PS5x
  VERSION 1.0.0
  DESCRIPTION "Production-ready PS5 emulator framework for Windows"
  LANGUAGES C CXX)

# ── C++ standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Output directories ────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(cfg Release RelWithDebInfo Debug)
  string(TOUPPER ${cfg} cfg_up)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_up}
      "${CMAKE_BINARY_DIR}/bin/${cfg}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_up}
      "${CMAKE_BINARY_DIR}/bin/${cfg}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_up}
      "${CMAKE_BINARY_DIR}/lib/${cfg}")
endforeach()

# ── Options ───────────────────────────────────────────────────────────────────
option(PS5X_BUILD_TESTS "Build unit tests" ON)
option(PS5X_INCLUDE_KYTY "Include Kyty upstream in the build" OFF)
option(PS5X_ENABLE_VULKAN "Enable Vulkan renderer backend" ON)
option(PS5X_ENABLE_DX11 "Enable DirectX 11 renderer backend" ON)
option(PS5X_ENABLE_DX12 "Enable DirectX 12 renderer backend" ON)
option(PS5X_ENABLE_OPENGL "Enable OpenGL renderer backend" OFF)
option(PS5X_ENABLE_ASAN "Enable AddressSanitizer (Debug)" OFF)
option(PS5X_CLANG_TIDY "Run clang-tidy during build" OFF)
option(PS5X_ENABLE_CRASH_HANDLER "Enable crash handler & minidump" ON)
option(PS5X_ENABLE_METRICS "Enable metrics & telemetry system" ON)
option(PS5X_ENABLE_SAVESTATES "Enable save state system" ON)
option(PS5X_ENABLE_INPUT_MAPPING "Enable configurable input mapping" ON)
option(PS5X_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# ── CMake modules ─────────────────────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(cmake/CompilerFlags.cmake)
include(cmake/PS5xVersion.cmake)

# ── Optional: Kyty upstream (Windows only, requires MinGW or MSVC) ────────────
if(PS5X_INCLUDE_KYTY)
  if(WIN32 AND (MINGW OR MSVC))
    message(STATUS "PS5x: including Kyty upstream build")
    add_subdirectory(kyty-upstream/source)
  else()
    message(
      WARNING "PS5X_INCLUDE_KYTY requires Windows + MinGW or MSVC. Skipping.")
  endif()
endif()

# ── clang-tidy ───────────────────────────────────────────────────────────────
if(PS5X_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  endif()
endif()

# ── PS5x modules ─────────────────────────────────────────────────────────────
add_subdirectory(source/ps5x)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(PS5X_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ── Packaging (CPack) ────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "PS5x")
set(CPACK_PACKAGE_VENDOR "libaerto")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PS5 emulator framework for Windows")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "PS5x")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

if(WIN32)
  set(CPACK_GENERATOR "ZIP;NSIS")
  set(CPACK_NSIS_PACKAGE_NAME "PS5x v${PROJECT_VERSION}")
  set(CPACK_NSIS_DISPLAY_NAME "PS5x - PS5 Emulator Framework")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/ps5x.ico")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
