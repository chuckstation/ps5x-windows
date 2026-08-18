cmake_minimum_required(VERSION 3.22)

# ─── ChuckStation5 modules ────────────────────────────────────────────────────────────
set(CHUCKSTATION5_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(CHUCKSTATION5_GEN_DIR "${CMAKE_BINARY_DIR}/generated")

# ── SDL2 detection ────────────────────────────────────────────────────────
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  message(
    STATUS
      "ChuckStation5: SDL2 ${SDL2_VERSION} found – enabling native Input/Audio backends."
  )
  set(CHUCKSTATION5_SDL2_AVAILABLE TRUE)
else()
  # Try pkg-config fallback
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
    if(SDL2_FOUND)
      set(CHUCKSTATION5_SDL2_AVAILABLE TRUE)
      message(STATUS "ChuckStation5: SDL2 ${SDL2_VERSION} found via pkg-config.")
    endif()
  endif()
endif()

# ── Kyty availability ─────────────────────────────────────────────────────
if(CHUCKSTATION5_INCLUDE_KYTY
   AND WIN32
   AND (MINGW OR MSVC))
  set(CHUCKSTATION5_KYTY_AVAILABLE TRUE)
endif()

# ── Helper macro ──────────────────────────────────────────────────────────
macro(chuckstation5_module NAME)
  set(SRCS ${ARGN})
  add_library(chuckstation5_${NAME} STATIC ${SRCS})

  target_include_directories(chuckstation5_${NAME} PUBLIC "${CHUCKSTATION5_INCLUDE_DIR}"
                                                 "${CHUCKSTATION5_GEN_DIR}")
  target_compile_features(chuckstation5_${NAME} PUBLIC cxx_std_20)
  chuckstation5_target_compile_options(chuckstation5_${NAME})

  if(CHUCKSTATION5_ENABLE_VULKAN)
    target_compile_definitions(chuckstation5_${NAME} PUBLIC CHUCKSTATION5_ENABLE_VULKAN=1)
  endif()
  if(CHUCKSTATION5_ENABLE_DX11)
    target_compile_definitions(chuckstation5_${NAME} PUBLIC CHUCKSTATION5_ENABLE_DX11=1)
  endif()
  if(CHUCKSTATION5_ENABLE_DX12)
    target_compile_definitions(chuckstation5_${NAME} PUBLIC CHUCKSTATION5_ENABLE_DX12=1)
  endif()
  if(CHUCKSTATION5_ENABLE_OPENGL)
    target_compile_definitions(chuckstation5_${NAME} PUBLIC CHUCKSTATION5_ENABLE_OPENGL=1)
  endif()
  if(CHUCKSTATION5_SDL2_AVAILABLE)
    target_compile_definitions(chuckstation5_${NAME} PUBLIC CHUCKSTATION5_HAVE_SDL2=1)
    if(SDL2_FOUND AND TARGET SDL2::SDL2)
      target_link_libraries(chuckstation5_${NAME} PUBLIC SDL2::SDL2)
    else()
      target_include_directories(chuckstation5_${NAME} PUBLIC ${SDL2_INCLUDE_DIRS})
      target_link_libraries(chuckstation5_${NAME} PUBLIC ${SDL2_LIBRARIES})
    endif()
  endif()
  if(CHUCKSTATION5_KYTY_AVAILABLE)
    target_compile_definitions(chuckstation5_${NAME} PUBLIC CHUCKSTATION5_KYTY_AVAILABLE=1)
  endif()
endmacro()

# ── Logger ────────────────────────────────────────────────────────────────
chuckstation5_module(logger src/Logger/Logger.cpp)

# ── Config ────────────────────────────────────────────────────────────────
chuckstation5_module(config src/Config/Config.cpp)
target_link_libraries(chuckstation5_config PUBLIC chuckstation5_logger)

# ── Memory Manager ────────────────────────────────────────────────────────
chuckstation5_module(memory src/Memory/Memory.cpp)
target_link_libraries(chuckstation5_memory PUBLIC chuckstation5_logger)

# ── Kernel (legacy stubs – kept for compatibility) ────────────────────────
chuckstation5_module(kernel src/Kernel/Kernel.cpp)
target_link_libraries(chuckstation5_kernel PUBLIC chuckstation5_logger chuckstation5_memory)

# ── Loader ────────────────────────────────────────────────────────────────
chuckstation5_module(loader src/Loader/Loader.cpp)
target_link_libraries(chuckstation5_loader PUBLIC chuckstation5_logger chuckstation5_memory)

# ── Input ─────────────────────────────────────────────────────────────────
chuckstation5_module(input src/Input/Input.cpp)
target_link_libraries(chuckstation5_input PUBLIC chuckstation5_logger chuckstation5_config)

# ── Audio ─────────────────────────────────────────────────────────────────
chuckstation5_module(audio src/Audio/Audio.cpp)
target_link_libraries(chuckstation5_audio PUBLIC chuckstation5_logger chuckstation5_config)

# ── Filesystem ────────────────────────────────────────────────────────────
chuckstation5_module(filesystem src/Filesystem/Filesystem.cpp)
target_link_libraries(chuckstation5_filesystem PUBLIC chuckstation5_logger)

# ── Renderer ──────────────────────────────────────────────────────────────
chuckstation5_module(renderer src/Renderer/VulkanBackend.cpp)
target_link_libraries(chuckstation5_renderer PUBLIC chuckstation5_logger chuckstation5_config)

# ── GPU ───────────────────────────────────────────────────────────────────
chuckstation5_module(gpu src/GPU/GPU.cpp)
target_link_libraries(chuckstation5_gpu PUBLIC chuckstation5_logger chuckstation5_renderer)

# ── Debugger ─────────────────────────────────────────────────────────────
chuckstation5_module(debugger src/Debugger/Debugger.cpp)
target_link_libraries(chuckstation5_debugger PUBLIC chuckstation5_logger chuckstation5_module_registry)

# ── UI ───────────────────────────────────────────────────────────────────
chuckstation5_module(ui src/UI/UI.cpp)
target_link_libraries(chuckstation5_ui PUBLIC chuckstation5_logger chuckstation5_config chuckstation5_perf_tools
                                     chuckstation5_cpu)

# ── KytyAdapter ──────────────────────────────────────────────────────────
chuckstation5_module(kyty_adapter src/KytyAdapter/KytyAdapter.cpp)
target_link_libraries(chuckstation5_kyty_adapter PUBLIC chuckstation5_logger chuckstation5_memory)
if(CHUCKSTATION5_KYTY_AVAILABLE)
  target_include_directories(
    chuckstation5_kyty_adapter
    PRIVATE "${CMAKE_SOURCE_DIR}/kyty-upstream/source/emulator/include")
endif()

# ── Main executable ──────────────────────────────────────────────────────
add_executable(chuckstation5 src/main.cpp)

target_link_libraries(
  chuckstation5
  PRIVATE chuckstation5_logger
          chuckstation5_config
          chuckstation5_memory
          chuckstation5_kernel
          chuckstation5_loader
          chuckstation5_input
          chuckstation5_audio
          chuckstation5_filesystem
          chuckstation5_gpu
          chuckstation5_renderer
          chuckstation5_debugger
          chuckstation5_ui
          chuckstation5_kyty_adapter
          chuckstation5_runtime
          chuckstation5_kernel_runtime
          chuckstation5_process)
if(CHUCKSTATION5_ENABLE_CRASH_HANDLER)
  target_link_libraries(chuckstation5 PRIVATE chuckstation5_crash_handler)
endif()
if(CHUCKSTATION5_ENABLE_METRICS)
  target_link_libraries(chuckstation5 PRIVATE chuckstation5_metrics)
endif()
if(CHUCKSTATION5_ENABLE_SAVESTATES)
  target_link_libraries(chuckstation5 PRIVATE chuckstation5_save_state)
endif()
if(CHUCKSTATION5_ENABLE_INPUT_MAPPING)
  target_link_libraries(chuckstation5 PRIVATE chuckstation5_input_mapping)
endif()
target_include_directories(chuckstation5 PRIVATE "${CHUCKSTATION5_INCLUDE_DIR}" "${CHUCKSTATION5_GEN_DIR}")
target_compile_features(chuckstation5 PRIVATE cxx_std_20)
chuckstation5_target_compile_options(chuckstation5)

if(WIN32)
  target_sources(chuckstation5 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/chuckstation5.rc")
  if(MSVC)
    target_link_options(chuckstation5 PRIVATE "/MANIFEST:NO")
  endif()
endif()
install(TARGETS chuckstation5 DESTINATION bin)

# ── Runtime Manager ───────────────────────────────────────────────────────
chuckstation5_module(runtime src/Runtime/Runtime.cpp)
target_link_libraries(
  chuckstation5_runtime
  PUBLIC chuckstation5_logger
         chuckstation5_config
         chuckstation5_memory
         chuckstation5_kernel
         chuckstation5_filesystem
         chuckstation5_loader
         chuckstation5_kyty_adapter
         chuckstation5_kernel_runtime
         chuckstation5_process
         chuckstation5_renderer
         chuckstation5_gpu
         chuckstation5_audio
         chuckstation5_input
         chuckstation5_debugger
         chuckstation5_ui)

# ── Kernel Runtime ────────────────────────────────────────────────────────
chuckstation5_module(kernel_runtime src/KernelRuntime/KernelRuntime.cpp)
target_link_libraries(chuckstation5_kernel_runtime PUBLIC chuckstation5_logger chuckstation5_memory)

# ── Process Manager ───────────────────────────────────────────────────────
chuckstation5_module(process src/Process/Process.cpp)
target_link_libraries(
  chuckstation5_process
  PUBLIC chuckstation5_logger
         chuckstation5_memory
         chuckstation5_filesystem
         chuckstation5_loader
         chuckstation5_kernel_runtime
         chuckstation5_syscalls
         chuckstation5_cpu)

# ── Phase 4 modules ───────────────────────────────────────────────────────

# Execution framework
chuckstation5_module(execution src/Execution/Execution.cpp)
target_link_libraries(
  chuckstation5_execution
  PUBLIC chuckstation5_logger
         chuckstation5_loader
         chuckstation5_process
         chuckstation5_kernel_runtime
         chuckstation5_filesystem
         chuckstation5_debugger
         chuckstation5_kyty_adapter
         chuckstation5_memory)

# Module registry
chuckstation5_module(module_registry src/ModuleRegistry/ModuleRegistry.cpp)
target_link_libraries(chuckstation5_module_registry PUBLIC chuckstation5_logger chuckstation5_loader)

# Kernel services
chuckstation5_module(kernel_services src/KernelServices/KernelServices.cpp)
target_link_libraries(chuckstation5_kernel_services PUBLIC chuckstation5_logger chuckstation5_memory
                                                  chuckstation5_kernel_runtime)

# Memory diagnostics
chuckstation5_module(memory_diag src/MemoryDiag/MemoryDiag.cpp)
target_link_libraries(chuckstation5_memory_diag PUBLIC chuckstation5_logger chuckstation5_memory)

# ── Phase 5 modules ───────────────────────────────────────────────────────

# Runtime Events
chuckstation5_module(runtime_events src/RuntimeEvents/RuntimeEvents.cpp)
target_link_libraries(chuckstation5_runtime_events PUBLIC chuckstation5_logger)

# Dynamic Linker
chuckstation5_module(dynamic_linker src/DynamicLinker/DynamicLinker.cpp)
target_link_libraries(
  chuckstation5_dynamic_linker PUBLIC chuckstation5_logger chuckstation5_memory chuckstation5_module_registry
                             chuckstation5_runtime_events)

# Shader Cache
chuckstation5_module(shader_cache src/ShaderCache/ShaderCache.cpp)
target_link_libraries(chuckstation5_shader_cache PUBLIC chuckstation5_logger chuckstation5_runtime_events)

# Performance Tools
chuckstation5_module(perf_tools src/PerfTools/PerfTools.cpp)
# ── Phase 7 modules ───────────────────────────────────────────────────────
chuckstation5_module(cpu src/Cpu/Cpu.cpp)
target_link_libraries(chuckstation5_cpu PUBLIC chuckstation5_logger chuckstation5_memory
                                      chuckstation5_runtime_events)

chuckstation5_module(syscalls src/Syscalls/Syscalls.cpp)
target_link_libraries(chuckstation5_syscalls PUBLIC chuckstation5_logger chuckstation5_runtime_events
                                           chuckstation5_cpu)

chuckstation5_module(command_processor src/CommandProcessor/CommandProcessor.cpp)
target_link_libraries(
  chuckstation5_command_processor PUBLIC chuckstation5_logger chuckstation5_renderer chuckstation5_gpu
                                chuckstation5_runtime_events)

target_link_libraries(chuckstation5_perf_tools PUBLIC chuckstation5_logger)

# ── Phase 9: Production modules ────────────────────────────────────────────

# Crash Handler
if(CHUCKSTATION5_ENABLE_CRASH_HANDLER)
  chuckstation5_module(crash_handler src/CrashHandler/CrashHandler.cpp)
  target_link_libraries(chuckstation5_crash_handler PUBLIC chuckstation5_logger)
  if(WIN32)
    target_link_libraries(chuckstation5_crash_handler PRIVATE dbghelp)
  endif()
endif()

# Metrics & Telemetry
if(CHUCKSTATION5_ENABLE_METRICS)
  chuckstation5_module(metrics src/Metrics/Metrics.cpp)
  target_link_libraries(chuckstation5_metrics PUBLIC chuckstation5_logger)
endif()

# Save States
if(CHUCKSTATION5_ENABLE_SAVESTATES)
  chuckstation5_module(save_state src/SaveState/SaveState.cpp)
  target_link_libraries(chuckstation5_save_state PUBLIC chuckstation5_logger chuckstation5_memory chuckstation5_cpu
                                               chuckstation5_gpu)
endif()

# Input Mapping
if(CHUCKSTATION5_ENABLE_INPUT_MAPPING)
  chuckstation5_module(input_mapping src/InputMapping/InputMapping.cpp)
  target_link_libraries(chuckstation5_input_mapping PUBLIC chuckstation5_logger chuckstation5_config)
endif()
