cmake_minimum_required(VERSION 3.22)

# ── Catch2 Test Framework ────────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2)

FetchContent_MakeAvailable(Catch2)

# ── Test suites ──────────────────────────────────────────────────────────────
add_subdirectory(unit)
