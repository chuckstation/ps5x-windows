# cmake/CompilerFlags.cmake
# PS5x – shared compiler flags for all targets

include_guard(GLOBAL)

# ── Detect compiler ───────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(PS5X_COMPILER_CLANG TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(PS5X_COMPILER_GCC TRUE)
elseif(MSVC)
    set(PS5X_COMPILER_MSVC TRUE)
endif()

# ── Function to apply standard flags to a target ─────────────────────────────
function(ps5x_target_compile_options TARGET)
    if(PS5X_COMPILER_MSVC)
        target_compile_options(${TARGET} PRIVATE
            /W4 /WX-
            /utf-8
            /MP                         # parallel compilation
            $<$<CONFIG:Debug>:/Od /Zi /RTC1>
            $<$<CONFIG:Release>:/O2 /GL /GS>
            $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
        )
        target_compile_definitions(${TARGET} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            $<$<CONFIG:Debug>:PS5X_BUILD_DEBUG=1>
            $<$<CONFIG:Release>:PS5X_BUILD_RELEASE=1 NDEBUG>
        )
    elseif(PS5X_COMPILER_CLANG OR PS5X_COMPILER_GCC)
        target_compile_options(${TARGET} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wno-unused-parameter
            $<$<BOOL:${PS5X_WARNINGS_AS_ERRORS}>:-Werror>
            $<$<CONFIG:Debug>:-O0 -g3>
            $<$<CONFIG:Release>:-O3 -DNDEBUG -fstack-protector-strong -D_FORTIFY_SOURCE=2>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g>
        )
        if(PS5X_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(${TARGET} PRIVATE -fsanitize=address,undefined)
            target_link_options(${TARGET} PRIVATE -fsanitize=address,undefined)
        endif()
        target_compile_definitions(${TARGET} PRIVATE
            $<$<CONFIG:Debug>:PS5X_BUILD_DEBUG=1>
            $<$<CONFIG:Release>:PS5X_BUILD_RELEASE=1 NDEBUG>
        )
    endif()
endfunction()
