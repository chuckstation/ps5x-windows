# cmake/CompilerFlags.cmake
# ChuckStation5 – shared compiler flags for all targets

include_guard(GLOBAL)

# ── Detect compiler ───────────────────────────────────────────────────────────
if(MSVC)
    set(CHUCKSTATION5_COMPILER_MSVC TRUE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CHUCKSTATION5_COMPILER_CLANG TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CHUCKSTATION5_COMPILER_GCC TRUE)
endif()

# ── Function to apply standard flags to a target ─────────────────────────────
function(chuckstation5_target_compile_options TARGET)
    if(CHUCKSTATION5_COMPILER_MSVC)
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
            $<$<CONFIG:Debug>:CHUCKSTATION5_BUILD_DEBUG=1>
            $<$<CONFIG:Release>:CHUCKSTATION5_BUILD_RELEASE=1 NDEBUG>
        )
    elseif(CHUCKSTATION5_COMPILER_CLANG OR CHUCKSTATION5_COMPILER_GCC)
        target_compile_options(${TARGET} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wno-unused-parameter
            $<$<BOOL:${CHUCKSTATION5_WARNINGS_AS_ERRORS}>:-Werror>
            $<$<CONFIG:Debug>:-O0 -g3>
            $<$<CONFIG:Release>:-O3 -DNDEBUG -fstack-protector-strong -D_FORTIFY_SOURCE=2>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g>
        )
        if(CHUCKSTATION5_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(${TARGET} PRIVATE -fsanitize=address,undefined)
            target_link_options(${TARGET} PRIVATE -fsanitize=address,undefined)
        endif()
        target_compile_definitions(${TARGET} PRIVATE
            $<$<CONFIG:Debug>:CHUCKSTATION5_BUILD_DEBUG=1>
            $<$<CONFIG:Release>:CHUCKSTATION5_BUILD_RELEASE=1 NDEBUG>
        )
    endif()
endfunction()
