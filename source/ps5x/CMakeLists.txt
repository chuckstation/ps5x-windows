cmake_minimum_required(VERSION 3.22)

# ─── PS5x modules ────────────────────────────────────────────────────────────
set(PS5X_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(PS5X_GEN_DIR "${CMAKE_BINARY_DIR}/generated")

# ── SDL2 detection ────────────────────────────────────────────────────────
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  message(
    STATUS
      "PS5x: SDL2 ${SDL2_VERSION} found – enabling native Input/Audio backends."
  )
  set(PS5X_SDL2_AVAILABLE TRUE)
else()
  # Try pkg-config fallback
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
    if(SDL2_FOUND)
      set(PS5X_SDL2_AVAILABLE TRUE)
      message(STATUS "PS5x: SDL2 ${SDL2_VERSION} found via pkg-config.")
    endif()
  endif()
endif()

# ── Kyty availability ─────────────────────────────────────────────────────
if(PS5X_INCLUDE_KYTY
   AND WIN32
   AND (MINGW OR MSVC))
  set(PS5X_KYTY_AVAILABLE TRUE)
endif()

# ── Helper macro ──────────────────────────────────────────────────────────
# cmake-lint: disable=C0103
macro(ps5x_module NAME)
  set(SRCS ${ARGN})
  add_library(ps5x_${NAME} STATIC ${SRCS})

  target_include_directories(ps5x_${NAME} PUBLIC "${PS5X_INCLUDE_DIR}"
                                                 "${PS5X_GEN_DIR}")
  target_compile_features(ps5x_${NAME} PUBLIC cxx_std_20)
  ps5x_target_compile_options(ps5x_${NAME})

  if(PS5X_ENABLE_VULKAN)
    target_compile_definitions(ps5x_${NAME} PUBLIC PS5X_ENABLE_VULKAN=1)
  endif()
  if(PS5X_ENABLE_DX11)
    target_compile_definitions(ps5x_${NAME} PUBLIC PS5X_ENABLE_DX11=1)
  endif()
  if(PS5X_ENABLE_DX12)
    target_compile_definitions(ps5x_${NAME} PUBLIC PS5X_ENABLE_DX12=1)
  endif()
  if(PS5X_ENABLE_OPENGL)
    target_compile_definitions(ps5x_${NAME} PUBLIC PS5X_ENABLE_OPENGL=1)
  endif()
  if(PS5X_SDL2_AVAILABLE)
    target_compile_definitions(ps5x_${NAME} PUBLIC PS5X_HAVE_SDL2=1)
    if(SDL2_FOUND AND TARGET SDL2::SDL2)
      target_link_libraries(ps5x_${NAME} PUBLIC SDL2::SDL2)
    else()
      target_include_directories(ps5x_${NAME} PUBLIC ${SDL2_INCLUDE_DIRS})
      target_link_libraries(ps5x_${NAME} PUBLIC ${SDL2_LIBRARIES})
    endif()
  endif()
  if(PS5X_KYTY_AVAILABLE)
    target_compile_definitions(ps5x_${NAME} PUBLIC PS5X_KYTY_AVAILABLE=1)
  endif()
endmacro()

# ── Logger ────────────────────────────────────────────────────────────────
ps5x_module(logger src/Logger/Logger.cpp)

# ── Config ────────────────────────────────────────────────────────────────
ps5x_module(config src/Config/Config.cpp)
target_link_libraries(ps5x_config PUBLIC ps5x_logger)

# ── Memory Manager ────────────────────────────────────────────────────────
ps5x_module(memory src/Memory/Memory.cpp)
target_link_libraries(ps5x_memory PUBLIC ps5x_logger)

# ── Kernel (legacy stubs – kept for compatibility) ────────────────────────
ps5x_module(kernel src/Kernel/Kernel.cpp)
target_link_libraries(ps5x_kernel PUBLIC ps5x_logger ps5x_memory)

# ── Loader ────────────────────────────────────────────────────────────────
ps5x_module(loader src/Loader/Loader.cpp)
target_link_libraries(ps5x_loader PUBLIC ps5x_logger ps5x_memory)

# ── Input ─────────────────────────────────────────────────────────────────
ps5x_module(input src/Input/Input.cpp)
target_link_libraries(ps5x_input PUBLIC ps5x_logger ps5x_config)

# ── Audio ─────────────────────────────────────────────────────────────────
ps5x_module(audio src/Audio/Audio.cpp)
target_link_libraries(ps5x_audio PUBLIC ps5x_logger ps5x_config)

# ── Filesystem ────────────────────────────────────────────────────────────
ps5x_module(filesystem src/Filesystem/Filesystem.cpp)
target_link_libraries(ps5x_filesystem PUBLIC ps5x_logger)

# ── Renderer ──────────────────────────────────────────────────────────────
ps5x_module(renderer src/Renderer/VulkanBackend.cpp)
target_link_libraries(ps5x_renderer PUBLIC ps5x_logger ps5x_config)

# ── GPU ───────────────────────────────────────────────────────────────────
ps5x_module(gpu src/GPU/GPU.cpp)
target_link_libraries(ps5x_gpu PUBLIC ps5x_logger ps5x_renderer)

# ── Debugger ─────────────────────────────────────────────────────────────
ps5x_module(debugger src/Debugger/Debugger.cpp)
target_link_libraries(ps5x_debugger PUBLIC ps5x_logger ps5x_module_registry)

# ── UI ───────────────────────────────────────────────────────────────────
ps5x_module(ui src/UI/UI.cpp)
target_link_libraries(ps5x_ui PUBLIC ps5x_logger ps5x_config ps5x_perf_tools
                                     ps5x_cpu)

# ── KytyAdapter ──────────────────────────────────────────────────────────
ps5x_module(kyty_adapter src/KytyAdapter/KytyAdapter.cpp)
target_link_libraries(ps5x_kyty_adapter PUBLIC ps5x_logger ps5x_memory)
if(PS5X_KYTY_AVAILABLE)
  target_include_directories(
    ps5x_kyty_adapter
    PRIVATE "${CMAKE_SOURCE_DIR}/kyty-upstream/source/emulator/include")
endif()

# ── Main executable ──────────────────────────────────────────────────────
add_executable(ps5x src/main.cpp)

target_link_libraries(
  ps5x
  PRIVATE ps5x_logger
          ps5x_config
          ps5x_memory
          ps5x_kernel
          ps5x_loader
          ps5x_input
          ps5x_audio
          ps5x_filesystem
          ps5x_gpu
          ps5x_renderer
          ps5x_debugger
          ps5x_ui
          ps5x_kyty_adapter
          ps5x_runtime
          ps5x_kernel_runtime
          ps5x_process)
if(PS5X_ENABLE_CRASH_HANDLER)
  target_link_libraries(ps5x PRIVATE ps5x_crash_handler)
endif()
if(PS5X_ENABLE_METRICS)
  target_link_libraries(ps5x PRIVATE ps5x_metrics)
endif()
if(PS5X_ENABLE_SAVESTATES)
  target_link_libraries(ps5x PRIVATE ps5x_save_state)
endif()
if(PS5X_ENABLE_INPUT_MAPPING)
  target_link_libraries(ps5x PRIVATE ps5x_input_mapping)
endif()
target_include_directories(ps5x PRIVATE "${PS5X_INCLUDE_DIR}" "${PS5X_GEN_DIR}")
target_compile_features(ps5x PRIVATE cxx_std_20)
ps5x_target_compile_options(ps5x)

if(WIN32)
  target_sources(ps5x PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ps5x.rc")
  if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_link_options(ps5x PRIVATE "/MANIFEST:NO")
  endif()
endif()
install(TARGETS ps5x DESTINATION bin)

# ── Runtime Manager ───────────────────────────────────────────────────────
ps5x_module(runtime src/Runtime/Runtime.cpp)
target_link_libraries(
  ps5x_runtime
  PUBLIC ps5x_logger
         ps5x_config
         ps5x_memory
         ps5x_kernel
         ps5x_filesystem
         ps5x_loader
         ps5x_kyty_adapter
         ps5x_kernel_runtime
         ps5x_process
         ps5x_renderer
         ps5x_gpu
         ps5x_audio
         ps5x_input
         ps5x_debugger
         ps5x_ui)

# ── Kernel Runtime ────────────────────────────────────────────────────────
ps5x_module(kernel_runtime src/KernelRuntime/KernelRuntime.cpp)
target_link_libraries(ps5x_kernel_runtime PUBLIC ps5x_logger ps5x_memory)

# ── Process Manager ───────────────────────────────────────────────────────
ps5x_module(process src/Process/Process.cpp)
target_link_libraries(
  ps5x_process
  PUBLIC ps5x_logger
         ps5x_memory
         ps5x_filesystem
         ps5x_loader
         ps5x_kernel_runtime
         ps5x_syscalls
         ps5x_cpu)

# ── Phase 4 modules ───────────────────────────────────────────────────────

# Execution framework
ps5x_module(execution src/Execution/Execution.cpp)
target_link_libraries(
  ps5x_execution
  PUBLIC ps5x_logger
         ps5x_loader
         ps5x_process
         ps5x_kernel_runtime
         ps5x_filesystem
         ps5x_debugger
         ps5x_kyty_adapter
         ps5x_memory)

# Module registry
ps5x_module(module_registry src/ModuleRegistry/ModuleRegistry.cpp)
target_link_libraries(ps5x_module_registry PUBLIC ps5x_logger ps5x_loader)

# Kernel services
ps5x_module(kernel_services src/KernelServices/KernelServices.cpp)
target_link_libraries(ps5x_kernel_services PUBLIC ps5x_logger ps5x_memory
                                                  ps5x_kernel_runtime)

# Memory diagnostics
ps5x_module(memory_diag src/MemoryDiag/MemoryDiag.cpp)
target_link_libraries(ps5x_memory_diag PUBLIC ps5x_logger ps5x_memory)

# ── Phase 5 modules ───────────────────────────────────────────────────────

# Runtime Events
ps5x_module(runtime_events src/RuntimeEvents/RuntimeEvents.cpp)
target_link_libraries(ps5x_runtime_events PUBLIC ps5x_logger)

# Dynamic Linker
ps5x_module(dynamic_linker src/DynamicLinker/DynamicLinker.cpp)
target_link_libraries(
  ps5x_dynamic_linker PUBLIC ps5x_logger ps5x_memory ps5x_module_registry
                             ps5x_runtime_events)

# Shader Cache
ps5x_module(shader_cache src/ShaderCache/ShaderCache.cpp)
target_link_libraries(ps5x_shader_cache PUBLIC ps5x_logger ps5x_runtime_events)

# Performance Tools
ps5x_module(perf_tools src/PerfTools/PerfTools.cpp)
# ── Phase 7 modules ───────────────────────────────────────────────────────
ps5x_module(cpu src/Cpu/Cpu.cpp)
target_link_libraries(ps5x_cpu PUBLIC ps5x_logger ps5x_memory
                                      ps5x_runtime_events)

ps5x_module(syscalls src/Syscalls/Syscalls.cpp)
target_link_libraries(ps5x_syscalls PUBLIC ps5x_logger ps5x_runtime_events
                                           ps5x_cpu)

ps5x_module(command_processor src/CommandProcessor/CommandProcessor.cpp)
target_link_libraries(
  ps5x_command_processor PUBLIC ps5x_logger ps5x_renderer ps5x_gpu
                                ps5x_runtime_events)

target_link_libraries(ps5x_perf_tools PUBLIC ps5x_logger)

# ── Phase 9: Production modules ────────────────────────────────────────────

# Crash Handler
if(PS5X_ENABLE_CRASH_HANDLER)
  ps5x_module(crash_handler src/CrashHandler/CrashHandler.cpp)
  target_link_libraries(ps5x_crash_handler PUBLIC ps5x_logger)
  if(WIN32)
    target_link_libraries(ps5x_crash_handler PRIVATE dbghelp)
  endif()
endif()

# Metrics & Telemetry
if(PS5X_ENABLE_METRICS)
  ps5x_module(metrics src/Metrics/Metrics.cpp)
  target_link_libraries(ps5x_metrics PUBLIC ps5x_logger)
endif()

# Save States
if(PS5X_ENABLE_SAVESTATES)
  ps5x_module(save_state src/SaveState/SaveState.cpp)
  target_link_libraries(ps5x_save_state PUBLIC ps5x_logger ps5x_memory ps5x_cpu
                                               ps5x_gpu)
endif()

# Input Mapping
if(PS5X_ENABLE_INPUT_MAPPING)
  ps5x_module(input_mapping src/InputMapping/InputMapping.cpp)
  target_link_libraries(ps5x_input_mapping PUBLIC ps5x_logger ps5x_config)
endif()
