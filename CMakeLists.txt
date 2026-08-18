cmake_minimum_required(VERSION 3.22)

# ─── ChuckStation5 ────────────────────────────────────────────────────────────────────
# Research-oriented PlayStation 5 emulator framework for Windows Built on top of
# the Kyty project (MIT, Copyright © 2021 InoriRus) ChuckStation5 additions: MIT,
# Copyright © 2024-2026 libaerto Contributors
#
# Repository: https://github.com/libaerto/chuckstation5-windows Kyty upstream is included
# as a reference / component on Windows only. ChuckStation5 modules are cross-platform
# C++20 and compile on any host.
# ─────────────────────────────────────────────────────────────────────────────

project(
  ChuckStation5
  VERSION 1.0.0
  DESCRIPTION
    "Production-ready PS5 emulator framework for Windows (libaerto/chuckstation5-windows)"
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

foreach(CFG Release RelWithDebInfo Debug)
  string(TOUPPER ${CFG} CFG_UP)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UP} "${CMAKE_BINARY_DIR}/bin/${CFG}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UP} "${CMAKE_BINARY_DIR}/bin/${CFG}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG_UP} "${CMAKE_BINARY_DIR}/lib/${CFG}")
endforeach()

# ── Options ───────────────────────────────────────────────────────────────────
option(CHUCKSTATION5_BUILD_TESTS "Build unit tests" ON)
option(CHUCKSTATION5_INCLUDE_KYTY "Include Kyty upstream in the build" OFF)
option(CHUCKSTATION5_ENABLE_VULKAN "Enable Vulkan renderer backend" ON)
option(CHUCKSTATION5_ENABLE_DX11 "Enable DirectX 11 renderer backend" ON)
option(CHUCKSTATION5_ENABLE_DX12 "Enable DirectX 12 renderer backend" ON)
option(CHUCKSTATION5_ENABLE_OPENGL "Enable OpenGL renderer backend" OFF)
option(CHUCKSTATION5_ENABLE_ASAN "Enable AddressSanitizer (Debug)" OFF)
option(CHUCKSTATION5_CLANG_TIDY "Run clang-tidy during build" OFF)
option(CHUCKSTATION5_ENABLE_CRASH_HANDLER "Enable crash handler & minidump" ON)
option(CHUCKSTATION5_ENABLE_METRICS "Enable metrics & telemetry system" ON)
option(CHUCKSTATION5_ENABLE_SAVESTATES "Enable save state system" ON)
option(CHUCKSTATION5_ENABLE_INPUT_MAPPING "Enable configurable input mapping" ON)
option(CHUCKSTATION5_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# ── CMake modules ─────────────────────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(cmake/CompilerFlags.cmake)
include(cmake/ChuckStation5Version.cmake)

# ── Optional: Kyty upstream (Windows only, requires MinGW or MSVC) ────────────
if(CHUCKSTATION5_INCLUDE_KYTY)
  if(WIN32 AND (MINGW OR MSVC))
    message(STATUS "ChuckStation5: including Kyty upstream build")
    add_subdirectory(kyty-upstream/source)
  else()
    message(
      WARNING "CHUCKSTATION5_INCLUDE_KYTY requires Windows + MinGW or MSVC. Skipping.")
  endif()
endif()

# ── clang-tidy ───────────────────────────────────────────────────────────────
if(CHUCKSTATION5_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  endif()
endif()

# ── ChuckStation5 modules ─────────────────────────────────────────────────────────────
add_subdirectory(source/chuckstation5)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(CHUCKSTATION5_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ── Packaging (CPack) ────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "ChuckStation5")
set(CPACK_PACKAGE_VENDOR "libaerto")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PS5 emulator framework for Windows")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ChuckStation5")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

if(WIN32)
  set(CPACK_GENERATOR "ZIP;NSIS")
  set(CPACK_NSIS_PACKAGE_NAME "ChuckStation5 v${PROJECT_VERSION}")
  set(CPACK_NSIS_DISPLAY_NAME "ChuckStation5 - PS5 Emulator Framework")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/chuckstation5.ico")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
