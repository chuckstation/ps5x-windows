# cmake/PS5xVersion.cmake
# Embeds git commit hash + project version into a generated header

include_guard(GLOBAL)

find_package(Git QUIET)

if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE PS5X_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE PS5X_GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(NOT PS5X_GIT_HASH)
    set(PS5X_GIT_HASH "unknown")
endif()
if(NOT PS5X_GIT_TAG)
    set(PS5X_GIT_TAG "v${PROJECT_VERSION}")
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/PS5xVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/PS5x/Version.h"
    @ONLY
)
